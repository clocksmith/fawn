// Runtime-supplied GLSL mapping controls. Correctness only; no timing verdict.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { pathToFileURL } from 'node:url';

const [moduleArgument, libraryArgument, outputArgument] = process.argv.slice(2);
assert(moduleArgument && libraryArgument && outputArgument,
  'Usage: node run-glsl-instruction-controls.mjs <provider-module> <Doe-library-or-dawn> <output-directory>');
const digest = (bytes) => createHash('sha256').update(bytes).digest('hex');
const outputRoot = resolve(outputArgument);
await mkdir(outputRoot, { recursive: true });
const shader = await readFile(new URL('../../runtime/zig/tests/wgsl/glsl_instruction_runtime.wgsl', import.meta.url), 'utf8');
const isDoe = libraryArgument !== 'dawn';
if (isDoe) process.env.DOE_WEBGPU_LIB = resolve(libraryArgument);
const provider = await import(pathToFileURL(resolve(moduleArgument)).href);
Object.assign(globalThis, provider.globals);
if (isDoe) assert.equal(provider.providerInfo().doeNative, true);
const gpu = provider.create(['backend=vulkan']);
const adapter = await gpu.requestAdapter();
assert(adapter, 'Physical adapter required');
const identity = Object.fromEntries(['vendor', 'device', 'architecture', 'description'].map(
  (key) => [key, adapter.info?.[key] ?? null]));
assert(/amd|radeon/i.test(JSON.stringify(identity)), 'AMD Vulkan required');
const device = await adapter.requestDevice();
const results = [];
const cases = [
  { values: [1.5, -2, 0, 0.75], exponents: [3, -2, 4, 0], exact: [12, -0.5, 0, 0.75] },
  { values: [-1.5, 0, 2, -0.125], exponents: [3, -3, -4, 4], exact: [-12, 0, 0.125, -2] },
  { values: [0.5, -0.5, 1, -1], exponents: [0, 0, -16, 16], exact: [0.5, -0.5, 1 / 65536, -65536] },
];
try {
  const module = device.createShaderModule({ code: shader });
  const compilation = await module.getCompilationInfo();
  assert(!compilation.messages.some((message) => message.type === 'error'), JSON.stringify(compilation.messages));
  const pipeline = await device.createComputePipelineAsync({ layout: 'auto', compute: { module, entryPoint: 'main' } });
  for (const [index, test] of cases.entries()) {
    const bytes = new ArrayBuffer(32);
    new Float32Array(bytes, 0, 4).set(test.values);
    new Int32Array(bytes, 16, 4).set(test.exponents);
    const expected = new Float32Array([
      ...test.exact, ...test.exact.slice(0, 2), 0, 0,
      ...test.exact.slice(0, 3), 0, ...test.exact,
      test.exact[3], 1, 1, 0,
      ...test.values.map((value) => Math.min(1, Math.max(0, value))),
      ...test.values.map((value) => value * 180 / Math.PI),
      ...test.values.map((value) => value * Math.PI / 180),
    ]);
    const buffers = [];
    let observed = null, failure = null;
    try {
      const input = device.createBuffer({ size: bytes.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
      buffers.push(input);
      const output = device.createBuffer({ size: expected.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
      buffers.push(output);
      const readback = device.createBuffer({ size: expected.byteLength, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
      buffers.push(readback);
      device.queue.writeBuffer(input, 0, bytes);
      const bindings = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
        { binding: 0, resource: { buffer: input } }, { binding: 1, resource: { buffer: output } },
      ] });
      const encoder = device.createCommandEncoder();
      const pass = encoder.beginComputePass();
      pass.setPipeline(pipeline); pass.setBindGroup(0, bindings); pass.dispatchWorkgroups(1); pass.end();
      encoder.copyBufferToBuffer(output, 0, readback, 0, expected.byteLength);
      device.queue.submit([encoder.finish()]);
      await readback.mapAsync(GPUMapMode.READ);
      observed = new Float32Array(readback.getMappedRange().slice(0));
      readback.unmap();
      for (let word = 0; word < expected.length; word++) {
        if (word < 24) assert.equal(observed[word], expected[word], `case ${index}, exact word ${word}`);
        // Conversion constants may round independently; this bound is only for radians/degrees.
        else assert(Number.isFinite(observed[word]) && Math.abs(observed[word] - expected[word]) <= 2e-6 * Math.max(1, Math.abs(expected[word])),
          `case ${index}, angular word ${word}: ${observed[word]} != ${expected[word]}`);
      }
    } catch (error) {
      failure = String(error);
    } finally {
      await device.queue.onSubmittedWorkDone();
      for (const buffer of buffers) buffer.destroy();
    }
    await writeFile(join(outputRoot, `${index}-input.bin`), new Uint8Array(bytes));
    await writeFile(join(outputRoot, `${index}-expected.bin`), new Uint8Array(expected.buffer));
    if (observed) await writeFile(join(outputRoot, `${index}-observed.bin`), new Uint8Array(observed.buffer));
    results.push({ ...test, expected: [...expected], observed: observed ? [...observed] : null, failure, passed: failure === null });
  }
} catch (error) {
  results.push({ passed: false, failure: String(error) });
} finally {
  await device.queue.onSubmittedWorkDone();
  device.destroy();
}
const maps = await readFile('/proc/self/maps', 'utf8');
const loadedLibraries = [...new Set(maps.split('\n').map((line) => line.match(/\s(\/[^\n]+)$/)?.[1]).filter(
  (path) => path && /\.so|\.node/.test(path)))];
if (isDoe) assert(loadedLibraries.includes(resolve(libraryArgument)), 'Declared Doe library was not loaded');
const report = { schemaVersion: 1, performanceClaim: false, identity, shaderSha256: digest(shader),
  providerModuleSha256: digest(await readFile(resolve(moduleArgument))),
  librarySha256: isDoe ? digest(await readFile(resolve(libraryArgument))) : null,
  loadedLibraries, results, passed: results.every((result) => result.passed) };
await writeFile(join(outputRoot, 'report.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify({ passed: report.passed, cases: results.length, identity }));
if (!report.passed) process.exitCode = 1;
