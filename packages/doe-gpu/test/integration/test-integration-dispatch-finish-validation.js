#!/usr/bin/env node
// A dispatch-only command encoder is recorded natively by finish(), so the
// provider's validation result surfaces at finish() as WebGPU requires, and
// submitting the invalid command buffer is a second validation error.
//
// Known provider gap: the Zig drop-in records these dispatches without
// validating workgroup counts or bind group compatibility, so the package keeps
// deferring them to its submit-time flush there (index.js
// deferNodeDispatchesToSubmit). On that provider this test checks that a valid
// deferred dispatch still runs.

import assert from 'node:assert/strict';
import { providerInfo, setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
const adapter = await target.navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('dispatch finish validation: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage, GPUMapMode, GPUValidationError } = target;
const flavor = providerInfo().libraryFlavor;
const validatesDispatch = flavor !== 'doe-dropin';
if (!validatesDispatch) console.log(`known gap: ${flavor} does not validate dispatches; checking the deferred dispatch runs`);

const module = device.createShaderModule({
  code: '@group(0) @binding(0) var<storage, read_write> value: u32; @compute @workgroup_size(1) fn main() { value = 5u; }',
});
const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' } });
const output = device.createBuffer({ size: 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
const group = device.createBindGroup({
  layout: pipeline.getBindGroupLayout(0),
  entries: [{ binding: 0, resource: { buffer: output } }],
});

async function dispatchOnly(workgroups) {
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, group);
  pass.dispatchWorkgroups(workgroups);
  pass.end();
  const commandBuffer = encoder.finish();
  const atFinish = await device.popErrorScope();
  if (validatesDispatch) assert.notEqual(commandBuffer._native, null, 'finish() recorded the dispatch natively');
  device.pushErrorScope('validation');
  device.queue.submit([commandBuffer]);
  const atSubmit = await device.popErrorScope();
  return { atFinish, atSubmit };
}

const valid = await dispatchOnly(1);
assert.deepEqual(valid, { atFinish: null, atSubmit: null }, 'a valid dispatch-only encoder finishes and submits cleanly');
const readback = device.createBuffer({ size: 4, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
const copy = device.createCommandEncoder();
copy.copyBufferToBuffer(output, 0, readback, 0, 4);
device.queue.submit([copy.finish()]);
await readback.mapAsync(GPUMapMode.READ);
assert.equal(new Uint32Array(readback.getMappedRange())[0], 5, 'the finished dispatch ran');
readback.unmap();

const invalid = await dispatchOnly(device.limits.maxComputeWorkgroupsPerDimension + 1);
if (validatesDispatch) {
  assert(invalid.atFinish instanceof GPUValidationError, 'an over-limit dispatch is a validation error at finish()');
  assert(invalid.atSubmit instanceof GPUValidationError, 'submitting the invalid command buffer is a validation error');
}
console.log(`ok: dispatch-only encoders ${validatesDispatch ? 'are recorded natively at finish and report their validation result there' : 'still run when deferred to submit'}`);
device.destroy();
