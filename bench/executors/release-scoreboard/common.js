// Shared provider loading, API observation and receipt writing for the
// release-scoreboard executors. Both providers run this same file in the same
// Node process shape; only the provider module differs.
import { createHash } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { mkdir, writeFile } from 'node:fs/promises';
import { dirname, isAbsolute, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';

import {
  installedPackageIdentity,
  observedBackendSelection,
  providerBackendContract,
} from '../node-webgpu/provider-contract.js';

export const REPO_ROOT = resolve(fileURLToPath(new URL('../../..', import.meta.url)));
const DOE_PACKAGE_PATH = join(REPO_ROOT, 'packages/doe-gpu/src/index.js');
const VENDORED_DAWN_PATH = join(REPO_ROOT, 'bench/vendor/node-webgpu-package/index.js');
export const PROVIDERS = Object.freeze(['doe', 'dawn']);
export const BACKENDS = Object.freeze(['metal', 'vulkan', 'd3d12']);
const SOFTWARE_ADAPTER = /swiftshader|llvmpipe|lavapipe|software|warp/i;
export const CAPABILITY_MODE_ENV = 'FAWN_SCOREBOARD_CAPABILITY_MODE';
export const CAPABILITY_SET_ENV = 'FAWN_SCOREBOARD_CAPABILITY_SET';
export const CAPABILITY_MODES = Object.freeze(['full', 'intersection']);

export function sha256(value) {
  return createHash('sha256').update(value).digest('hex');
}

export function sha256File(path) {
  return sha256(readFileSync(path));
}

export function percentile(values, fraction) {
  if (!values.length) throw new Error('percentile of an empty sample');
  const sorted = [...values].sort((a, b) => a - b);
  const position = (sorted.length - 1) * fraction;
  const lower = Math.floor(position);
  const upper = Math.ceil(position);
  return sorted[lower] + (sorted[upper] - sorted[lower]) * (position - lower);
}

export function parseExecutorArgs(usage) {
  const { values } = parseArgs({
    options: {
      provider: { type: 'string' },
      backend: { type: 'string' },
      scenario: { type: 'string' },
      'trace-meta': { type: 'string' },
      'trace-jsonl': { type: 'string' },
      workload: { type: 'string' },
    },
    allowPositionals: false,
  });
  for (const key of ['provider', 'backend', 'scenario', 'trace-meta', 'trace-jsonl', 'workload']) {
    if (!values[key]) throw new Error(`missing --${key}; usage: ${usage}`);
  }
  if (!PROVIDERS.includes(values.provider)) {
    throw new Error(`--provider must be one of ${PROVIDERS.join(', ')}`);
  }
  if (!BACKENDS.includes(values.backend)) {
    throw new Error(`--backend must be one of ${BACKENDS.join(', ')}`);
  }
  return {
    provider: values.provider,
    backend: values.backend,
    scenarioPath: resolve(values.scenario),
    traceMetaPath: resolve(values['trace-meta']),
    traceJsonlPath: resolve(values['trace-jsonl']),
    workloadId: values.workload,
  };
}

export function loadScenario(path, expectedKind, workloadId) {
  const scenario = JSON.parse(readFileSync(path, 'utf8'));
  if (scenario.kind !== expectedKind || scenario.schemaVersion !== 1) {
    throw new Error(`${path}: expected kind ${expectedKind} schemaVersion 1`);
  }
  const ids = Array.isArray(scenario.workloads) ? scenario.workloads.map((entry) => entry.id) : [];
  const workload = scenario.workloads?.find((entry) => entry.id === workloadId);
  if (!workload) {
    throw new Error(`${path} does not declare workload ${workloadId}; declared: ${ids.join(', ')}`);
  }
  return { scenario, workload, scenarioSha256: sha256File(path) };
}

export function repoPath(value) {
  return isAbsolute(value) ? value : resolve(REPO_ROOT, value);
}

// Loads exactly one provider module. Dawn comes from the pinned bench
// package (vendored copy first, then bench/node_modules/webgpu); Doe from the
// in-tree doe-gpu package. The requested backend is enforced on both.
export async function loadProvider(provider, backend) {
  const contract = providerBackendContract(backend, provider === 'dawn' ? 'node-webgpu' : 'doe');
  let module;
  let identity;
  if (provider === 'doe') {
    module = await import(pathToFileURL(DOE_PACKAGE_PATH).href);
    identity = typeof module.providerInfo === 'function' ? module.providerInfo() : null;
  } else {
    let moduleUrl = pathToFileURL(VENDORED_DAWN_PATH).href;
    let importFallback = false;
    if (existsSync(VENDORED_DAWN_PATH)) {
      module = await import(moduleUrl);
    } else {
      importFallback = true;
      module = await import('webgpu');
      moduleUrl = import.meta.resolve('webgpu');
    }
    identity = installedPackageIdentity(REPO_ROOT, {
      provider: 'dawn-node-webgpu', packageName: 'webgpu', moduleUrl, importFallback,
    });
  }
  if (typeof module.create !== 'function' || !module.globals) {
    throw new Error(`${provider} provider module exports no create()/globals`);
  }
  return { module, identity, contract };
}

// Capability mode, declared per backend in config/release-scoreboard.json and
// passed down by the runner. `full` (the default) gives the application every
// feature and limit the provider's adapter reports. `intersection` exposes only
// the capability set both providers' adapters support on this host, so an
// application that selects kernels from adapter features (Doppler) selects the
// same shaders on both. The set is the runner's probe of both providers; it is
// never computed by one provider alone.
export function capabilityPolicy(env = process.env) {
  const mode = env[CAPABILITY_MODE_ENV]?.trim() || 'full';
  if (!CAPABILITY_MODES.includes(mode)) {
    throw new Error(`${CAPABILITY_MODE_ENV} must be one of ${CAPABILITY_MODES.join(', ')}, not ${mode}`);
  }
  const raw = env[CAPABILITY_SET_ENV];
  if (mode === 'full') {
    if (raw) throw new Error(`${CAPABILITY_SET_ENV} is only valid in intersection mode`);
    return Object.freeze({ mode, features: null, limits: null });
  }
  const set = raw ? JSON.parse(raw) : null;
  const features = set?.features;
  const limits = set?.limits;
  if (!Array.isArray(features) || features.some((name) => typeof name !== 'string')
      || !limits || typeof limits !== 'object' || Array.isArray(limits)
      || Object.values(limits).some((value) => !Number.isFinite(value))) {
    throw new Error(`intersection mode needs ${CAPABILITY_SET_ENV} = {features: [...], limits: {...}}`);
  }
  return Object.freeze({
    mode, features: Object.freeze([...features].sort()), limits: Object.freeze({ ...limits }),
  });
}

// Every numeric limit a GPUSupportedLimits object reports, own or inherited.
export function limitsRecord(limits) {
  const names = new Set();
  for (let object = limits; object && object !== Object.prototype; object = Object.getPrototypeOf(object)) {
    for (const name of Object.getOwnPropertyNames(object)) names.add(name);
  }
  const record = {};
  for (const name of [...names].sort()) {
    if (name === 'constructor' || name === '__proto__') continue;
    let value;
    try {
      value = limits[name];
    } catch {
      continue;
    }
    if (typeof value === 'number' || typeof value === 'bigint') record[name] = Number(value);
  }
  return record;
}

// Alignment limits are better when smaller; every other limit when larger.
export function isAlignmentLimit(name) {
  return name.startsWith('min');
}

// What the receipt says about the capability mode: the exposed set, the
// adapter's own features, and what the application actually requested.
const capabilityRecord = {
  mode: null, exposedFeatures: null, exposedLimits: null,
  adapterFeatures: null, requestedFeatures: [], requestedLimits: [],
};

export function capabilityReceipt() {
  return JSON.parse(JSON.stringify(capabilityRecord));
}

function restrictAdapter(adapter, policy) {
  const missing = policy.features.filter((name) => !adapter.features.has(name));
  if (missing.length) {
    throw new Error(`capability set names features this adapter lacks: ${missing.join(', ')}`);
  }
  const actual = limitsRecord(adapter.limits);
  for (const [name, value] of Object.entries(policy.limits)) {
    if (!(name in actual)) throw new Error(`capability set names limit ${name} this adapter does not report`);
    const exceeds = isAlignmentLimit(name) ? value < actual[name] : value > actual[name];
    if (exceeds) throw new Error(`capability set limit ${name}=${value} exceeds this adapter's ${actual[name]}`);
  }
  // Own properties shadow the provider's prototype getters; the provider's
  // objects are otherwise untouched.
  Object.defineProperty(adapter, 'features', { value: new Set(policy.features), configurable: true });
  Object.defineProperty(adapter, 'limits', { value: Object.freeze({ ...policy.limits }), configurable: true });
}

function admitDeviceRequest(descriptor, policy) {
  const features = [...(descriptor?.requiredFeatures ?? [])].map(String);
  const limits = descriptor?.requiredLimits ?? {};
  capabilityRecord.requestedFeatures.push([...features].sort());
  capabilityRecord.requestedLimits.push(Object.fromEntries(
    Object.entries(limits).filter(([, value]) => value !== undefined).sort(([a], [b]) => a.localeCompare(b))));
  if (policy.mode !== 'intersection') return;
  const outside = features.filter((name) => !policy.features.includes(name));
  if (outside.length) {
    throw new Error(`device requested features outside the capability set: ${outside.join(', ')}`);
  }
  for (const [name, value] of Object.entries(limits)) {
    if (value === undefined) continue;
    if (!(name in policy.limits)) throw new Error(`device requested limit ${name} outside the capability set`);
    const exceeds = isAlignmentLimit(name) ? value < policy.limits[name] : value > policy.limits[name];
    if (exceeds) throw new Error(`device requested ${name}=${value} beyond the capability set's ${policy.limits[name]}`);
  }
}

// Installs the provider's WebGPU constants and interface objects (so engine
// `instanceof GPU...` checks work) and the observed GPU as navigator.gpu.
// Dawn exports its interfaces in `globals`; doe-gpu installs them through its
// public setupGlobals(), harvested here from a scratch target.
export function installGpuGlobals(module, gpu, createOptions = []) {
  const interfaces = {};
  if (typeof module.setupGlobals === 'function') {
    const scratch = {};
    module.setupGlobals(scratch, createOptions);
    for (const name of Object.getOwnPropertyNames(scratch)) {
      if (name.startsWith('GPU')) interfaces[name] = scratch[name];
    }
  }
  Object.assign(globalThis, interfaces, module.globals);
  Object.defineProperty(globalThis, 'navigator', {
    value: { gpu, userAgent: 'node' }, configurable: true, writable: true,
  });
}

// Observes the WebGPU API boundary that both providers implement. Every
// interval is attributed to the harness phase that is open when it starts.
// The observer only times calls; it never changes arguments or results.
export class ApiObserver {
  // countPassCommands wraps pass encoders to count dispatches and draws; the
  // draw-overhead row disables it because it measures that encoding path and
  // its command counts are known exactly from the workload.
  constructor({ countPassCommands = true } = {}) {
    this.countPassCommands = countPassCommands;
    this.phase = 'unattributed';
    this.phases = {};
    this.shaderSources = new Map();
    this.moduleSha = new WeakMap();
    this.pipelines = new Set();
    this.asyncPipelines = [];
    this.adapterInfo = null;
    this.backendSelection = null;
    this.device = null;
    this.devices = [];
  }

  enter(phase) {
    this.phase = phase;
  }

  bucket() {
    if (!this.phases[this.phase]) {
      this.phases[this.phase] = {
        shaderModuleCalls: 0, shaderModuleMs: 0,
        pipelineSyncCalls: 0, pipelineSyncMs: 0,
        pipelineAsyncCalls: 0, pipelineAsyncCallMs: 0, pipelineAsyncResolveMs: 0,
        writeBufferCalls: 0, writeBufferBytes: 0, writeBufferMs: 0,
        mappedAtCreationBuffers: 0, mappedAtCreationBytes: 0,
        submitCalls: 0, submitMs: 0,
        dispatchCommands: 0, drawCommands: 0,
      };
    }
    return this.phases[this.phase];
  }

  wrapGpu(gpu, contract, policy = capabilityPolicy()) {
    capabilityRecord.mode = policy.mode;
    capabilityRecord.exposedFeatures = policy.features;
    capabilityRecord.exposedLimits = policy.limits;
    const requestAdapter = gpu.requestAdapter.bind(gpu);
    gpu.requestAdapter = async (options = {}) => {
      const adapter = await requestAdapter({ ...options, ...contract.adapterOptions });
      if (!adapter) throw new Error('provider returned no adapter');
      capabilityRecord.adapterFeatures = [...adapter.features].sort();
      const info = adapter.info ?? {};
      this.backendSelection = observedBackendSelection(contract, info);
      this.adapterInfo = {
        vendor: info.vendor ?? '', architecture: info.architecture ?? '',
        device: info.device ?? '', description: info.description ?? '',
        backendType: Number.isInteger(info.backendType) ? info.backendType : null,
        isFallbackAdapter: info.isFallbackAdapter === true,
        features: [...adapter.features].sort(),
      };
      if (this.adapterInfo.isFallbackAdapter || SOFTWARE_ADAPTER.test(JSON.stringify(this.adapterInfo))) {
        throw new Error(`software or fallback adapter refused: ${JSON.stringify(this.adapterInfo)}`);
      }
      if (policy.mode === 'intersection') restrictAdapter(adapter, policy);
      const requestDevice = adapter.requestDevice.bind(adapter);
      adapter.requestDevice = async (descriptor) => {
        admitDeviceRequest(descriptor, policy);
        return this.wrapDevice(await requestDevice(descriptor));
      };
      return adapter;
    };
    return gpu;
  }

  wrapDevice(device) {
    // Applications may open a probe device first; every device is observed.
    this.devices.push(device);
    this.device = device;
    const timeSync = (name, record) => {
      const original = device[name].bind(device);
      device[name] = (descriptor, ...rest) => {
        const started = performance.now();
        const result = original(descriptor, ...rest);
        record(this.bucket(), performance.now() - started, descriptor);
        return result;
      };
    };
    const createShaderModule = device.createShaderModule.bind(device);
    device.createShaderModule = (descriptor) => {
      const started = performance.now();
      const module = createShaderModule(descriptor);
      const bucket = this.bucket();
      bucket.shaderModuleCalls += 1;
      bucket.shaderModuleMs += performance.now() - started;
      const code = String(descriptor?.code ?? '');
      const digest = sha256(code);
      this.shaderSources.set(digest, code.length);
      this.moduleSha.set(module, digest);
      return module;
    };
    for (const name of ['createComputePipeline', 'createRenderPipeline']) {
      timeSync(name, (bucket, ms, descriptor) => {
        bucket.pipelineSyncCalls += 1;
        bucket.pipelineSyncMs += ms;
        this.recordPipeline(name, descriptor);
      });
    }
    for (const name of ['createComputePipelineAsync', 'createRenderPipelineAsync']) {
      const original = device[name].bind(device);
      device[name] = (descriptor) => {
        const bucket = this.bucket();
        const started = performance.now();
        const promise = original(descriptor);
        const returned = performance.now();
        bucket.pipelineAsyncCalls += 1;
        bucket.pipelineAsyncCallMs += returned - started;
        this.recordPipeline(name, descriptor);
        return promise.then((pipeline) => {
          const resolved = performance.now();
          bucket.pipelineAsyncResolveMs += resolved - started;
          this.asyncPipelines.push({ callMs: returned - started, resolveMs: resolved - started });
          return pipeline;
        });
      };
    }
    timeSync('createBuffer', (bucket, _ms, descriptor) => {
      if (descriptor?.mappedAtCreation) {
        bucket.mappedAtCreationBuffers += 1;
        bucket.mappedAtCreationBytes += Number(descriptor.size ?? 0);
      }
    });
    if (this.countPassCommands) this.countCommands(device);
    const queue = device.queue;
    const writeBuffer = queue.writeBuffer.bind(queue);
    queue.writeBuffer = (buffer, offset, data, dataOffset, size) => {
      const started = performance.now();
      const result = writeBuffer(buffer, offset, data, dataOffset, size);
      const bucket = this.bucket();
      bucket.writeBufferCalls += 1;
      bucket.writeBufferMs += performance.now() - started;
      const elementBytes = data?.BYTES_PER_ELEMENT ?? 1;
      bucket.writeBufferBytes += size !== undefined
        ? size * elementBytes
        : (data?.byteLength ?? 0) - (dataOffset ?? 0) * elementBytes;
      return result;
    };
    const submit = queue.submit.bind(queue);
    queue.submit = (buffers) => {
      const started = performance.now();
      const result = submit(buffers);
      const bucket = this.bucket();
      bucket.submitCalls += 1;
      bucket.submitMs += performance.now() - started;
      return result;
    };
    return device;
  }

  countCommands(device) {
    const createCommandEncoder = device.createCommandEncoder.bind(device);
    const observer = this;
    const wrapPass = (pass, methods, field) => {
      for (const name of methods) {
        if (typeof pass[name] !== 'function') continue;
        const original = pass[name].bind(pass);
        pass[name] = (...values) => {
          observer.bucket()[field] += 1;
          return original(...values);
        };
      }
      return pass;
    };
    device.createCommandEncoder = (descriptor) => {
      const encoder = createCommandEncoder(descriptor);
      const beginCompute = encoder.beginComputePass.bind(encoder);
      encoder.beginComputePass = (passDescriptor) => wrapPass(
        beginCompute(passDescriptor), ['dispatchWorkgroups', 'dispatchWorkgroupsIndirect'], 'dispatchCommands');
      const beginRender = encoder.beginRenderPass.bind(encoder);
      encoder.beginRenderPass = (passDescriptor) => wrapPass(
        beginRender(passDescriptor), ['draw', 'drawIndexed', 'drawIndirect', 'drawIndexedIndirect'], 'drawCommands');
      return encoder;
    };
  }

  // Work issued inside the named phases: the comparator requires equal
  // dispatch/draw and submit counts on both providers.
  executionShape(phases) {
    const total = (field) => phases.reduce((sum, name) => sum + (this.phases[name]?.[field] ?? 0), 0);
    return {
      executionDispatchCount: total('dispatchCommands') + total('drawCommands'),
      executionSubmitCount: total('submitCalls'),
      executionShapePhases: phases,
      executionDispatchKinds: { dispatch: total('dispatchCommands'), draw: total('drawCommands') },
    };
  }

  recordPipeline(kind, descriptor) {
    const stage = (entry) => entry ? {
      module: this.moduleSha.get(entry.module) ?? null,
      entryPoint: entry.entryPoint ?? null,
      constants: entry.constants ?? null,
    } : null;
    this.pipelines.add(JSON.stringify({
      kind: kind.replace(/Async$/, ''),
      compute: stage(descriptor?.compute),
      vertex: stage(descriptor?.vertex),
      fragment: stage(descriptor?.fragment),
    }));
  }

  // Same WGSL bytes, entry points and override constants on both providers
  // make the same shaders; the digest is what the comparator checks.
  shaderReceipt() {
    const modules = [...this.shaderSources.keys()].sort();
    const pipelines = [...this.pipelines].sort();
    return {
      shaderModuleCount: modules.length,
      pipelineCount: pipelines.length,
      shaderSourceSha256s: modules,
      shaderSourceReceiptsHash: sha256(JSON.stringify({ modules, pipelines })),
    };
  }

  // The WebGPU API boundary separates WGSL module creation from pipeline
  // creation; translation and native compilation are not separable there.
  asyncPipelineSummary() {
    const calls = this.asyncPipelines;
    const callMs = calls.reduce((sum, entry) => sum + entry.callMs, 0);
    const resolveMs = calls.reduce((sum, entry) => sum + entry.resolveMs, 0);
    return {
      calls: calls.length,
      synchronousPortionMs: callMs,
      callToResolveMs: resolveMs,
      synchronousFraction: resolveMs > 0 ? callMs / resolveMs : null,
      createdBeforeReturn: calls.length > 0 && resolveMs > 0 && callMs / resolveMs >= 0.9,
    };
  }
}

async function writeJson(path, payload) {
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, `${JSON.stringify(payload, null, 2)}\n`, 'utf8');
}

export function executionBackendName(lane, provider) {
  return `release_scoreboard_${lane}_node_${provider}`;
}

export async function writeReceipt(args, fields) {
  const meta = {
    traceMetaVersion: 1,
    runtimeHost: 'node',
    benchmarkLane: 'release-scoreboard',
    workloadId: args.workloadId,
    executionProvider: args.provider === 'dawn' ? 'node-webgpu' : 'doe',
    executionProviderName: args.provider === 'dawn' ? 'webgpu' : 'doe-gpu',
    requestedBackend: args.backend,
    timingClass: 'operation',
    workloadUnitWallSource: 'trace-meta-process-wall',
    processWallMs: performance.now(),
    nodeVersion: process.version,
    capability: capabilityReceipt(),
    ...fields,
  };
  await writeJson(args.traceMetaPath, meta);
  await mkdir(dirname(args.traceJsonlPath), { recursive: true });
  const row = {
    traceFormat: 'release-scoreboard-v1',
    status: meta.executionErrorCount ? 'error' : 'success',
    executionBackend: meta.executionBackend,
    workloadId: meta.workloadId,
    timingMs: meta.timingMs ?? null,
    timingSource: meta.timingSource ?? null,
    resultSummary: meta.resultSummary ?? null,
    failureMessage: meta.failureMessage ?? null,
  };
  await writeFile(args.traceJsonlPath, `${JSON.stringify(row)}\n`, 'utf8');
}

// A failure keeps the receipt shape and exits nonzero, so the runner
// records a failed sample rather than a timing. Teardown runs after the
// receipt is written, outside every timed interval, and the process exits
// explicitly because some provider builds keep native handles alive.
const TEARDOWN_LIMIT_MS = 30000;

export async function runExecutor(usage, lane, body) {
  let args;
  try {
    args = parseExecutorArgs(usage);
  } catch (error) {
    process.stderr.write(`${error.message}\n`);
    process.exit(2);
  }
  const teardown = [];
  let exitCode = 0;
  try {
    const fields = await body(args, (callback) => teardown.push(callback));
    await writeReceipt(args, {
      executionBackend: executionBackendName(lane, args.provider),
      executionRowCount: 1,
      executionSuccessCount: 1,
      executionErrorCount: 0,
      ...fields,
    });
  } catch (error) {
    // An output mismatch keeps its observations so the rejection is auditable;
    // its timing is never selected because the sample is a failure.
    const observed = error?.fields ?? {};
    await writeReceipt(args, {
      ...observed,
      outputCheckFailed: error?.name === 'OutputMismatchError',
      executionBackend: executionBackendName(lane, args.provider),
      executionRowCount: 0,
      executionSuccessCount: 0,
      executionErrorCount: 1,
      terminalFailureCaptured: true,
      failureMessage: error instanceof Error ? error.message : String(error),
      failureDetail: error?.detail ?? null,
    });
    process.stderr.write(`${error instanceof Error ? (error.stack ?? error.message) : String(error)}\n`);
    exitCode = 1;
  }
  for (const callback of teardown.reverse()) {
    let timer;
    try {
      await Promise.race([
        Promise.resolve().then(callback),
        new Promise((_, reject) => {
          timer = setTimeout(() => reject(new Error('teardown exceeded its limit')), TEARDOWN_LIMIT_MS);
        }),
      ]);
    } catch (error) {
      process.stderr.write(`teardown (outside the receipt): ${error.message}\n`);
    } finally {
      clearTimeout(timer);
    }
  }
  process.exit(exitCode);
}

export class OutputMismatchError extends Error {
  constructor(message, detail) {
    super(message);
    this.name = 'OutputMismatchError';
    this.detail = detail;
  }
}
