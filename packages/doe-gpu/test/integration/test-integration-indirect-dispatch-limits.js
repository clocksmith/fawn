#!/usr/bin/env node
// dispatchWorkgroupsIndirect arguments are GPU data: a workgroup count above
// maxComputeWorkgroupsPerDimension makes the dispatch do nothing
// (https://gpuweb.github.io/gpuweb/#dom-gpucomputepassencoder-dispatchworkgroupsindirect),
// while counts within the limit run, including after a GPU write of the args.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('indirect dispatch limits: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage, GPUMapMode } = target;
const limit = device.limits.maxComputeWorkgroupsPerDimension;

const counter = device.createComputePipeline({
  layout: 'auto',
  compute: {
    module: device.createShaderModule({
      code: '@group(0) @binding(0) var<storage, read_write> hits: atomic<u32>; @compute @workgroup_size(1) fn main() { atomicAdd(&hits, 1u); }',
    }),
    entryPoint: 'main',
  },
});
// Writes its own indirect arguments, so the guard must see a GPU write.
const writer = device.createComputePipeline({
  layout: 'auto',
  compute: {
    module: device.createShaderModule({
      code: '@group(0) @binding(0) var<storage, read_write> args: array<u32, 3>; @compute @workgroup_size(1) fn main() { args[0] = 3u; args[1] = 1u; args[2] = 1u; }',
    }),
    entryPoint: 'main',
  },
});

async function hits(counts, writeOnGpu = false) {
  const indirect = device.createBuffer({ size: 12, usage: GPUBufferUsage.INDIRECT | GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
  device.queue.writeBuffer(indirect, 0, new Uint32Array(counts));
  const output = device.createBuffer({ size: 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
  const readback = device.createBuffer({ size: 4, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  if (writeOnGpu) {
    pass.setPipeline(writer);
    pass.setBindGroup(0, device.createBindGroup({ layout: writer.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: indirect } }] }));
    pass.dispatchWorkgroups(1);
  }
  pass.setPipeline(counter);
  pass.setBindGroup(0, device.createBindGroup({ layout: counter.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: output } }] }));
  pass.dispatchWorkgroupsIndirect(indirect, 0);
  pass.end();
  encoder.copyBufferToBuffer(output, 0, readback, 0, 4);
  device.queue.submit([encoder.finish()]);
  assert.equal(await device.popErrorScope(), null, 'indirect dispatches encode without validation errors');
  await readback.mapAsync(GPUMapMode.READ);
  const value = new Uint32Array(readback.getMappedRange())[0];
  readback.unmap();
  return value;
}

assert.equal(await hits([2, 1, 1]), 2, 'counts within the limit dispatch');
assert.equal(await hits([limit + 1, 1, 1]), 0, 'a count above the limit dispatches nothing');
assert.equal(await hits([1, 1, limit + 1]), 0, 'any dimension above the limit dispatches nothing');
assert.equal(await hits([limit + 1, 1, 1], true), 3, 'arguments a dispatch wrote earlier in the pass are the ones validated');
console.log('ok: indirect dispatch counts above maxComputeWorkgroupsPerDimension are skipped');
device.destroy();
