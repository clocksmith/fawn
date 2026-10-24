#!/usr/bin/env node
// An externalTexture bind group layout entry accepts a GPUTextureView
// (https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbindgroup): a 2d view
// with one mip level and array layer, TEXTURE_BINDING usage, a single sample
// and an rgba8unorm, bgra8unorm or rgba16float format. Other resources and
// views are validation errors.

import assert from 'node:assert/strict';
import { providerInfo, setupGlobals } from '../../src/index.js';

setupGlobals();
const adapter = await navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('external texture view: skipped (no adapter)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUShaderStage: S } = globalThis;

async function validationError(action) {
  device.pushErrorScope('validation');
  const value = action();
  return { value, error: await device.popErrorScope() };
}

const created = await validationError(() => device.createBindGroupLayout({ entries: [{ binding: 0, visibility: S.FRAGMENT, externalTexture: {} }] }));
assert.equal(created.error, null, 'externalTexture layout');
const layout = created.value;
const texture = (format, usage = T.TEXTURE_BINDING, { layers = 1, mips = 1, samples = 1 } = {}) =>
  device.createTexture({ size: [16, 16, layers], format, usage, mipLevelCount: mips, sampleCount: samples });
async function binds(name, resource, valid) {
  const { error } = await validationError(() => device.createBindGroup({ layout, entries: [{ binding: 0, resource }] }));
  assert.equal(error === null, valid, name);
}

await binds('rgba8unorm view', texture('rgba8unorm').createView(), true);
await binds('bgra8unorm view', texture('bgra8unorm').createView(), true);
await binds('rgba16float view', texture('rgba16float').createView(), true);
await binds('rgba8unorm texture (default view)', texture('rgba8unorm'), true);
await binds('r8unorm view', texture('r8unorm').createView(), false);
await binds('view without TEXTURE_BINDING', texture('rgba8unorm', T.COPY_DST).createView(), false);
await binds('two-mip view', texture('rgba8unorm', T.TEXTURE_BINDING, { mips: 2 }).createView(), false);
await binds('one mip of a two-mip texture', texture('rgba8unorm', T.TEXTURE_BINDING, { mips: 2 }).createView({ mipLevelCount: 1 }), true);
await binds('2d-array view', texture('rgba8unorm', T.TEXTURE_BINDING, { layers: 2 }).createView(), false);
await binds('multisampled view', texture('rgba8unorm', T.TEXTURE_BINDING | T.RENDER_ATTACHMENT, { samples: 4 }).createView(), false);
await binds('buffer', { buffer: device.createBuffer({ size: 16, usage: B.UNIFORM }) }, false);
await binds('sampler', device.createSampler(), false);
console.log(`ok: an externalTexture entry binds a 2d single-mip rgba8unorm/bgra8unorm/rgba16float view (${providerInfo().libraryFlavor ?? 'provider'})`);
device.destroy();
