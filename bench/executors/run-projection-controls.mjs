// Native execution of a frozen, independently referenced projection fixture.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { performance } from 'node:perf_hooks';

const [fixtureArgument, moduleArgument, libraryArgument, outputArgument, vendorArgument] = process.argv.slice(2);
assert(fixtureArgument && moduleArgument && libraryArgument && outputArgument
  && ['intel', 'amd'].includes(vendorArgument),
  'Usage: run-projection-controls.mjs <fixture> <provider-module> <library-or-dawn> <unused-output> <intel-or-amd>');
const fixture = resolve(fixtureArgument), output = resolve(outputArgument);
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const manifestBytes = await readFile(join(fixture, 'manifest.json'));
const manifest = JSON.parse(manifestBytes);
assert.equal(manifest.schema, 'doppler.w8a8-projection-fixture/v1');
assert.equal(manifest.acceptance.exactAllOutputBytes, true);
assert.equal(manifest.layout.byteOrder, 'little-endian');
const payloads = new Map();
for (const [name, record] of Object.entries(manifest.files)) {
  assert.equal(resolve(fixture, name), join(fixture, name), 'Payload must stay within the fixture');
  assert(!name.includes('/') && !name.includes('\\'), 'Payload filename must be local');
  const bytes = await readFile(join(fixture, name));
  assert.equal(bytes.length, record.sizeBytes, name);
  assert.equal(digest(bytes), record.sha256, name);
  payloads.set(name, bytes);
}
await mkdir(output, { recursive: false });
const isDoe = libraryArgument !== 'dawn';
if (isDoe) process.env.DOE_WEBGPU_LIB = resolve(libraryArgument);
const provider = await import(pathToFileURL(resolve(moduleArgument)).href);
Object.assign(globalThis, provider.globals);
const gpu = provider.create(['backend=vulkan']);
const adapter = await gpu.requestAdapter();
assert(adapter, 'The explicit native Vulkan provider must expose an adapter');
const identity = Object.fromEntries(['vendor', 'device', 'architecture', 'description']
  .map(key => [key, adapter.info?.[key] ?? null]));
assert(new RegExp(vendorArgument === 'amd' ? 'amd|radeon' : 'intel', 'i').test(JSON.stringify(identity)),
  'Provider must select the declared physical vendor');
assert(!/swiftshader|llvmpipe|software/i.test(JSON.stringify(identity)), 'Software fallback is forbidden');
const providerInfo = isDoe ? provider.providerInfo() : null;
if (isDoe) assert.equal(providerInfo.doeNative, true, 'Doe must be loaded explicitly');
const library = isDoe ? resolve(libraryArgument) : resolve(moduleArgument, '../dist/linux-x64.dawn.node');
const mapped = await readFile('/proc/self/maps', 'utf8');
assert(mapped.includes(library), 'Actual native library must be observed in process maps');
const report = { schemaVersion: 1, classification: 'frozen-projection-correctness',
  provider: isDoe ? 'doe' : 'dawn', compiler: isDoe ? 'doe' : 'tint',
  observedAt: new Date().toISOString(), fixtureManifestSha256: digest(manifestBytes),
  runnerSha256: digest(await readFile(fileURLToPath(import.meta.url))),
  providerModuleSha256: digest(await readFile(resolve(moduleArgument))),
  nativeLibrary: { path: library, sha256: digest(await readFile(library)), loaded: true },
  identity, providerInfo, shape: manifest.shape,
  languageFeatures: [...gpu.wgslLanguageFeatures ?? []], sequences: [], errors: [],
  passed: false, failure: null };
let device;
try {
  device = await adapter.requestDevice();
  device.addEventListener('uncapturederror', event => report.errors.push(event.error.message));
  for (const [name, stageIds] of Object.entries(manifest.sequences)) {
    const buffers = new Map(), owned = [], modules = [], pipelines = new Map(), groups = new Map();
    const stages = stageIds.map(id => {
      const stage = manifest.stages.find(row => row.id === id);
      assert(stage, `Unknown stage ${id}`); return stage;
    });
    const row = { name, preparationMs: null, executionMs: null, stages: [], outputs: [],
      passed: false, failure: null, cleanup: { settled: false, destroyedBuffers: 0 } };
    try {
      const prepareStart = performance.now();
      for (const [id, record] of Object.entries(manifest.buffers)) {
        const usage = record.usage.reduce((flags, key) => {
          assert(Number.isInteger(GPUBufferUsage[key]), `Unknown usage ${key}`);
          return flags | GPUBufferUsage[key];
        }, 0);
        assert(Number.isSafeInteger(record.size) && record.size > 0 && record.size % 4 === 0);
        const buffer = device.createBuffer({ size: record.size, usage, label: id });
        owned.push(buffer); buffers.set(id, buffer);
        if (record.initial !== 'zero') {
          const bytes = payloads.get(record.initial);
          assert(bytes && bytes.length === record.size, `Missing or invalid initial data for ${id}`);
          device.queue.writeBuffer(buffer, 0, bytes);
        }
      }
      for (const stage of stages) {
        const bytes = payloads.get(stage.shader); assert(bytes, stage.shader);
        const begin = performance.now();
        const module = device.createShaderModule({ code: bytes.toString('utf8'), label: stage.id });
        modules.push(module);
        // WebGPU's constants map contains numbers; boolean WGSL overrides use 0/1.
        const constants = Object.fromEntries(Object.entries(stage.constants)
          .map(([key, value]) => [key, typeof value === 'boolean' ? Number(value) : value]));
        const pipeline = await device.createComputePipelineAsync({ layout: 'auto',
          compute: { module, entryPoint: stage.entryPoint, constants } });
        const compileAndPrepareMs = performance.now() - begin;
        const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(stage.group),
          entries: stage.bindings.map(binding => ({ binding: binding.binding,
            resource: { buffer: buffers.get(binding.buffer), offset: binding.offset } })) });
        pipelines.set(stage.id, pipeline); groups.set(stage.id, group);
        row.stages.push({ id: stage.id, shaderSha256: digest(bytes), entryPoint: stage.entryPoint,
          constants: stage.constants, dispatch: stage.dispatch, compileAndPrepareMs });
      }
      const readbacks = new Map();
      for (const [id, record] of Object.entries(manifest.buffers)) {
        if (!record.expected && record.expectedU32 === undefined) continue;
        const buffer = device.createBuffer({ size: record.size,
          usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        owned.push(buffer); readbacks.set(id, buffer);
      }
      row.preparationMs = performance.now() - prepareStart;
      const start = performance.now();
      const encoder = device.createCommandEncoder();
      const pass = encoder.beginComputePass();
      for (const stage of stages) {
        pass.setPipeline(pipelines.get(stage.id)); pass.setBindGroup(stage.group, groups.get(stage.id));
        pass.dispatchWorkgroups(...stage.dispatch);
      }
      pass.end();
      for (const [id, buffer] of readbacks) encoder.copyBufferToBuffer(buffers.get(id), 0, buffer, 0, manifest.buffers[id].size);
      device.queue.submit([encoder.finish()]);
      await device.queue.onSubmittedWorkDone();
      for (const [id, buffer] of readbacks) {
        await buffer.mapAsync(GPUMapMode.READ);
        const bytes = Buffer.from(new Uint8Array(buffer.getMappedRange()));
        const record = manifest.buffers[id];
        const expected = record.expected ? payloads.get(record.expected) : Buffer.alloc(record.size);
        if (record.expectedU32 !== undefined) expected.writeUInt32LE(record.expectedU32);
        await writeFile(join(output, `${name}-${id}.bin`), bytes);
        row.outputs.push({ buffer: id, sizeBytes: bytes.length, expectedSha256: digest(expected),
          observedSha256: digest(bytes), exact: bytes.equals(expected) });
        buffer.unmap();
      }
      row.executionMs = performance.now() - start;
      assert(row.outputs.length > 0 && row.outputs.every(result => result.exact), 'Every declared output must match exact independent bytes');
      assert.deepEqual(report.errors, []);
      row.passed = true;
    } catch (error) { row.failure = { message: error.message, stack: error.stack }; }
    finally {
      report.sequences.push(row);
      await device.queue.onSubmittedWorkDone(); row.cleanup.settled = true;
      for (const buffer of owned) { buffer.destroy(); row.cleanup.destroyedBuffers++; }
      for (const pipeline of pipelines.values()) pipeline.destroy?.();
      for (const module of modules) module.destroy?.();
    }
  }
  report.passed = report.sequences.length === Object.keys(manifest.sequences).length
    && report.sequences.every(row => row.passed) && report.errors.length === 0;
} catch (error) { report.failure = { message: error.message, stack: error.stack }; }
finally {
  if (device) { await device.queue.onSubmittedWorkDone(); device.destroy(); }
  await writeFile(join(output, 'results.json'), JSON.stringify(report, null, 2) + '\n');
}
assert(report.passed, 'Frozen projection correctness failed; inspect retained results');
console.log(`Frozen portable and packed projection exact bytes passed on ${report.provider} ${vendorArgument}/Vulkan`);
