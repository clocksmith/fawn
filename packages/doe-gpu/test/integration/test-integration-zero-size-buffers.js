#!/usr/bin/env node
// Zero-size buffers keep size 0 all the way to the provider: they map to empty
// ranges, unmap before submission, zero-byte copies validate their offsets, and an indexed draw reading
// past an empty index buffer is a validation error.
//
// Known provider gap: the Zig drop-in crashes creating a zero-size Vulkan
// buffer, so the package gives that provider a 4-byte native stand-in; an
// indexed draw then fits inside the stand-in and is not rejected there.

import assert from 'node:assert/strict';
import { providerInfo, setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
const adapter = await target.navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('zero-size buffers: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage, GPUMapMode, GPUTextureUsage, GPUValidationError } = target;
const flavor = providerInfo().libraryFlavor;
const nativeZeroSize = flavor !== 'doe-dropin';
if (!nativeZeroSize) console.log(`known gap: ${flavor} needs a native stand-in for zero-size buffers`);

async function errorOf(action) {
  device.pushErrorScope('validation');
  action();
  return device.popErrorScope();
}

const empty = device.createBuffer({ size: 0, usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
const target16 = device.createBuffer({ size: 16, usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
assert.equal(empty.size, 0);
const mapped = device.createBuffer({ size: 0, usage: GPUBufferUsage.MAP_WRITE | GPUBufferUsage.COPY_SRC, mappedAtCreation: true });
assert.equal(mapped.getMappedRange().byteLength, 0);
mapped.unmap();
assert.equal(await errorOf(() => {
  const encoder = device.createCommandEncoder();
  encoder.copyBufferToBuffer(mapped, 0, target16, 0, 0);
  device.queue.submit([encoder.finish()]);
}), null, 'a zero-size buffer created mapped is unmapped for submission');
const readable = device.createBuffer({ size: 0, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
await readable.mapAsync(GPUMapMode.READ);
assert.equal(readable.getMappedRange().byteLength, 0);
readable.unmap();
console.log('ok: zero-size buffers report size 0 and map to empty ranges');

assert.equal(await errorOf(() => {
  const encoder = device.createCommandEncoder();
  encoder.copyBufferToBuffer(empty, 0, target16, 0, 0);
  device.queue.submit([encoder.finish()]);
}), null, 'a zero-byte copy from a zero-size buffer is valid');
assert(await errorOf(() => {
  const encoder = device.createCommandEncoder();
  encoder.copyBufferToBuffer(empty, 4, target16, 0, 0);
  encoder.finish();
}) instanceof GPUValidationError, 'a zero-byte copy past the end of a zero-size buffer is rejected at finish');
console.log('ok: zero-byte copies validate their offsets');

if (nativeZeroSize) {
  const indices = device.createBuffer({ size: 0, usage: GPUBufferUsage.INDEX });
  const module = device.createShaderModule({
    code: `@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }
           @fragment fn fs() -> @location(0) vec4f { return vec4f(1.0); }`,
  });
  const pipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module, entryPoint: 'vs' },
    fragment: { module, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] },
  });
  const view = device.createTexture({ size: [1, 1], format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT }).createView();
  async function drawIndexed(firstIndex) {
    return errorOf(() => {
      const encoder = device.createCommandEncoder();
      const pass = encoder.beginRenderPass({ colorAttachments: [{ view, loadOp: 'clear', storeOp: 'store' }] });
      pass.setPipeline(pipeline);
      pass.setIndexBuffer(indices, 'uint32');
      pass.drawIndexed(0, 1, firstIndex);
      pass.end();
      encoder.finish();
    });
  }
  assert.equal(await drawIndexed(0), null, 'an empty draw from an empty index buffer is valid');
  assert(await drawIndexed(1) instanceof GPUValidationError, 'firstIndex past an empty index buffer is rejected');
  console.log('ok: indexed draws see the real zero size');
}
device.destroy();
