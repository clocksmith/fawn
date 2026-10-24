#!/usr/bin/env node
// After device.destroy() WebGPU makes the device inert: creation returns
// invalid objects, queue and encoder operations do nothing, popErrorScope
// resolves null, mapAsync rejects with AbortError, and nothing throws or
// generates an error.

import assert from 'node:assert/strict';
import { requestNativeDeviceOrSkip } from './native-device-test-helper.js';

const device = await requestNativeDeviceOrSkip('device lost');
const { GPUBufferUsage, GPUTextureUsage, GPUShaderStage, GPUMapMode } = globalThis;

const code = '@compute @workgroup_size(1) fn main() {}\n'
  + '@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0); }\n'
  + '@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0); }';
const texture = device.createTexture({
  size: [4, 4], format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST,
});
const buffer = device.createBuffer({ size: 64, usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
const readable = device.createBuffer({ size: 16, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
const recordedBefore = device.createCommandEncoder();
recordedBefore.copyBufferToBuffer(buffer, 0, buffer, 32, 16);

const events = [];
device.addEventListener('uncapturederror', (event) => events.push(event.error));
const lost = device.lost;
device.destroy();
assert.equal((await lost).reason, 'destroyed');

// The CTS pushes scopes before a test and pops them after it.
device.pushErrorScope('validation');
const created = [
  device.createBuffer({ size: 16, usage: GPUBufferUsage.COPY_DST }),
  device.createTexture({ size: [4, 4], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING }),
  texture.createView(),
  device.createSampler(),
  device.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: {} }] }),
  device.createPipelineLayout({ bindGroupLayouts: [] }),
  device.createShaderModule({ code }),
  device.createQuerySet({ type: 'occlusion', count: 1 }),
  device.createRenderBundleEncoder({ colorFormats: ['rgba8unorm'] }),
];
for (const object of created) {
  assert.equal(object._invalid, true, `${object._resourceLabel} on a lost device is invalid`);
}
const layout = created[4];
assert.equal(device.createBindGroup({ layout, entries: [] })._invalid, true);
const module = created[6];
assert.equal(device.createComputePipeline({ layout: 'auto', compute: { module } })._invalid, true);
assert.equal(device.createRenderPipeline({
  layout: 'auto', vertex: { module }, fragment: { module, targets: [{ format: 'rgba8unorm' }] },
})._invalid, true);
assert.equal((await device.createComputePipelineAsync({ layout: 'auto', compute: { module } }))._invalid, true);
console.log('ok: creation on a lost device returns invalid objects');

const mapped = device.createBuffer({ size: 16, usage: GPUBufferUsage.MAP_WRITE | GPUBufferUsage.COPY_SRC, mappedAtCreation: true });
assert.equal(mapped.getMappedRange().byteLength, 16, 'mappedAtCreation still yields a mapping');
mapped.unmap();
await assert.rejects(readable.mapAsync(GPUMapMode.READ), (error) => error.name === 'AbortError');
console.log('ok: mappedAtCreation maps host memory and mapAsync rejects with AbortError');

const bundleEncoder = created[8];
bundleEncoder.draw(3);
assert.equal(bundleEncoder.finish()._invalid, true);
const encoder = device.createCommandEncoder();
encoder.copyBufferToBuffer(buffer, 0, buffer, 32, 16);
encoder.clearBuffer(buffer);
const compute = encoder.beginComputePass();
compute.dispatchWorkgroups(1);
compute.end();
const render = encoder.beginRenderPass({ colorAttachments: [{ view: texture.createView(), loadOp: 'clear', storeOp: 'store' }] });
render.setViewport(0, 0, 1, 1, 0, 1);
render.draw(3);
render.end();
const commands = encoder.finish();
assert.equal(commands._invalid, true);
assert.equal(recordedBefore.finish()._invalid, true, 'an encoder recorded before loss finishes invalid');
assert.equal(recordedBefore.finish()._invalid, true, 'a second finish() on a lost device does not throw');
compute.end();
render.end();
device.queue.submit([commands]);
device.queue.writeBuffer(buffer, 0, new Uint32Array(4));
device.queue.writeTexture({ texture }, new Uint8Array(64), { bytesPerRow: 16 }, [4, 4]);
await device.queue.onSubmittedWorkDone();
console.log('ok: encoders and queue operations on a lost device do nothing');

assert.equal(await device.popErrorScope(), null);
assert.equal(await device.popErrorScope(), null, 'a pop past the stack resolves null on a lost device');
await new Promise((resolve) => setTimeout(resolve, 20));
assert.equal(events.length, 0, `a lost device generates no errors, got ${events.map((error) => error.message)}`);
console.log('ok: a lost device generates no errors and pops resolve null');
