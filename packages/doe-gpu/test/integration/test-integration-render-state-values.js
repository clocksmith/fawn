#!/usr/bin/env node
// Render and copy state crosses the addon as webgpu.h values on every provider:
// sampler filters, stencil operations, copy aspects, per-aspect attachment
// operations, null color slots and fragment-less pipelines, checked by readback;
// provider validation errors reach native error scopes.

import assert from 'node:assert/strict';
import { requestNativeDeviceOrSkip } from './native-device-test-helper.js';

const device = await requestNativeDeviceOrSkip('render state values');
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUMapMode: M } = globalThis;
const SIZE = { width: 4, height: 4 };

async function readFirstTexel(texture, aspect, bytes) {
  const buffer = device.createBuffer({ size: 256 * SIZE.height, usage: B.COPY_DST | B.MAP_READ });
  const encoder = device.createCommandEncoder();
  encoder.copyTextureToBuffer({ texture, aspect }, { buffer, bytesPerRow: 256, rowsPerImage: SIZE.height }, SIZE);
  device.queue.submit([encoder.finish()]);
  await buffer.mapAsync(M.READ);
  const texel = new Uint8Array(buffer.getMappedRange()).slice(0, bytes);
  buffer.unmap();
  buffer.destroy();
  return texel;
}

const fullscreen = device.createShaderModule({ code: `
  @vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
    let p = array<vec2<f32>, 3>(vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    return vec4<f32>(p[i], 0.25, 1.0);
  }
  struct Targets { @location(0) color: vec4<f32>, @location(2) id: u32 }
  @fragment fn color() -> @location(0) vec4<f32> { return vec4<f32>(1.0); }
  @fragment fn targets() -> Targets { return Targets(vec4<f32>(1.0, 0.0, 1.0, 1.0), 42u); }` });

{
  // A 2x1 [0, 255] texture sampled linearly halfway between the texels.
  const texture = device.createTexture({ size: { width: 2, height: 1 }, format: 'rgba8unorm', usage: T.TEXTURE_BINDING | T.COPY_DST });
  device.queue.writeTexture({ texture }, new Uint8Array([0, 0, 0, 255, 255, 255, 255, 255]), { bytesPerRow: 8 }, { width: 2, height: 1 });
  const output = device.createBuffer({ size: 4, usage: B.STORAGE | B.COPY_SRC });
  const readback = device.createBuffer({ size: 4, usage: B.MAP_READ | B.COPY_DST });
  const module = device.createShaderModule({ code: `
    @group(0) @binding(0) var t: texture_2d<f32>;
    @group(0) @binding(1) var s: sampler;
    @group(0) @binding(2) var<storage, read_write> o: array<f32>;
    @compute @workgroup_size(1) fn main() { o[0] = textureSampleLevel(t, s, vec2<f32>(0.5, 0.5), 0.0).x; }` });
  const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' } });
  const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
    { binding: 0, resource: texture.createView() },
    { binding: 1, resource: device.createSampler({ magFilter: 'linear', minFilter: 'linear' }) },
    { binding: 2, resource: { buffer: output } }] });
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, group);
  pass.dispatchWorkgroups(1);
  pass.end();
  encoder.copyBufferToBuffer(output, 0, readback, 0, 4);
  device.queue.submit([encoder.finish()]);
  await readback.mapAsync(M.READ);
  const value = new Float32Array(readback.getMappedRange())[0];
  readback.unmap();
  assert(Math.abs(value - 0.5) < 0.01, `linear filtering read ${value}, expected 0.5`);
  console.log('ok: linear sampler filters between texels');
}

for (const format of ['stencil8', 'depth24plus-stencil8']) {
  // Stencil replace with reference 7; depth operations only where the format has depth.
  // A member present with the value undefined is absent (WebIDL), so failOp is keep.
  const hasDepth = format !== 'stencil8';
  const stencil = device.createTexture({ size: SIZE, format, usage: T.RENDER_ATTACHMENT | T.COPY_SRC });
  const color = device.createTexture({ size: SIZE, format: 'rgba8unorm', usage: T.RENDER_ATTACHMENT });
  const face = { compare: 'always', passOp: 'replace', failOp: undefined, depthFailOp: 'keep' };
  const pipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module: fullscreen, entryPoint: 'vs' },
    fragment: { module: fullscreen, entryPoint: 'color', targets: [{ format: 'rgba8unorm' }] },
    depthStencil: { format, stencilFront: face, stencilBack: face,
      ...(hasDepth ? { depthWriteEnabled: false, depthCompare: 'always' } : {}) },
  });
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({
    colorAttachments: [{ view: color.createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
    depthStencilAttachment: { view: stencil.createView(), stencilLoadOp: 'clear', stencilStoreOp: 'store', stencilClearValue: 0,
      ...(hasDepth ? { depthLoadOp: 'clear', depthStoreOp: 'store', depthClearValue: 1 } : {}) },
  });
  pass.setPipeline(pipeline);
  pass.setStencilReference(7);
  pass.draw(3);
  pass.end();
  device.queue.submit([encoder.finish()]);
  assert.equal((await readFirstTexel(stencil, 'stencil-only', 1))[0], 7, `${format} stencil after replace`);
  console.log(`ok: ${format} stencil replace writes the reference`);
}

for (const constant of [[0.25, 0.5, 0.75, 1], { r: 0.25, g: 0.5, b: 0.75, a: 1 }]) {
  // White source times the blend constant: every channel reads the constant, so
  // a dropped constant ((0, 0, 0, 1)) or a wrong component order shows up.
  const color = device.createTexture({ size: SIZE, format: 'rgba8unorm', usage: T.RENDER_ATTACHMENT | T.COPY_SRC });
  const blend = { srcFactor: 'constant', dstFactor: 'zero', operation: 'add' };
  const pipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module: fullscreen, entryPoint: 'vs' },
    fragment: { module: fullscreen, entryPoint: 'color', targets: [{ format: 'rgba8unorm', blend: { color: blend, alpha: blend } }] },
  });
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({ colorAttachments: [{ view: color.createView(), loadOp: 'clear', storeOp: 'store',
    clearValue: { r: 0, g: 0, b: 0, a: 0 } }] });
  pass.setPipeline(pipeline);
  pass.setBlendConstant(constant);
  pass.draw(3);
  pass.end();
  device.queue.submit([encoder.finish()]);
  assert.deepEqual(Array.from(await readFirstTexel(color, 'all', 4)), [64, 128, 191, 255]);
}
{
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({ colorAttachments: [{ view: device.createTexture({ size: SIZE, format: 'rgba8unorm',
    usage: T.RENDER_ATTACHMENT }).createView(), loadOp: 'clear', storeOp: 'store' }] });
  assert.throws(() => pass.setBlendConstant([1, 1, 1]), /four/);
  pass.end();
  console.log('ok: blend constants reach the provider as sequences and dictionaries');
}
{
  // An omitted clearValue clears to transparent black.
  const color = device.createTexture({ size: SIZE, format: 'rgba8unorm', usage: T.RENDER_ATTACHMENT | T.COPY_SRC });
  const encoder = device.createCommandEncoder();
  encoder.beginRenderPass({ colorAttachments: [{ view: color.createView(), loadOp: 'clear', storeOp: 'store' }] }).end();
  device.queue.submit([encoder.finish()]);
  assert.deepEqual(Array.from(await readFirstTexel(color, 'all', 4)), [0, 0, 0, 0]);
  console.log('ok: an omitted clearValue is (0, 0, 0, 0)');
}

{
  // A provider-side validation error lands in the innermost native error scope.
  const texture = device.createTexture({ size: SIZE, format: 'rgba8unorm', usage: T.COPY_DST });
  device.pushErrorScope('validation');
  device.queue.writeTexture({ texture, origin: { x: 3 } }, new Uint8Array(64), { bytesPerRow: 16 }, SIZE);
  const error = await device.popErrorScope();
  assert(error instanceof Error && error.message.length > 0, `writeTexture range error reaches the scope, got ${error}`);
  console.log('ok: error scopes capture provider validation errors');
}

{
  // createShaderModule generates a validation error for invalid WGSL as well
  // as reporting it through getCompilationInfo; a valid module generates none.
  device.pushErrorScope('validation');
  const invalid = device.createShaderModule({ code: 'not WGSL' });
  const error = await device.popErrorScope();
  assert(error instanceof Error && error.message.length > 0, `invalid WGSL reaches the validation scope, got ${error}`);
  const messages = (await invalid.getCompilationInfo()).messages;
  assert(messages.some((message) => message.type === 'error' && message.message.length > 0), 'invalid WGSL reports an error message');
  device.pushErrorScope('validation');
  device.createShaderModule({ code: '@compute @workgroup_size(1) fn main() {}' });
  assert((await device.popErrorScope()) === null, 'valid WGSL generates no validation error');
  console.log('ok: invalid WGSL reports a validation error and compilation errors');
}

// Several color targets with a null slot, and a depth-only pass.
{
  const first = device.createTexture({ size: SIZE, format: 'rgba8unorm', usage: T.RENDER_ATTACHMENT | T.COPY_SRC });
  const third = device.createTexture({ size: SIZE, format: 'r32uint', usage: T.RENDER_ATTACHMENT | T.COPY_SRC });
  const pipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module: fullscreen, entryPoint: 'vs' },
    fragment: { module: fullscreen, entryPoint: 'targets', targets: [{ format: 'rgba8unorm' }, null, { format: 'r32uint' }] },
  });
  const clear = { loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } };
  let encoder = device.createCommandEncoder();
  let pass = encoder.beginRenderPass({ colorAttachments: [{ view: first.createView(), ...clear }, null, { view: third.createView(), ...clear }] });
  pass.setPipeline(pipeline);
  pass.draw(3);
  pass.end();
  device.queue.submit([encoder.finish()]);
  assert.deepEqual(Array.from(await readFirstTexel(first, 'all', 4)), [255, 0, 255, 255]);
  assert.equal(new Uint32Array((await readFirstTexel(third, 'all', 4)).buffer)[0], 42);
  console.log('ok: color targets with a null slot write their own attachments');

  const depth = device.createTexture({ size: SIZE, format: 'depth32float', usage: T.RENDER_ATTACHMENT | T.COPY_SRC });
  const depthOnly = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module: fullscreen, entryPoint: 'vs' },
    depthStencil: { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'always' },
  });
  encoder = device.createCommandEncoder();
  pass = encoder.beginRenderPass({ colorAttachments: [],
    depthStencilAttachment: { view: depth.createView(), depthLoadOp: 'clear', depthStoreOp: 'store', depthClearValue: 1 } });
  pass.setPipeline(depthOnly);
  pass.draw(3);
  pass.end();
  device.queue.submit([encoder.finish()]);
  assert.equal(new Float32Array((await readFirstTexel(depth, 'depth-only', 4)).buffer)[0], 0.25);
  console.log('ok: a fragment-less pipeline writes depth');
}

device.destroy();
