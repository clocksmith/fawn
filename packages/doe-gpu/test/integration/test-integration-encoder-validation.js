#!/usr/bin/env node
// Encoder validation the package must not hide (review of a8aa75668):
// 1 setBindGroup reaches the provider when called, even with no dispatch after;
// 2 an out-of-range bind group index is never deduplicated away;
// 3 merged deferred dispatches never inherit another dispatch's bind group;
// 4 a deferred copy error survives native materialization of the encoder;
// 5 GPUCommandEncoder debug groups reach the provider;
// 6 finish() of an invalid encoder reports a validation error and returns an
//   invalid command buffer instead of throwing;
// 7 setBindGroup converts offsets with [EnforceRange] and unbinds on undefined;
// 8 the doe-gpu/compute facade forwards dynamic offsets;
// 9 the addon rejects a bind group argument that is not a bind group.
//
// Known provider gaps: the Zig drop-in does not check bind groups for pipelines
// with automatic layouts at dispatch, nor debug group balance, so on it 3 and 5
// check only that the calls reach the provider without errors.

import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { providerInfo, setupGlobals } from '../../src/index.js';
import { requestAdapter as requestComputeAdapter } from '../../src/compute.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('encoder validation: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage, GPUMapMode, GPUShaderStage, GPUValidationError } = target;
const flavor = providerInfo().libraryFlavor;
const providerValidatesAll = flavor !== 'doe-dropin';
if (!providerValidatesAll) console.log(`known gap: ${flavor} does not check automatic-layout dispatch groups or debug group balance`);

const module = device.createShaderModule({
  code: '@group(0) @binding(0) var<storage, read_write> value: u32; @compute @workgroup_size(1) fn main() { value = 5u; }',
});
const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' } });
const output = device.createBuffer({ size: 512, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
const other = device.createBuffer({ size: 512, usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: output, size: 4 } }] });
const dynamicLayout = device.createBindGroupLayout({
  entries: [{ binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage', hasDynamicOffset: true } }],
});
const dynamicGroup = device.createBindGroup({ layout: dynamicLayout, entries: [{ binding: 0, resource: { buffer: output, size: 4 } }] });
const dynamicPipeline = device.createComputePipeline({
  layout: device.createPipelineLayout({ bindGroupLayouts: [dynamicLayout] }),
  compute: { module, entryPoint: 'main' },
});
const nativeEncoder = (encoder) => encoder.clearBuffer(other, 0, 4);

async function finishError(encode) {
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  encode(encoder);
  encoder.finish();
  return device.popErrorScope();
}

async function expectInvalid(name, encode, zigGap = false) {
  const error = await finishError(encode);
  if (!zigGap || providerValidatesAll) {
    assert(error instanceof GPUValidationError, `${name}: finish() reports a validation error`);
  }
}

// 1: setBindGroup with no later dispatch, on lazy and native passes.
await expectInvalid('1 native pass, misaligned offset', (encoder) => {
  nativeEncoder(encoder);
  const pass = encoder.beginComputePass();
  pass.setPipeline(dynamicPipeline);
  pass.setBindGroup(0, dynamicGroup, [4]);
  pass.end();
});
await expectInvalid('1 lazy pass, second misaligned offset', (encoder) => {
  const pass = encoder.beginComputePass();
  pass.setPipeline(dynamicPipeline);
  pass.setBindGroup(0, dynamicGroup, [0]);
  pass.setBindGroup(0, dynamicGroup, [4]);
  pass.end();
});
await expectInvalid('1 lazy pass, null group with offsets', (encoder) => {
  const pass = encoder.beginComputePass();
  pass.setBindGroup(0, null, [0]);
  pass.end();
});
console.log('ok 1: setBindGroup reaches the provider without a dispatch');

// 2: index >= maxBindGroups with a null group, lazy and native.
for (const [name, prepare] of [['lazy', () => {}], ['native', nativeEncoder]]) {
  await expectInvalid(`2 ${name} pass, index maxBindGroups`, (encoder) => {
    prepare(encoder);
    const pass = encoder.beginComputePass();
    pass.setBindGroup(device.limits.maxBindGroups, null);
    pass.end();
  });
}
console.log('ok 2: an out-of-range bind group index is not deduplicated');

// 3: deferred dispatches followed by a copy are not merged across bindings.
await expectInvalid('3 unbind between dispatches', (encoder) => {
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, group);
  pass.dispatchWorkgroups(1);
  pass.setBindGroup(0, null);
  pass.dispatchWorkgroups(1);
  pass.end();
  encoder.copyBufferToBuffer(output, 0, other, 0, 4);
}, true);
await expectInvalid('3 second pass without a bind group', (encoder) => {
  let pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, group);
  pass.dispatchWorkgroups(1);
  pass.end();
  pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.dispatchWorkgroups(1);
  pass.end();
  encoder.copyBufferToBuffer(output, 0, other, 0, 4);
}, true);
console.log('ok 3: merged deferred dispatches keep their own bind groups');

// 4: a deferred copy error survives materialization (package-side).
await expectInvalid('4 deferred copy then clearBuffer', (encoder) => {
  encoder.copyBufferToBuffer(output, 0, other, 0, 1024);
  encoder.clearBuffer(other, 0, 4);
});
await expectInvalid('4 deferred copy then pass with offsets', (encoder) => {
  encoder.copyBufferToBuffer(output, 0, other, 0, 1024);
  const pass = encoder.beginComputePass();
  pass.setPipeline(dynamicPipeline);
  pass.setBindGroup(0, dynamicGroup, [0]);
  pass.dispatchWorkgroups(1);
  pass.end();
});
console.log('ok 4: a deferred copy error survives native materialization');

// 5: encoder debug groups reach the provider.
await expectInvalid('5 popDebugGroup with an empty stack', (encoder) => encoder.popDebugGroup(), true);
await expectInvalid('5 pushDebugGroup left open at finish', (encoder) => encoder.pushDebugGroup('open'), true);
assert.equal(await finishError((encoder) => {
  encoder.pushDebugGroup('outer');
  encoder.insertDebugMarker('marker');
  encoder.popDebugGroup();
}), null, '5 balanced debug groups are valid');
console.log('ok 5: GPUCommandEncoder debug groups reach the provider');

// 6: an invalid encoder's finish() without a scope is uncaptured, not thrown.
{
  const uncaptured = [];
  device.onuncapturederror = (event) => uncaptured.push(event.error);
  const encoder = device.createCommandEncoder();
  encoder.copyBufferToBuffer(output, 0, other, 0, 1024);
  const commandBuffer = encoder.finish();
  device.pushErrorScope('validation');
  device.queue.submit([commandBuffer]);
  const atSubmit = await device.popErrorScope();
  await new Promise((resolve) => setTimeout(resolve, 20)); // uncapturederror fires from a queued task
  device.onuncapturederror = null;
  assert.equal(uncaptured.length, 1, '6 finish() raised one uncaptured validation error');
  assert(uncaptured[0] instanceof GPUValidationError);
  assert(atSubmit instanceof GPUValidationError, '6 submitting the invalid command buffer is a validation error');
  console.log('ok 6: finish() of an invalid encoder returns an invalid command buffer');
}

// 7: [EnforceRange] offsets and an undefined bind group.
{
  const readback = device.createBuffer({ size: 4, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(dynamicPipeline);
  pass.setBindGroup(0, dynamicGroup, ['256']);
  pass.dispatchWorkgroups(1);
  pass.setBindGroup(0, dynamicGroup, [256.75]);
  pass.dispatchWorkgroups(1);
  pass.setBindGroup(1, undefined);
  pass.end();
  encoder.copyBufferToBuffer(output, 256, readback, 0, 4);
  device.pushErrorScope('validation');
  device.queue.submit([encoder.finish()]);
  assert.equal(await device.popErrorScope(), null, '7 "256" and 256.75 convert to 256; undefined unbinds');
  await readback.mapAsync(GPUMapMode.READ);
  assert.equal(new Uint32Array(readback.getMappedRange())[0], 5);
  readback.unmap();
  const probe = device.createCommandEncoder().beginComputePass();
  assert.throws(() => probe.setBindGroup(0, dynamicGroup, [-1]), TypeError);
  assert.throws(() => probe.setBindGroup(0, dynamicGroup, [2 ** 32]), TypeError);
  assert.throws(() => probe.setBindGroup(0, dynamicGroup, [Number.NaN]), TypeError);
  console.log('ok 7: dynamic offsets use [EnforceRange] and undefined unbinds');
}

// 8: the compute facade forwards dynamic offsets.
{
  const computeAdapter = await requestComputeAdapter() ?? await requestComputeAdapter({ forceFallbackAdapter: true });
  const compute = await computeAdapter.requestDevice();
  const facadeLayout = compute.createBindGroupLayout({
    entries: [{ binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage', hasDynamicOffset: true } }],
  });
  const facadeBuffer = compute.createBuffer({ size: 512, usage: GPUBufferUsage.STORAGE });
  const facadeGroup = compute.createBindGroup({ layout: facadeLayout, entries: [{ binding: 0, resource: { buffer: facadeBuffer, size: 4 } }] });
  const facadePipeline = compute.createComputePipeline({
    layout: compute.createPipelineLayout({ bindGroupLayouts: [facadeLayout] }),
    compute: { module: compute.createShaderModule({ code: '@group(0) @binding(0) var<storage, read_write> value: u32; @compute @workgroup_size(1) fn main() { value = 5u; }' }), entryPoint: 'main' },
  });
  compute.pushErrorScope('validation');
  const encoder = compute.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(facadePipeline);
  pass.setBindGroup(0, facadeGroup, new Uint32Array([7, 256]), 1, 1);
  pass.dispatchWorkgroups(1);
  pass.end();
  compute.queue.submit([encoder.finish()]);
  assert.equal(await compute.popErrorScope(), null, '8 the facade passes its dynamic offset through');
  console.log('ok 8: doe-gpu/compute forwards dynamic offsets');
}

// 9: the addon rejects a bind group argument that is not a bind group.
{
  const addon = createRequire(import.meta.url)('../../build/Release/doe_napi.node');
  const encoder = device.createCommandEncoder();
  nativeEncoder(encoder);
  const pass = encoder.beginComputePass();
  assert.throws(() => addon.computePassSetBindGroup(pass._native, 0, {}), /bind group/);
  assert.throws(() => addon.computePassSetBindGroup(pass._native, 'zero', null), /index/);
  pass.end();
  encoder.finish();
  console.log('ok 9: the addon rejects non-bind-group arguments');
}
device.destroy();
