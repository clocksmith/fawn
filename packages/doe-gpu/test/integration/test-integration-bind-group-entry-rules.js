#!/usr/bin/env node
// createBindGroup validates each entry's resource against its layout binding
// (https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbindgroup): buffer
// usage, offset alignment, range, minBindingSize and storage size; texture
// usage, sample type, dimension and multisampling; storage texture format and
// mip count; sampler type.

import assert from 'node:assert/strict';
import { providerInfo, setupGlobals } from '../../src/index.js';

setupGlobals();
const adapter = await navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('bind group entry rules: skipped (no adapter)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUShaderStage: S } = globalThis;
const visibility = S.COMPUTE;

async function rejected(layoutEntry, resource) {
  const layout = device.createBindGroupLayout({ entries: [{ binding: 0, visibility, ...layoutEntry }] });
  device.pushErrorScope('validation');
  device.createBindGroup({ layout, entries: [{ binding: 0, resource }] });
  return (await device.popErrorScope()) !== null;
}
async function expect(name, layoutEntry, resource, error) {
  assert.equal(await rejected(layoutEntry, resource), error, name);
}

const uniformAlign = device.limits.minUniformBufferOffsetAlignment;
const storageAlign = device.limits.minStorageBufferOffsetAlignment;
const uniformBuffer = device.createBuffer({ size: 4 * uniformAlign, usage: B.UNIFORM });
const storageBuffer = device.createBuffer({ size: 4 * storageAlign, usage: B.STORAGE });
const uniform = { buffer: { type: 'uniform' } };
const storage = { buffer: { type: 'storage' } };
await expect('uniform buffer', uniform, { buffer: uniformBuffer }, false);
await expect('uniform layout, storage-only buffer', uniform, { buffer: storageBuffer }, true);
await expect('storage layout, uniform-only buffer', storage, { buffer: uniformBuffer }, true);
await expect('aligned uniform offset', uniform, { buffer: uniformBuffer, offset: uniformAlign }, false);
await expect('unaligned uniform offset', uniform, { buffer: uniformBuffer, offset: uniformAlign / 2 }, true);
await expect('unaligned storage offset', storage, { buffer: storageBuffer, offset: storageAlign / 2 }, true);
await expect('range past the buffer', uniform, { buffer: uniformBuffer, offset: uniformAlign, size: 4 * uniformAlign }, true);
await expect('empty range at the end', uniform, { buffer: uniformBuffer, offset: 4 * uniformAlign }, true);
await expect('storage size not a multiple of 4', storage, { buffer: storageBuffer, size: 6 }, true);
await expect('range below minBindingSize', { buffer: { type: 'uniform', minBindingSize: 32 } }, { buffer: uniformBuffer, size: 16 }, true);
await expect('range at minBindingSize', { buffer: { type: 'uniform', minBindingSize: 32 } }, { buffer: uniformBuffer, size: 32 }, false);

const sampled = device.createTexture({ size: [4, 4], format: 'rgba8unorm', usage: T.TEXTURE_BINDING | T.STORAGE_BINDING });
const copyOnly = device.createTexture({ size: [4, 4], format: 'rgba8unorm', usage: T.COPY_DST });
const r32 = device.createTexture({ size: [4, 4], format: 'r32float', usage: T.TEXTURE_BINDING });
const uint = device.createTexture({ size: [4, 4], format: 'r32uint', usage: T.TEXTURE_BINDING });
const mips = device.createTexture({ size: [4, 4], format: 'rgba8unorm', mipLevelCount: 2, usage: T.STORAGE_BINDING });
const layered = device.createTexture({ size: [4, 4, 2], format: 'rgba8unorm', usage: T.TEXTURE_BINDING });
const multisampled = device.createTexture({ size: [4, 4], format: 'rgba8unorm', sampleCount: 4, usage: T.TEXTURE_BINDING | T.RENDER_ATTACHMENT });
const float = { texture: { sampleType: 'float' } };
await expect('sampled texture', float, sampled.createView(), false);
await expect('texture without TEXTURE_BINDING', float, copyOnly.createView(), true);
await expect('uint texture as float', float, uint.createView(), true);
await expect('r32float as unfilterable-float', { texture: { sampleType: 'unfilterable-float' } }, r32.createView(), false);
await expect('r32float as float without float32-filterable', float, r32.createView(), true);
await expect('2d-array view for a 2d binding', float, layered.createView(), true);
await expect('2d-array view for a 2d-array binding', { texture: { sampleType: 'float', viewDimension: '2d-array' } }, layered.createView(), false);
await expect('multisampled texture for a single-sample binding', float, multisampled.createView(), true);
await expect('multisampled binding', { texture: { sampleType: 'unfilterable-float', multisampled: true } }, multisampled.createView(), false);
const storageTexture = { storageTexture: { access: 'write-only', format: 'rgba8unorm' } };
await expect('storage texture', storageTexture, sampled.createView(), false);
await expect('storage texture of another format', { storageTexture: { access: 'write-only', format: 'r32float' } }, sampled.createView(), true);
await expect('storage texture view with two mips', storageTexture, mips.createView(), true);
await expect('storage texture without STORAGE_BINDING', storageTexture, copyOnly.createView(), true);

const filtering = device.createSampler({ magFilter: 'linear' });
const nearest = device.createSampler();
const comparison = device.createSampler({ compare: 'less' });
await expect('filtering sampler', { sampler: { type: 'filtering' } }, filtering, false);
await expect('linear sampler for non-filtering', { sampler: { type: 'non-filtering' } }, filtering, true);
await expect('nearest sampler for non-filtering', { sampler: { type: 'non-filtering' } }, nearest, false);
await expect('comparison sampler for filtering', { sampler: { type: 'filtering' } }, comparison, true);
await expect('comparison sampler', { sampler: { type: 'comparison' } }, comparison, false);
await expect('plain sampler for comparison', { sampler: { type: 'comparison' } }, nearest, true);

console.log(`ok: createBindGroup checks buffer, texture, storage texture and sampler entries against their layout (${providerInfo().libraryFlavor ?? 'provider'})`);
device.destroy();
