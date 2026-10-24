#!/usr/bin/env node
// texture-component-swizzle reaches the provider as webgpu.h's chained
// WGPUTextureComponentSwizzleDescriptor and changes what a sampled view returns.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

setupGlobals();
const adapter = await navigator.gpu.requestAdapter();
if (!adapter || !adapter.features.has('texture-component-swizzle')) {
  console.log('texture view swizzle: skipped (adapter lacks texture-component-swizzle)');
  process.exit(0);
}
const device = await adapter.requestDevice({ requiredFeatures: ['texture-component-swizzle'] });
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUMapMode: M } = globalThis;

const texture = device.createTexture({ size: { width: 1, height: 1 }, format: 'rgba8unorm', usage: T.TEXTURE_BINDING | T.COPY_DST });
device.queue.writeTexture({ texture }, new Uint8Array([255, 0, 51, 102]), { bytesPerRow: 4 }, { width: 1, height: 1 });
const module = device.createShaderModule({ code: `
  @group(0) @binding(0) var t: texture_2d<f32>;
  @group(0) @binding(1) var<storage, read_write> o: array<vec4<f32>>;
  @compute @workgroup_size(1) fn main() { o[0] = textureLoad(t, vec2<i32>(0, 0), 0); }` });
const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' } });

async function load(swizzle) {
  const output = device.createBuffer({ size: 16, usage: B.STORAGE | B.COPY_SRC });
  const readback = device.createBuffer({ size: 16, usage: B.MAP_READ | B.COPY_DST });
  const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
    { binding: 0, resource: texture.createView(swizzle ? { swizzle } : {}) },
    { binding: 1, resource: { buffer: output } }] });
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, group);
  pass.dispatchWorkgroups(1);
  pass.end();
  encoder.copyBufferToBuffer(output, 0, readback, 0, 16);
  device.queue.submit([encoder.finish()]);
  await readback.mapAsync(M.READ);
  const value = Array.from(new Float32Array(readback.getMappedRange()), (x) => Math.round(x * 255));
  readback.unmap();
  return value;
}

assert.deepEqual(await load(undefined), [255, 0, 51, 102]);
assert.deepEqual(await load('abgr'), [102, 51, 0, 255]);
assert.deepEqual(await load('r01g'), [255, 0, 255, 0]);
console.log('ok: swizzled views reorder and substitute components');
device.destroy();
