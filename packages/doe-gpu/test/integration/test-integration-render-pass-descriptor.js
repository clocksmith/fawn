#!/usr/bin/env node
// GPURenderPassDescriptor valid usage that webgpu.h cannot carry to a
// provider, and query sets checked by the provider: each invalid descriptor
// invalidates the encoder, so finish() reports one validation error and
// beginRenderPass itself does not throw
// (https://gpuweb.github.io/gpuweb/#render-pass-descriptor-valid-usage).

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('render pass descriptor: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const timestamps = adapter.features.has('timestamp-query');
const device = await adapter.requestDevice({ requiredFeatures: timestamps ? ['timestamp-query'] : [] });
const { GPUTextureUsage, GPUValidationError } = target;
const color = () => device.createTexture({ size: [1, 1, 1], format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT }).createView();
const depth = () => device.createTexture({ size: [1, 1, 1], format: 'depth32float', usage: GPUTextureUsage.RENDER_ATTACHMENT }).createView();
const attachment = () => ({ view: color(), loadOp: 'clear', storeOp: 'store' });

async function finishError(descriptor) {
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  encoder.beginRenderPass(descriptor).end();
  encoder.finish();
  return device.popErrorScope();
}

const occlusion = device.createQuerySet({ type: 'occlusion', count: 2 });
// A 4x4x4 3d texture: its mip level 1 is two slices deep.
const volume = device.createTexture({ dimension: '3d', size: [4, 4, 4], mipLevelCount: 2, format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT });
const slice = (depthSlice, baseMipLevel = 0) => ({ view: volume.createView({ baseMipLevel, mipLevelCount: 1 }), depthSlice, loadOp: 'clear', storeOp: 'store' });
const cases = [
  ['a valid pass', { colorAttachments: [attachment()] }, true],
  ['more color slots than maxColorAttachments', { colorAttachments: Array.from({ length: device.limits.maxColorAttachments + 1 }, attachment) }, false],
  ['depthSlice on a 2d view', { colorAttachments: [{ ...attachment(), depthSlice: 0 }] }, false],
  ['a 3d view without depthSlice', { colorAttachments: [{ ...slice(0), depthSlice: undefined }] }, false],
  ['the last depth slice of mip level 1', { colorAttachments: [slice(1, 1)] }, true],
  ['a depth slice past mip level 1', { colorAttachments: [slice(2, 1)] }, false],
  ['two depth slices of one texture', { colorAttachments: [slice(0), slice(3)] }, true],
  ['one depth slice twice', { colorAttachments: [slice(1), slice(1)] }, false],
  ['a cleared depth aspect without depthClearValue', {
    colorAttachments: [],
    depthStencilAttachment: { view: depth(), depthLoadOp: 'clear', depthStoreOp: 'store' },
  }, false],
  ['an occlusion query set', { colorAttachments: [attachment()], occlusionQuerySet: occlusion }, true],
  ['timestampWrites with an occlusion query set', { colorAttachments: [attachment()], timestampWrites: { querySet: occlusion, beginningOfPassWriteIndex: 0 } }, false],
];
if (timestamps) {
  const timestampSet = device.createQuerySet({ type: 'timestamp', count: 2 });
  cases.push(
    ['timestampWrites with no index', { colorAttachments: [attachment()], timestampWrites: { querySet: timestampSet } }, false],
    ['timestampWrites with one index', { colorAttachments: [attachment()], timestampWrites: { querySet: timestampSet, endOfPassWriteIndex: 1 } }, true],
    ['an occlusionQuerySet of timestamp type', { colorAttachments: [attachment()], occlusionQuerySet: timestampSet }, false],
  );
}
for (const [label, descriptor, valid] of cases) {
  const error = await finishError(descriptor);
  if (valid) assert.equal(error, null, `${label} is valid`);
  else assert(error instanceof GPUValidationError, `${label} is a validation error at finish()`);
}

// A pass begun on an encoder an earlier pass invalidated still records
// nothing and reports once.
device.pushErrorScope('validation');
const encoder = device.createCommandEncoder();
encoder.beginRenderPass({ colorAttachments: [attachment()], occlusionQuerySet: occlusion, timestampWrites: { querySet: occlusion, beginningOfPassWriteIndex: 0 } }).end();
encoder.beginRenderPass({ colorAttachments: [attachment()] }).end();
encoder.beginComputePass().end();
encoder.finish();
assert(await device.popErrorScope() instanceof GPUValidationError, 'passes on an invalid encoder report one error at finish()');
console.log(`ok: render pass descriptors are validated (timestamp cases ${timestamps ? 'run' : 'skipped'})`);
device.destroy();
