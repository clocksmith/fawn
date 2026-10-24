#!/usr/bin/env node
// A render bundle starts with empty state: a vertex buffer the pass set before
// executeBundles does not satisfy a bundle draw whose pipeline reads slot 0,
// so the encoding reports a validation error. A bundle that sets its own
// vertex buffer encodes and submits cleanly.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('bundle state: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!adapter) {
  console.log('bundle state: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUTextureUsage: T } = globalThis;
const module = device.createShaderModule({ code: `
  @vertex fn vs(@location(0) p: vec2f) -> @builtin(position) vec4f { return vec4f(p, 0.0, 1.0); }
  @fragment fn fs() -> @location(0) vec4f { return vec4f(1.0); }
` });
const pipeline = device.createRenderPipeline({
  layout: 'auto',
  vertex: { module, entryPoint: 'vs', buffers: [{ arrayStride: 8, attributes: [{ shaderLocation: 0, format: 'float32x2', offset: 0 }] }] },
  fragment: { module, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] },
});
const vertices = device.createBuffer({ size: 24, usage: B.VERTEX });
const target = device.createTexture({ size: [4, 4], format: 'rgba8unorm', usage: T.RENDER_ATTACHMENT });

function bundle(setsVertexBuffer) {
  const encoder = device.createRenderBundleEncoder({ colorFormats: ['rgba8unorm'] });
  encoder.setPipeline(pipeline);
  if (setsVertexBuffer) encoder.setVertexBuffer(0, vertices);
  encoder.draw(3);
  return encoder.finish();
}

async function encode(setsVertexBuffer) {
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({ colorAttachments: [{ view: target.createView(), loadOp: 'clear', storeOp: 'store' }] });
  pass.setVertexBuffer(0, vertices);
  pass.executeBundles([bundle(setsVertexBuffer)]);
  pass.end();
  device.queue.submit([encoder.finish()]);
  return device.popErrorScope();
}

assert.equal(await encode(true), null);
console.log('ok: a bundle that sets its own vertex buffer encodes cleanly');
assert.ok(await encode(false), 'the pass vertex buffer must not reach the bundle draw');
console.log('ok: bundle draws do not inherit the pass vertex buffer');
device.destroy();
