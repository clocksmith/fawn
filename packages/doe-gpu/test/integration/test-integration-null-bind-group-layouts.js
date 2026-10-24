#!/usr/bin/env node
// createPipelineLayout accepts null bindGroupLayouts entries (WebGPU
// sequence<GPUBindGroupLayout?>) and passes them to the provider as null
// layouts; a pipeline using only group 1 then dispatches with only group 1 set.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('null bind group layouts: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage, GPUMapMode, GPUShaderStage } = target;
const layout = device.createBindGroupLayout({
  entries: [{ binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } }],
});

device.pushErrorScope('validation');
const pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [null, layout] });
device.createPipelineLayout({ bindGroupLayouts: [null] });
const pipeline = device.createComputePipeline({
  layout: pipelineLayout,
  compute: {
    module: device.createShaderModule({
      code: '@group(1) @binding(0) var<storage, read_write> value: u32; @compute @workgroup_size(1) fn main() { value = 7u; }',
    }),
    entryPoint: 'main',
  },
});
const output = device.createBuffer({ size: 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
const readback = device.createBuffer({ size: 4, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
const encoder = device.createCommandEncoder();
const pass = encoder.beginComputePass();
pass.setPipeline(pipeline);
pass.setBindGroup(1, device.createBindGroup({ layout, entries: [{ binding: 0, resource: { buffer: output } }] }));
pass.dispatchWorkgroups(1);
pass.end();
encoder.copyBufferToBuffer(output, 0, readback, 0, 4);
device.queue.submit([encoder.finish()]);
assert.equal(await device.popErrorScope(), null, 'null layouts create a pipeline that dispatches without group 0');
await readback.mapAsync(GPUMapMode.READ);
assert.equal(new Uint32Array(readback.getMappedRange())[0], 7);
readback.unmap();
console.log('ok: null bindGroupLayouts entries are empty groups');
device.destroy();
