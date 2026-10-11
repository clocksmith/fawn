import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import assert from 'node:assert/strict';
import { observeMemory } from './observe-memory.mjs';

const [policyPath, providersPath, lane, output, mode = 'diagnostic', admissionPath] = process.argv.slice(2);
assert(['doe', 'dawn'].includes(lane));
assert(['diagnostic', 'qualification', 'measurement'].includes(mode));
const hash = async (path) => {
  const digest = createHash('sha256');
  for await (const bytes of createReadStream(path)) digest.update(bytes);
  return digest.digest('hex');
};
const read = async (path) => JSON.parse(await readFile(path));
const policy = await read(policyPath);
const contract = await read(policy.generationContract);
const reference = await read(policy.reference);
const providers = await read(providersPath);
assert.equal(await hash(policy.generationContract), policy.generationContractSha256);
assert.equal(await hash(policy.reference), policy.referenceSha256);
for (const artifact of [...providers.archives, ...providers.files]) {
  assert.equal(await hash(artifact.path), artifact.sha256, artifact.path);
}
for (const artifact of contract.model.files) {
  assert.equal(await hash(resolve(contract.model.path, artifact.path)), artifact.sha256, artifact.path);
}
const test = contract.cases.find((entry) => entry.id === policy.caseId);
const expected = reference.cases.find((entry) => entry.id === policy.caseId);
assert.equal(expected.promptTokenIds.length, policy.inputTokens);
assert.equal(test.maxTokens, policy.maximumOutputTokens);
if (mode === 'measurement') {
  assert(admissionPath, 'Measurement requires a successful exact-package qualification');
  const admission = await read(admissionPath);
  assert.equal(admission.classification, 'qualification');
  assert.equal(admission.passed, true);
  assert.equal(admission.lane, lane);
  assert.equal(admission.policySha256, await hash(policyPath));
  assert.equal(admission.providerManifestSha256, await hash(providersPath));
  assert.equal(admission.runnerSha256, await hash(fileURLToPath(import.meta.url)));
}
const devices = [];
const destroyed = new WeakSet();
let phase = 'provider-init';
const observer = mode === 'diagnostic' ? observeMemory(() => phase, policy) : null;
const checkpoints = [];
const rows = [];
let model, poolModule, failure, cleanup, loadMs, runtimeSession, providerIdentity, adapterIdentity;
const checkpoint = async (reason) => {
  observer?.snapshot(reason);
  // Pool internals are explicitly diagnostic-only, never the executor API.
  const pools = mode === 'diagnostic' && poolModule && model?.loaded
    ? [poolModule.getBufferPool().getStats()] : null;
  checkpoints.push({ phase, reason, host: process.memoryUsage(), pools });
};
try {
  if (lane === 'doe') process.env.DOE_WEBGPU_LIB = providers.doeLibrary;
  const provider = await import(pathToFileURL(providers[`${lane}Module`]).href);
  Object.assign(globalThis, provider.globals);
  const gpu = provider.create(['backend=vulkan']);
  providerIdentity = typeof provider.providerInfo === 'function' ? provider.providerInfo() : { provider: lane };
  if (lane === 'doe') {
    const mappings = await readFile('/proc/self/maps', 'utf8');
    assert(mappings.includes(providers.doeLibrary), 'Selected Doe native library is not loaded');
  }
  const requestAdapter = gpu.requestAdapter.bind(gpu);
  gpu.requestAdapter = async (...args) => {
    const adapter = await requestAdapter(...args);
    assert(adapter, 'No explicitly selected Vulkan adapter');
    adapterIdentity = { vendor: adapter.info.vendor, device: adapter.info.device,
      architecture: adapter.info.architecture, description: adapter.info.description };
    const requestDevice = adapter.requestDevice.bind(adapter);
    adapter.requestDevice = async (...args) => {
      const device = await requestDevice(...args);
      observer?.attach(device);
      const destroy = device.destroy.bind(device);
      device.destroy = () => { const result = destroy(); destroyed.add(device); return result; };
      devices.push(device);
      return device;
    };
    return adapter;
  };
  Object.defineProperty(globalThis, 'navigator', { value: { gpu }, configurable: true });
  const api = await import(pathToFileURL(providers.dopplerModule).href);
  if (mode === 'diagnostic') poolModule = await import(pathToFileURL(resolve(dirname(providers.dopplerModule), 'memory/buffer-pool.js')).href);
  phase = 'load';
  await checkpoint('before-load');
  const start = performance.now();
  model = await api.load({ url: pathToFileURL(contract.model.path + '/').href },
    { cache: false, isolatedLoader: true, onProgress() {} });
  loadMs = performance.now() - start;
  assert(/amd|radeon/i.test(JSON.stringify(model.deviceInfo)));
  assert(!/swiftshader|llvmpipe|software/i.test(JSON.stringify(model.deviceInfo)));
  runtimeSession = model.advanced.getResolvedRuntimeSession();
  assert.equal(runtimeSession.runtime.session.kvcache.maxSeqLen, policy.contextCapacityTokens);
  await checkpoint('loaded');
  const options = { ...contract.generation, maxTokens: test.maxTokens };
  delete options.eosTokenIds;
  assert.deepEqual(model.advanced.tokenizePrompt(test.prompt, options), expected.promptTokenIds);
  const generate = async (label) => {
    phase = label;
    const tokens = [], chunks = [];
    const start = performance.now();
    let firstTokenMs = null;
    for await (const chunk of model.generate(test.prompt, { ...options, onToken(id) { tokens.push(id); } })) {
      firstTokenMs ??= performance.now() - start;
      chunks.push(chunk);
    }
    const row = { phase, firstTokenMs, completeMs: performance.now() - start,
      tokens, text: chunks.join(''), stats: model.advanced.getStats() };
    rows.push(row);
    assert.deepEqual(tokens, expected.tokenIds, `${label}: generated token mismatch`);
    assert.equal(row.text, expected.outputText, `${label}: complete text mismatch`);
    assert.equal(row.stats.stopReason, 'stop-token');
    await checkpoint('generation-completed');
  };
  await generate('initial');
  for (let i = 0; i < policy.warmupRuns; i++) await generate('warmup');
  for (let i = 0; i < policy.residentRuns; i++) await generate(`resident-${i}`);
  phase = 'idle';
  await checkpoint('idle-start');
  // This is an observation window, never the completion acceptance mechanism.
  await new Promise((done) => setTimeout(done, policy.idleObservationMs));
  await checkpoint('idle-end');
  phase = 'cancellation';
  const controller = new AbortController();
  const chunks = [];
  let cancellationError = null;
  try {
    for await (const chunk of model.generate(test.prompt, { ...options, signal: controller.signal })) {
      chunks.push(chunk);
      if (chunks.length === contract.cancellation.abortAfterStreamChunks) controller.abort();
    }
  } catch (error) { cancellationError = { name: error.name, message: error.message }; }
  assert(controller.signal.aborted);
  assert.equal(chunks.length, contract.cancellation.abortAfterStreamChunks);
  assert(expected.outputText.startsWith(chunks.join('')));
  for (const device of devices) if (!destroyed.has(device)) await device.queue.onSubmittedWorkDone();
  rows.push({ phase, chunks, cancellationError, completionSettled: true });
  await checkpoint('cancellation-settled');
  await generate('following-cancellation');
} catch (error) { failure = { name: error.name, message: error.message, stack: error.stack }; }
finally {
  try {
    phase = 'cleanup';
    await model?.unload();
    for (const device of devices) if (!destroyed.has(device)) { await device.queue.onSubmittedWorkDone(); device.destroy(); }
    cleanup = { unloaded: model ? !model.loaded : null, destroyedDevices: devices.filter((d) => destroyed.has(d)).length };
    await checkpoint('unloaded');
  } catch (error) { cleanup = { error: error.message }; }
  observer?.close();
  await writeFile(output, JSON.stringify({ schemaVersion: 1, classification: mode, lane,
    policySha256: await hash(policyPath), providerManifestSha256: await hash(providersPath),
    runnerSha256: await hash(fileURLToPath(import.meta.url)), observerSha256: await hash(resolve(dirname(fileURLToPath(import.meta.url)), 'observe-memory.mjs')),
    providerIdentity, adapterIdentity, admissionSha256: admissionPath ? await hash(admissionPath) : null,
    loadMs, runtimeSession, checkpoints, rows, observation: observer?.result() ?? null,
    peakProcessRssBytes: process.resourceUsage().maxRSS * 1024,
    cleanup, passed: !failure && !cleanup?.error, failure: failure ?? null }, null, 2) + '\n');
}
if (failure || cleanup?.error) { console.error(failure ?? cleanup); process.exitCode = 1; }
