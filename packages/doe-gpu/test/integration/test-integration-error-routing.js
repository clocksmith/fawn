#!/usr/bin/env node
// Error routing has one ordered owner: provider errors reach the package
// through the native uncaptured-error callback and are delivered to the
// package's error scopes together with package-detected errors, in call
// order. Each section covers one review finding on 3c53dabc8/afc744561:
// finish() reports once (1), scopes keep the first error (2), deep scope
// stacks (3), lost devices discard errors (5), WebIDL failures throw TypeError
// (6), provider-reported objects are invalid (7), finish() never throws and
// listeners follow EventTarget rules (8).

import assert from 'node:assert/strict';
import { requestNativeDeviceOrSkip } from './native-device-test-helper.js';

const device = await requestNativeDeviceOrSkip('error routing');
const { GPUBufferUsage, GPUTextureUsage, GPUValidationError } = globalThis;

const a = device.createBuffer({ size: 64, usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
const b = device.createBuffer({ size: 64, usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
const BAD_WGSL = '@compute @workgroup_size(1) fn main() { let x: u32 = 1.5; }';

async function popAfter(action) {
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
const invalidBuffer = (await popAfter(() => device.createBuffer({ size: 16, usage: 0 }))).value;

async function eventsDuring(action) {
  const events = [];
  const listener = (event) => events.push(event.error);
  device.addEventListener('uncapturederror', listener);
  let value;
  try {
    value = action();
    await device.queue.onSubmittedWorkDone();
    await new Promise((resolve) => setTimeout(resolve, 20));
  } finally {
    device.removeEventListener('uncapturederror', listener);
  }
  return { value, events };
}

// 1. One error per finish(), whatever else invalidated the encoder.
for (const [name, record] of [
  ['misaligned copy then invalid-object copy', (encoder) => {
    encoder.copyBufferToBuffer(a, 0, b, 0, 3);
    encoder.copyBufferToBuffer(invalidBuffer, 0, b, 0, 16);
  }],
  ['invalid-object copy alone', (encoder) => encoder.copyBufferToBuffer(invalidBuffer, 0, b, 0, 16)],
  ['render pass without RENDER_ATTACHMENT then invalid-object copy', (encoder) => {
    const sampled = device.createTexture({ size: [4, 4], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING });
    const pass = encoder.beginRenderPass({
      colorAttachments: [{ view: sampled.createView(), loadOp: 'clear', storeOp: 'store' }],
    });
    pass.end();
    encoder.copyBufferToBuffer(invalidBuffer, 0, b, 0, 16);
  }],
]) {
  const { value, events } = await eventsDuring(() => {
    const encoder = device.createCommandEncoder();
    record(encoder);
    return encoder.finish();
  });
  assert.equal(events.length, 1, `${name}: finish() reports one error, got ${events.length}`);
  assert.equal(value._invalid, true, `${name}: finish() returns an invalid command buffer`);
}
console.log('ok: finish() reports exactly one error');

// 2. A scope keeps the first error across provider and package errors.
const misalignedSubmit = () => {
  const encoder = device.createCommandEncoder();
  encoder.copyBufferToBuffer(a, 0, b, 0, 3);
  device.queue.submit([encoder.finish()]);
};
const packageError = () => device.createSampler({ lodMinClamp: 5, lodMaxClamp: 1 });
const providerFirst = await popAfter(() => {
  misalignedSubmit();
  packageError();
});
assert(providerFirst.error instanceof GPUValidationError, 'the scope captured an error');
assert(!providerFirst.error.message.includes('lodMinClamp'), `the earlier provider error wins: ${providerFirst.error.message}`);
const packageFirst = await popAfter(() => {
  packageError();
  misalignedSubmit();
});
assert(packageFirst.error.message.includes('lodMinClamp'), `the earlier package error wins: ${packageFirst.error.message}`);
console.log('ok: scopes keep the first error in call order');

// 3. Deep scope stacks: the package pushes no native scopes of its own.
for (const depth of [62, 63, 100]) {
  for (let index = 0; index < depth; index += 1) device.pushErrorScope('out-of-memory');
  device.pushErrorScope('validation');
  device.createShaderModule({ code: BAD_WGSL });
  const popped = [];
  for (let index = 0; index <= depth; index += 1) popped.push(await device.popErrorScope());
  assert(popped[0] instanceof GPUValidationError, `depth ${depth + 1}: the validation scope captured the error`);
  assert(popped.slice(1).every((error) => error === null), `depth ${depth + 1}: outer scopes stay empty`);
}
console.log('ok: scope stacks of any depth keep their errors');

// 5. A lost device discards errors.
{
  const adapter = await navigator.gpu.requestAdapter();
  const lost = await adapter.requestDevice();
  const events = [];
  lost.addEventListener('uncapturederror', (event) => events.push(event.error));
  lost.destroy();
  const sampler = lost.createSampler();
  const buffer = lost.createBuffer({ size: 4, usage: GPUBufferUsage.COPY_DST });
  await new Promise((resolve) => setTimeout(resolve, 20));
  assert.equal(events.length, 0, 'no uncapturederror after device loss');
  assert.equal(sampler._invalid, true, 'creation on a lost device returns an invalid object');
  assert.equal(buffer._invalid, true, 'a buffer on a lost device is invalid');
}
console.log('ok: a lost device discards errors');

// 6. WebIDL conversion failures throw TypeError even inside a scope.
for (const [name, action] of [
  ['createBindGroup without a layout', () => device.createBindGroup({ entries: [] })],
  ['createTexture(undefined)', () => device.createTexture(undefined)],
  ['createSampler with a non-object descriptor', () => device.createSampler(7)],
]) {
  const { thrown, error } = await popAfter(action);
  assert(thrown instanceof TypeError, `${name} throws TypeError, got ${thrown}`);
  assert.equal(error, null, `${name} reports nothing to the scope`);
}
console.log('ok: WebIDL conversion failures throw TypeError');

// 7. An object the provider reported but returned is invalid.
{
  const { value: module, error } = await popAfter(() => device.createShaderModule({ code: BAD_WGSL }));
  assert(error instanceof GPUValidationError, 'the WGSL error is reported');
  assert.equal(module._invalid, true, 'the module the provider returned is invalid');
  const info = await module.getCompilationInfo();
  assert(info.messages.length > 0, 'its compilation info is still available');
  const pipeline = await popAfter(() => device.createComputePipeline({ layout: 'auto', compute: { module } }));
  assert(pipeline.error instanceof GPUValidationError, 'a pipeline from it is a validation error');
}
console.log('ok: provider-reported objects are invalid');

// 8. finish() never throws; listeners follow EventTarget rules.
{
  const listener = () => {};
  device.addEventListener('uncapturederror', listener);
  const encoder = device.createCommandEncoder();
  encoder.copyBufferToBuffer(invalidBuffer, 0, b, 0, 16);
  device.removeEventListener('uncapturederror', listener);
  const commands = encoder.finish();
  assert.equal(commands._invalid, true, 'finish() without an observer returns an invalid command buffer');

  const seen = [];
  const object = { handleEvent: (event) => seen.push(event.error) };
  device.addEventListener('uncapturederror', object);
  packageError();
  device.removeEventListener('uncapturederror', object);
  assert.equal(seen.length, 1, 'a handleEvent listener object receives the event');

  let calls = 0;
  device.addEventListener('uncapturederror', () => { calls += 1; }, { once: true });
  packageError();
  assert.equal(calls, 1, 'a once listener runs');
  assert.throws(packageError, Error, 'after its single call the once listener is gone, so creation throws again');
}
console.log('ok: finish() never throws and listeners follow EventTarget rules');

device.destroy();
