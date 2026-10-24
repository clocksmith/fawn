#!/usr/bin/env node
// WebGPU "validate the encoder state": a command on an ended pass or a finished
// encoder is a validation error at once; one on an encoder with an open pass
// (locked) or a render pass without attachments invalidates the encoder, so
// finish() reports it. Unobserved, the ended-state use throws. Compute pass
// debug markers on a pass that has recorded nothing reach the native pass.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('encoder open state: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!adapter) {
  console.log('encoder open state: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUTextureUsage: T } = globalThis;
const buffer = device.createBuffer({ size: 16, usage: B.COPY_SRC | B.COPY_DST });
const target = device.createTexture({ size: [1, 1], format: 'rgba8unorm', usage: T.RENDER_ATTACHMENT });
const renderPass = (encoder) => encoder.beginRenderPass({
  colorAttachments: [{ view: target.createView(), loadOp: 'clear', storeOp: 'store' }],
});

async function scoped(fn) {
  device.pushErrorScope('validation');
  fn();
  return device.popErrorScope();
}

{
  const encoder = device.createCommandEncoder();
  const pass = renderPass(encoder);
  pass.end();
  assert.ok(await scoped(() => pass.draw(1)), 'draw on an ended render pass');
  assert.ok(await scoped(() => pass.end()), 'end of an ended render pass');
  const compute = encoder.beginComputePass();
  compute.end();
  assert.ok(await scoped(() => compute.dispatchWorkgroups(1)), 'dispatch on an ended compute pass');
  assert.equal(await scoped(() => encoder.finish()), null);
  assert.ok(await scoped(() => encoder.clearBuffer(buffer)), 'clearBuffer on a finished encoder');
  assert.ok(await scoped(() => encoder.finish()), 'finish of a finished encoder');
  assert.throws(() => pass.draw(1), /render pass is already ended/);
  console.log('ok: commands on ended passes and finished encoders are validation errors');
}

{
  const encoder = device.createCommandEncoder();
  const pass = renderPass(encoder);
  assert.equal(await scoped(() => encoder.clearBuffer(buffer)), null);
  pass.end();
  const error = await scoped(() => encoder.finish());
  assert.match(error?.message ?? '', /active pass/);
  console.log('ok: an encoder command during an open pass invalidates the encoder');
}

{
  const encoder = device.createCommandEncoder();
  assert.equal(await scoped(() => encoder.beginRenderPass({ colorAttachments: [] }).end()), null);
  const error = await scoped(() => encoder.finish());
  assert.match(error?.message ?? '', /color attachment or a depthStencilAttachment/);
  console.log('ok: a render pass without attachments invalidates the encoder');
}

{
  const encoder = device.createCommandEncoder();
  const error = await scoped(() => {
    const pass = encoder.beginComputePass();
    pass.pushDebugGroup('group');
    pass.insertDebugMarker('marker');
    pass.popDebugGroup();
    pass.end();
    device.queue.submit([encoder.finish()]);
  });
  assert.equal(error, null);
  console.log('ok: compute pass debug markers record');
}
device.destroy();
