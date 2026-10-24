#!/usr/bin/env node
// Synchronous creation follows WebGPU error semantics when errors can be
// observed: with an error scope or an uncapturederror listener, a failed
// creation reports one validation error and returns an invalid object of the
// right class, and using that object in another creation or a queue write is
// itself a validation error. With neither, creation throws as it did before.
//
// Failures come from package validation and from the provider; each provider
// error is reported exactly once (the C runtime reports it, the Zig drop-in
// sometimes rejects without reporting, and the package then reports it).

import assert from 'node:assert/strict';
import { requestNativeDeviceOrSkip } from './native-device-test-helper.js';

const device = await requestNativeDeviceOrSkip('creation errors');
const {
  GPUBufferUsage,
  GPUTextureUsage,
  GPUShaderStage,
  GPUValidationError,
} = globalThis;

const texture = device.createTexture({
  size: [4, 4],
  format: 'rgba8unorm',
  usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT,
});
const computeModule = device.createShaderModule({ code: '@compute @workgroup_size(1) fn main() {}' });
const renderModule = device.createShaderModule({
  code: '@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0); }\n'
    + '@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0); }',
});
const sampledLayout = device.createBindGroupLayout({
  entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: {} }],
});

// Each case fails on both providers. The createView (provider) case is left to
// the provider; createBindGroup and createPipelineLayout also make the invalid
// object they then use, so each of them reports two errors in total.
const failures = [
  ['createBuffer', 'GPUBuffer', () => device.createBuffer({ size: 4, usage: 0 })],
  ['createTexture', 'GPUTexture', () => device.createTexture({
    size: [4, 4], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING, sampleCount: 0,
  })],
  ['createView', 'GPUTextureView', () => texture.createView({ format: 'r8unorm' })],
  ['createView (provider)', 'GPUTextureView', () => texture.createView({ baseMipLevel: 3 })],
  ['createSampler', 'GPUSampler', () => device.createSampler({ lodMinClamp: 5, lodMaxClamp: 1 })],
  ['createBindGroupLayout', 'GPUBindGroupLayout', () => device.createBindGroupLayout({
    entries: [{
      binding: 0,
      visibility: GPUShaderStage.COMPUTE,
      storageTexture: { format: 'rgba8unorm', access: 'read-write' },
    }],
  })],
  ['createBindGroup', 'GPUBindGroup', () => device.createBindGroup({
    layout: sampledLayout,
    entries: [{ binding: 0, resource: texture.createView({ format: 'r8unorm' }) }],
  })],
  ['createPipelineLayout', 'GPUPipelineLayout', () => device.createPipelineLayout({
    bindGroupLayouts: [device.createBindGroupLayout({
      entries: [{
        binding: 0,
        visibility: GPUShaderStage.COMPUTE,
        storageTexture: { format: 'rgba8unorm', access: 'read-write' },
      }],
    })],
  })],
  ['createComputePipeline', 'GPUComputePipeline', () => device.createComputePipeline({
    layout: 'auto', compute: { module: computeModule, entryPoint: 'missing' },
  })],
  ['createRenderPipeline', 'GPURenderPipeline', () => device.createRenderPipeline({
    layout: 'auto', vertex: { module: renderModule, entryPoint: 'vs' },
  })],
  ['createQuerySet', 'GPUQuerySet', () => device.createQuerySet({ type: 'occlusion', count: 5000 })],
  ['createRenderBundleEncoder', 'GPURenderBundleEncoder', () => device.createRenderBundleEncoder({ colorFormats: [] })],
  ['getBindGroupLayout', 'GPUBindGroupLayout', () => device.createComputePipeline({
    layout: 'auto', compute: { module: computeModule, entryPoint: 'main' },
  }).getBindGroupLayout(100)],
];

async function scoped(action) {
  device.pushErrorScope('validation');
  let value;
  let thrown = null;
  try {
    value = action();
  } catch (error) {
    thrown = error;
  }
  return { value, thrown, error: await device.popErrorScope() };
}

for (const [name, label, action] of failures) {
  const { value, thrown, error } = await scoped(action);
  assert.equal(thrown, null, `${name} must not throw inside a validation scope: ${thrown?.message}`);
  assert(error instanceof GPUValidationError, `${name} reports a GPUValidationError, got ${error}`);
  assert(error.message.length > 0, `${name} reports a message`);
  assert.equal(value._resourceLabel, label, `${name} returns a ${label}`);
  assert.equal(value._invalid, true, `${name} returns an invalid object`);
}
console.log(`ok: ${failures.length} creators report one validation error and return an invalid object`);

// Every later use of an invalid object is a validation error.
async function invalidObject(action) {
  return (await scoped(action)).value;
}
const invalidView = await invalidObject(() => texture.createView({ format: 'r8unorm' }));
const invalidTexture = await invalidObject(() => device.createTexture({
  size: [4, 4], format: 'rgba8unorm', usage: 0,
}));
const invalidBuffer = await invalidObject(() => device.createBuffer({ size: 16, usage: 0 }));

const uses = [
  ['bind group with an invalid view', () => device.createBindGroup({
    layout: sampledLayout, entries: [{ binding: 0, resource: invalidView }],
  })],
  ['createView on an invalid texture', () => invalidTexture.createView()],
  ['writeBuffer to an invalid buffer', () => device.queue.writeBuffer(invalidBuffer, 0, new Uint32Array(4))],
];
for (const [name, action] of uses) {
  const { value, thrown, error } = await scoped(action);
  assert.equal(thrown, null, `${name} must not throw inside a validation scope: ${thrown?.message}`);
  assert(error instanceof GPUValidationError, `${name} is a validation error, got ${error}`);
  if (value && typeof value === 'object') {
    assert.equal(value._invalid, true, `${name} returns an invalid object`);
  }
}
console.log(`ok: ${uses.length} uses of invalid objects are validation errors`);

// Exactly once: with only an uncapturederror listener, each failed creation
// dispatches one event, including after asynchronous native deliveries.
const events = [];
const listener = (event) => events.push(event.error);
device.addEventListener('uncapturederror', listener);
for (const [name, , action] of failures) {
  const before = events.length;
  action();
  await device.queue.onSubmittedWorkDone();
  await new Promise((resolve) => setTimeout(resolve, 20));
  const expected = name === 'createBindGroup' || name === 'createPipelineLayout' ? 2 : 1;
  assert.equal(events.length - before, expected, `${name} reports ${expected} uncaptured error(s), got ${events.length - before}`);
}
device.removeEventListener('uncapturederror', listener);
console.log('ok: each failed creation is reported exactly once');

// With no scope and no listener, creation and use throw as before.
for (const [name, , action] of failures) {
  assert.throws(action, Error, `${name} throws without a scope or listener`);
}
assert.throws(() => device.queue.writeBuffer(invalidBuffer, 0, new Uint32Array(4)), /invalid/);
console.log('ok: without a scope or listener, failed creations still throw');

// Async pipeline creation rejects with GPUPipelineError and reports nothing.
const asyncResult = await scoped(() => device.createComputePipelineAsync({
  layout: 'auto', compute: { module: computeModule, entryPoint: 'missing' },
}));
await assert.rejects(asyncResult.value, (error) => error.name === 'GPUPipelineError' && error.reason === 'validation');
assert.equal(asyncResult.error, null, 'async pipeline failures do not reach the error scope');
console.log('ok: async pipeline creation rejects with GPUPipelineError');

device.destroy();
