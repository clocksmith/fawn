#!/usr/bin/env node
// A stencil aspect binds as a texture with sampleType 'uint': a stencil-only
// view of depth24plus-stencil8, and a stencil8 texture. A depth-only view does
// not bind as 'uint'.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('stencil binding: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!adapter) {
  console.log('stencil binding: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUTextureUsage: T, GPUShaderStage: S } = globalThis;
const layout = device.createBindGroupLayout({ entries: [{ binding: 0, visibility: S.FRAGMENT, texture: { sampleType: 'uint' } }] });

async function bind(format, aspect) {
  const texture = device.createTexture({ size: [4, 4], format, usage: T.TEXTURE_BINDING | T.RENDER_ATTACHMENT });
  device.pushErrorScope('validation');
  let thrown = null;
  try {
    device.createBindGroup({ layout, entries: [{ binding: 0, resource: texture.createView({ aspect }) }] });
  } catch (error) {
    thrown = error;
  }
  const scoped = await device.popErrorScope();
  return thrown ?? scoped;
}

assert.equal(await bind('depth24plus-stencil8', 'stencil-only'), null);
assert.equal(await bind('stencil8', 'all'), null);
console.log('ok: stencil aspects bind with sampleType uint');
assert.ok(await bind('depth32float', 'depth-only'), 'a depth aspect is not uint');
console.log('ok: a depth aspect does not bind as uint');
device.destroy();
