#!/usr/bin/env node
// texture-component-swizzle reaches the provider as webgpu.h's chained
// WGPUTextureComponentSwizzleDescriptor and changes what a sampled view returns.

import assert from 'node:assert/strict';
import { providerInfo, setupGlobals } from '../../src/index.js';

setupGlobals();
const adapter = await navigator.gpu.requestAdapter();
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUMapMode: M } = globalThis;

async function validationError(device, action) {
  device.pushErrorScope('validation');
  action();
  return (await device.popErrorScope()) !== null;
}

// Without the feature (a second adapter, since an adapter yields one device)
// the identity "rgba" is admitted, any other swizzle is a validation error,
// and a timestamp query set throws TypeError.
if (adapter) {
  const plain = await (await navigator.gpu.requestAdapter()).requestDevice();
  const texture = plain.createTexture({ size: [1, 1], format: 'rgba8unorm', usage: T.TEXTURE_BINDING });
  assert.equal(await validationError(plain, () => texture.createView({ swizzle: 'rgba' })), false);
  assert.equal(await validationError(plain, () => texture.createView({ swizzle: 'abgr' })), true);
  assert.throws(() => plain.createQuerySet({ type: 'timestamp', count: 1 }), TypeError);
  plain.destroy();
  console.log('ok: featureless device admits identity swizzle; non-identity and timestamp sets reject');
}

if (!adapter || !adapter.features.has('texture-component-swizzle')) {
  console.log('texture view swizzle: skipped (adapter lacks texture-component-swizzle)');
  process.exit(0);
}
const device = await adapter.requestDevice({ requiredFeatures: ['texture-component-swizzle'] });

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

// swizzle is a DOMString: 1111 converts to "1111"; anything outside [rgba01]{4} throws TypeError.
for (const invalid of ['rgbA', 'rgb', 'rgba01', 'ｒｇｂａ', 1234, true, null]) {
  assert.throws(() => texture.createView({ swizzle: invalid }), TypeError, String(invalid));
}
assert.equal(await validationError(device, () => texture.createView({ swizzle: 1111 })), false);

// A swizzled view of an attachment texture is created; using it as a color
// attachment is the validation error, reported when the encoder finishes.
const target = device.createTexture({ size: [1, 1], format: 'rgba8unorm',
  usage: T.RENDER_ATTACHMENT | T.TEXTURE_BINDING });
let swizzled;
assert.equal(await validationError(device, () => { swizzled = target.createView({ swizzle: 'bgra' }); }), false);
const attachmentRejected = await validationError(device, () => {
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({ colorAttachments: [{ view: swizzled, loadOp: 'clear', storeOp: 'store' }] });
  pass.end();
  encoder.finish();
});
const flavor = providerInfo().libraryFlavor;
if (flavor === 'doe-dropin') {
  console.log(`known gap: ${flavor} encodes a swizzled color attachment without a validation error`);
} else {
  assert.equal(attachmentRejected, true);
}
console.log('ok: invalid swizzles throw TypeError; a swizzled view is created with attachment usage');
device.destroy();
