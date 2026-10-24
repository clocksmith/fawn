#!/usr/bin/env node
// Public JavaScript override specialization through one explicit C provider.
// Execution scope follows the observed adapter; this is no performance qualification.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, realpathSync } from 'node:fs';
import { isAbsolute } from 'node:path';

const WORD_BYTES = Uint32Array.BYTES_PER_ELEMENT;
const OUTPUT_WORDS = 8;
const OUTPUT_BYTES = OUTPUT_WORDS * WORD_BYTES;
const READBACK_WORDS = 18;
const READBACK_BYTES = READBACK_WORDS * WORD_BYTES;
const SHA256_PATTERN = /^[a-f0-9]{64}$/;
const CASES = Object.freeze([
  Object.freeze({ width: 3, mul: -5, bias: 1.5, enabled: 0 }),
  Object.freeze({ width: 7, mul: 9, bias: -2.25, enabled: 1 }),
]);
// Independent literal oracle: prefix/suffix, typed values, workgroup memory and padding.
const EXPECTED = Object.freeze([
  0,
  3, 0xfffffffb, 0x3fc00000, 0, 3, 4, 0xfffffffe, 0,
  7, 9, 0xc0100000, 1, 7, 8, 1, 0,
  0,
]);
const CODE = `
@id(17) override width: u32;
override mul: i32 = 3;
override bias: f32 = 0.5;
override enabled: bool = true;
override reciprocal: i32 = 12 / mul;
override unrelated: u32;
var<workgroup> cache: array<u32, width>;
@group(0) @binding(0) var<storage, read_write> result: array<u32>;
@compute @workgroup_size(width)
fn selected(@builtin(local_invocation_index) lane: u32) {
  cache[lane] = lane + 1u;
  workgroupBarrier();
  if (lane == 0u) {
    result[0] = width;
    result[1] = bitcast<u32>(mul);
    result[2] = bitcast<u32>(bias);
    if (enabled) { result[3] = 1u; } else { result[3] = 0u; }
    result[4] = cache[width - 1u];
    result[5] = cache[0] + cache[width - 1u];
    result[6] = bitcast<u32>(reciprocal);
  }
}
@compute @workgroup_size(unrelated) fn sibling() {}
`;

function sha256(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

const adapterClass = process.env.DOE_C_TEST_ADAPTER;
assert(['software', 'physical'].includes(adapterClass),
  'Set DOE_C_TEST_ADAPTER explicitly to software or physical');
assert(process.env.DOE_WEBGPU_LIB && isAbsolute(process.env.DOE_WEBGPU_LIB),
  'Set DOE_WEBGPU_LIB to the exact absolute C provider artifact');
const providerPath = realpathSync(process.env.DOE_WEBGPU_LIB);
const expectedProviderSha256 = process.env.DOE_C_TEST_PROVIDER_SHA256;
assert(SHA256_PATTERN.test(expectedProviderSha256 ?? ''),
  'Set DOE_C_TEST_PROVIDER_SHA256 to the selected artifact SHA256 in lowercase');
assert.equal(sha256(readFileSync(providerPath)), expectedProviderSha256);

const { requestAdapter, providerInfo, globals } = await import('doe-gpu/native');
function selectedProvider() {
  const info = providerInfo();
  assert.equal(info.loaded, true);
  assert.equal(info.doeNative, true);
  assert.equal(info.libraryFlavor, 'doe-c');
  assert.equal(realpathSync(info.doeLibraryPath), providerPath);
  return info;
}

let adapter;
let device;
let shader;
let readback;
let isMapped = false;
let observedAdapter;
let observedProvider;
let actual;
const buffers = [];
const errors = [];
try {
  adapter = await requestAdapter({ forceFallbackAdapter: adapterClass === 'software' });
  assert(adapter, 'Explicit C provider returned no adapter');
  observedProvider = selectedProvider();
  observedAdapter = { ...adapter.info };
  assert.equal(observedAdapter.isFallbackAdapter, adapterClass === 'software',
    'Observed adapter class must match the explicit selection');
  device = await adapter.requestDevice();
  device.addEventListener('uncapturederror', (event) => errors.push(event.error.message));
  device.pushErrorScope('validation');
  const sourceDescriptor = { code: CODE };
  shader = device.createShaderModule(sourceDescriptor);
  assert.deepEqual((await shader.getCompilationInfo()).messages, []);
  sourceDescriptor.code = 'invalid replacement; the module must own its original source';
  const pipelines = CASES.map((values) => {
    const constants = {
      '17': values.width,
      mul: values.mul,
      bias: values.bias,
      enabled: values.enabled,
    };
    const pipeline = device.createComputePipeline({
      layout: 'auto',
      compute: { module: shader, entryPoint: 'selected', constants },
    });
    constants['17'] = 31;
    constants.mul = 87;
    constants.bias = 99;
    constants.enabled = 0;
    return pipeline;
  });
  shader.destroy();
  shader = null;
  readback = device.createBuffer({
    size: READBACK_BYTES,
    usage: globals.GPUBufferUsage.COPY_DST | globals.GPUBufferUsage.MAP_READ,
  });
  buffers.push(readback);
  const encoder = device.createCommandEncoder();
  for (const [index, pipeline] of pipelines.entries()) {
    const output = device.createBuffer({
      size: OUTPUT_BYTES,
      usage: globals.GPUBufferUsage.STORAGE | globals.GPUBufferUsage.COPY_SRC,
    });
    buffers.push(output);
    const bindings = device.createBindGroup({
      layout: pipeline.getBindGroupLayout(0),
      entries: [{ binding: 0, resource: { buffer: output, size: OUTPUT_BYTES } }],
    });
    const pass = encoder.beginComputePass();
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, bindings);
    pass.dispatchWorkgroups(1);
    pass.end();
    encoder.copyBufferToBuffer(output, 0, readback, WORD_BYTES + index * OUTPUT_BYTES,
      OUTPUT_BYTES);
  }
  device.queue.submit([encoder.finish()]);
  await readback.mapAsync(globals.GPUMapMode.READ);
  isMapped = true;
  actual = Array.from(new Uint32Array(readback.getMappedRange().slice(0)));
  assert.deepEqual(actual, EXPECTED);
  readback.unmap();
  isMapped = false;
  assert.equal(await device.popErrorScope(), null);
  assert.deepEqual(errors, []);
  selectedProvider();
} finally {
  if (isMapped) readback.unmap();
  for (const buffer of buffers) buffer.destroy();
  shader?.destroy();
  // Pipeline/group/layout wrappers keep their ordinary public garbage-collected lifetime.
  device?.destroy();
  adapter?.destroy();
}
assert.deepEqual(errors, []);
assert.equal(sha256(readFileSync(providerPath)), expectedProviderSha256,
  'Selected C provider artifact changed during execution');
console.log(JSON.stringify({
  kind: 'doe-gpu.c-provider-overrides.developer',
  schemaVersion: 1,
  scope: adapterClass === 'software'
    ? 'Public Node JavaScript through selected C provider on observed CPU fallback adapter'
    : 'Public Node JavaScript through selected C provider on observed nonfallback adapter',
  qualification: 'Focused override correctness only; no performance or broader backend qualification',
  node: process.version,
  platform: process.platform,
  architecture: process.arch,
  providerArtifact: { path: providerPath, sha256: expectedProviderSha256 },
  provider: observedProvider,
  adapter: observedAdapter,
  backendObservation: 'Numeric backend enum is not exposed by this public JavaScript boundary',
  workload: { shaderSha256: sha256(CODE), entryPoint: 'selected', constants: CASES },
  result: { expected: EXPECTED, actual, exactWords: READBACK_WORDS },
  cleanup: {
    explicit: 'Readback unmapped; buffers/shader/device/adapter destroyed through public APIs',
    otherHandles: 'Pipeline/group/layout wrappers follow ordinary public garbage collection',
  },
}, null, 2));
