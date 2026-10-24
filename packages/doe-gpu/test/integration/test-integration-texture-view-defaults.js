#!/usr/bin/env node
// GPUTexture.createView resolves its defaults per WebGPU: a 2d texture with
// several layers views as 2d-array, and a single-aspect view of a combined
// depth/stencil format takes that aspect's format. Checked on every provider.

import assert from 'node:assert/strict';
import { requestNativeDeviceOrSkip } from './native-device-test-helper.js';

const device = await requestNativeDeviceOrSkip('texture view defaults');
const { GPUTextureUsage: T } = globalThis;

async function noValidationError(label, create) {
  device.pushErrorScope('validation');
  const value = create();
  const error = await device.popErrorScope();
  assert.equal(error, null, `${label}: ${error?.message}`);
  return value;
}

{
  // The default view of a two-layer texture binds where a 2d-array view is
  // required (an explicit layout, so no shader front end is involved).
  const texture = device.createTexture({ size: [1, 1, 2], format: 'r32float', usage: T.TEXTURE_BINDING });
  const layout = device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: 'unfilterable-float', viewDimension: '2d-array' } },
  ] });
  const view = texture.createView();
  await noValidationError('2d-array default view binds', () => device.createBindGroup({ layout, entries: [{ binding: 0, resource: view }] }));
  // An explicit 2d view of the same texture covers one layer by default.
  await noValidationError('explicit 2d view defaults to one layer', () => texture.createView({ dimension: '2d' }));
  console.log('ok: a multi-layer 2d texture views as 2d-array by default');
}

{
  const texture = device.createTexture({ size: [1, 1], format: 'depth24plus-stencil8', usage: T.TEXTURE_BINDING | T.RENDER_ATTACHMENT });
  const layout = device.createBindGroupLayout({ entries: [
    { binding: 0, visibility: GPUShaderStage.COMPUTE, texture: { sampleType: 'depth' } },
  ] });
  const depth = await noValidationError('default depth-only view', () => texture.createView({ aspect: 'depth-only' }));
  await noValidationError('default stencil-only view', () => texture.createView({ aspect: 'stencil-only' }));
  await noValidationError('depth-only view binds as depth', () => device.createBindGroup({ layout, entries: [
    { binding: 0, resource: depth }] }));
  await noValidationError('explicit aspect formats', () => {
    texture.createView({ aspect: 'depth-only', format: 'depth24plus' });
    texture.createView({ aspect: 'stencil-only', format: 'stencil8' });
  });
  assert.throws(() => texture.createView({ aspect: 'depth-only', format: 'depth24plus-stencil8' }),
    /depth-only format/);
  console.log('ok: single-aspect views take the aspect format');
}

device.destroy();
