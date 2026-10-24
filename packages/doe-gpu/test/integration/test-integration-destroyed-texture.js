#!/usr/bin/env node
// A destroyed texture (WebGPU): createView still succeeds, commands that name
// it still encode and finish, and queue.submit reports a validation error,
// whether the texture was destroyed before encoding or after finish().
// queue.writeTexture to a destroyed texture is a validation error at once.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('destroyed texture: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!adapter) {
  console.log('destroyed texture: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUTextureUsage: T } = globalThis;
const usage = T.COPY_SRC | T.COPY_DST | T.RENDER_ATTACHMENT;
const texture = () => device.createTexture({ size: [4, 4], format: 'rgba8unorm', usage });
const readback = device.createBuffer({ size: 1024, usage: B.COPY_DST });

async function scoped(fn) {
  device.pushErrorScope('validation');
  const value = fn();
  return { value, error: await device.popErrorScope() };
}
function encodeCopy(source) {
  const encoder = device.createCommandEncoder();
  encoder.copyTextureToBuffer({ texture: source }, { buffer: readback, bytesPerRow: 256 }, [4, 4]);
  return encoder.finish();
}
function encodePass(view) {
  const encoder = device.createCommandEncoder();
  encoder.beginRenderPass({ colorAttachments: [{ view, loadOp: 'clear', storeOp: 'store' }] }).end();
  return encoder.finish();
}

// Control: nothing destroyed.
{
  const live = texture();
  assert.equal((await scoped(() => device.queue.submit([encodeCopy(live), encodePass(live.createView())]))).error, null);
  live.destroy();
}

// Destroyed before encoding.
{
  const destroyed = texture();
  destroyed.destroy();
  const view = await scoped(() => destroyed.createView());
  assert.equal(view.error, null, 'createView of a destroyed texture is valid');
  const encoded = await scoped(() => [encodeCopy(destroyed), encodePass(view.value)]);
  assert.equal(encoded.error, null, 'encoding a destroyed texture is valid');
  for (const commandBuffer of encoded.value) {
    const submitted = await scoped(() => device.queue.submit([commandBuffer]));
    assert.ok(submitted.error, 'submitting a destroyed texture is a validation error');
  }
  const write = await scoped(() => device.queue.writeTexture({ texture: destroyed }, new Uint8Array(64), { bytesPerRow: 16 }, [4, 4]));
  assert.ok(write.error, 'writeTexture to a destroyed texture is a validation error');
  console.log('ok: a texture destroyed before encoding fails at submit and in writeTexture');
}

// Destroyed after finish().
{
  const copied = texture();
  const rendered = texture();
  const commandBuffers = [encodeCopy(copied), encodePass(rendered.createView())];
  copied.destroy();
  rendered.destroy();
  for (const commandBuffer of commandBuffers) {
    const submitted = await scoped(() => device.queue.submit([commandBuffer]));
    assert.ok(submitted.error, 'a texture destroyed after finish fails at submit');
  }
  console.log('ok: a texture destroyed after finish fails at submit');
}
device.destroy();
