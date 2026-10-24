#!/usr/bin/env node
// WebGPU usage scopes
// (https://gpuweb.github.io/gpuweb/#programming-model-synchronization): within
// a render pass, or one dispatch, a buffer or texture subresource is either
// only read or used with one writable usage; a conflict invalidates the
// encoder and finish() reports it. Also the createTexture usage/format rules
// and createView usage subset.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('usage scope: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUTextureUsage, GPUBufferUsage, GPUShaderStage, GPUValidationError } = target;

async function validationError(fn) {
  device.pushErrorScope('validation');
  fn();
  return device.popErrorScope();
}
async function expectValid(label, fn) {
  assert.equal(await validationError(fn), null, `${label} is valid`);
}
async function expectInvalid(label, fn) {
  assert(await validationError(fn) instanceof GPUValidationError, `${label} is a validation error`);
}

const usage = GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT;
const texture = device.createTexture({ size: [4, 4, 2], format: 'rgba8unorm', usage });
const layer = (baseArrayLayer) => texture.createView({ dimension: '2d', baseArrayLayer, arrayLayerCount: 1 });
const sampledLayout = device.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT | GPUShaderStage.COMPUTE, texture: {} }] });
const storageLayout = device.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT | GPUShaderStage.COMPUTE, storageTexture: { format: 'rgba8unorm' } }] });
const sampled = (view) => device.createBindGroup({ layout: sampledLayout, entries: [{ binding: 0, resource: view }] });
const storage = (view) => device.createBindGroup({ layout: storageLayout, entries: [{ binding: 0, resource: view }] });

function renderPass(attachment, record) {
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({ colorAttachments: [{ view: attachment, loadOp: 'clear', storeOp: 'store' }] });
  record(pass);
  pass.end();
  encoder.finish();
}

// One subresource as attachment and sampled, or sampled and storage, conflicts;
// another layer of the same texture does not.
await expectInvalid('a sampled attachment', () => renderPass(layer(0), (pass) => pass.setBindGroup(0, sampled(layer(0)))));
await expectValid('sampling another layer than the attachment', () => renderPass(layer(0), (pass) => pass.setBindGroup(0, sampled(layer(1)))));
await expectInvalid('one layer sampled and written as storage in a pass', () => renderPass(layer(1), (pass) => {
  pass.setBindGroup(0, sampled(layer(0)));
  pass.setBindGroup(1, storage(layer(0)));
}));
// A render pass scope includes replaced bind groups.
await expectInvalid('a replaced bind group still in the pass scope', () => renderPass(layer(1), (pass) => {
  pass.setBindGroup(0, sampled(layer(0)));
  pass.setBindGroup(0, storage(layer(0)));
}));

// A buffer bound as storage and used as a vertex buffer in one pass conflicts.
const buffer = device.createBuffer({ size: 256, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.VERTEX });
const bufferLayout = device.createBindGroupLayout({ entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT | GPUShaderStage.COMPUTE, buffer: { type: 'storage' } }] });
const storageBuffer = device.createBindGroup({ layout: bufferLayout, entries: [{ binding: 0, resource: { buffer } }] });
await expectInvalid('a storage buffer used as a vertex buffer in one pass', () => renderPass(layer(0), (pass) => {
  pass.setBindGroup(0, storageBuffer);
  pass.setVertexBuffer(0, buffer);
}));

// A dispatch's scope is the bind groups its pipeline uses.
const module = device.createShaderModule({ code: `
  @group(0) @binding(0) var sampledTexture: texture_2d<f32>;
  @group(1) @binding(0) var storageTexture: texture_storage_2d<rgba8unorm, write>;
  @compute @workgroup_size(1) fn main() {
    _ = textureLoad(sampledTexture, vec2u(0), 0);
    textureStore(storageTexture, vec2u(0), vec4f(0));
  }` });
const pipeline = device.createComputePipeline({
  layout: device.createPipelineLayout({ bindGroupLayouts: [sampledLayout, storageLayout] }),
  compute: { module, entryPoint: 'main' },
});
function dispatch(view0, view1) {
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, sampled(view0));
  pass.setBindGroup(1, storage(view1));
  pass.dispatchWorkgroups(1);
  pass.end();
  encoder.finish();
}
await expectInvalid('a dispatch sampling and writing one layer', () => dispatch(layer(0), layer(0)));
await expectValid('a dispatch sampling one layer and writing another', () => dispatch(layer(0), layer(1)));

// createTexture and createView usage rules.
await expectInvalid('a 1d render attachment', () => device.createTexture({ size: [4], dimension: '1d', format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT }));
await expectInvalid('storage usage of rgba8unorm-srgb', () => device.createTexture({ size: [4, 4], format: 'rgba8unorm-srgb', usage: GPUTextureUsage.STORAGE_BINDING }));
await expectInvalid('a view usage outside the texture usage', () => device.createTexture({ size: [4, 4], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING }).createView({ usage: GPUTextureUsage.STORAGE_BINDING }));
await expectInvalid('a cube view of non-square faces', () => device.createTexture({ size: [4, 8, 6], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING }).createView({ dimension: 'cube' }));
console.log('ok: usage scopes and texture usage rules are validated');
device.destroy();
