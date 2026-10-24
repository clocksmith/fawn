#!/usr/bin/env node
// A destroyed buffer (WebGPU): createBindGroup naming it, directly or as a
// GPUBufferBinding, is valid; a compute pass that uses the group still
// encodes and finishes, and queue.submit reports the validation error. A
// buffer destroyed after the group was made fails the same way.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

setupGlobals();
const adapter = await navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('destroyed buffer bind group: skipped (no adapter)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUShaderStage: S } = globalThis;
const module = device.createShaderModule({ code: `
  @group(0) @binding(0) var<storage, read_write> out: array<u32>;
  @compute @workgroup_size(1) fn main() { out[0] = 1u; }` });
const layout = device.createBindGroupLayout({ entries: [{ binding: 0, visibility: S.COMPUTE, buffer: { type: 'storage' } }] });
const pipeline = device.createComputePipeline({ layout: device.createPipelineLayout({ bindGroupLayouts: [layout] }), compute: { module, entryPoint: 'main' } });

async function validationError(action) {
  device.pushErrorScope('validation');
  const value = action();
  return { value, error: await device.popErrorScope() };
}
function dispatch(group) {
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, group);
  pass.dispatchWorkgroups(1);
  pass.end();
  return encoder.finish();
}

for (const direct of [false, true]) {
  const name = direct ? 'GPUBuffer' : 'GPUBufferBinding';
  const resource = (buffer) => (direct ? buffer : { buffer });

  // Control: a live buffer submits cleanly.
  const live = device.createBuffer({ size: 16, usage: B.STORAGE });
  const control = await validationError(() => device.createBindGroup({ layout, entries: [{ binding: 0, resource: resource(live) }] }));
  assert.equal(control.error, null, `${name}: live bind group`);
  assert.equal((await validationError(() => device.queue.submit([dispatch(control.value)]))).error, null, `${name}: live submit`);

  // Destroyed before createBindGroup: creation and encoding succeed, submit fails.
  const before = device.createBuffer({ size: 16, usage: B.STORAGE });
  before.destroy();
  const created = await validationError(() => device.createBindGroup({ layout, entries: [{ binding: 0, resource: resource(before) }] }));
  assert.equal(created.error, null, `${name}: createBindGroup with a destroyed buffer`);
  const encoded = await validationError(() => dispatch(created.value));
  assert.equal(encoded.error, null, `${name}: encoding with a destroyed buffer`);
  assert.notEqual((await validationError(() => device.queue.submit([encoded.value]))).error, null, `${name}: submit with a destroyed buffer`);

  // Destroyed after createBindGroup: the submit fails.
  const after = device.createBuffer({ size: 16, usage: B.STORAGE });
  const group = device.createBindGroup({ layout, entries: [{ binding: 0, resource: resource(after) }] });
  after.destroy();
  assert.notEqual((await validationError(() => device.queue.submit([dispatch(group)]))).error, null, `${name}: submit after destroy`);
}
console.log('ok: a destroyed buffer binds without error and fails the submit that uses it');
device.destroy();
