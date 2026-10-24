#!/usr/bin/env node
// WebGPU interface globals, forceFallbackAdapter semantics, and createTexture
// error classification, as WebGPU code (and the CTS) observe them.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
const adapter = await target.navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('webgpu globals: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
for (const name of ['GPU', 'GPUAdapter', 'GPUDevice', 'GPUQueue', 'GPUBuffer', 'GPUTexture', 'GPUTextureView',
  'GPUSampler', 'GPUBindGroupLayout', 'GPUBindGroup', 'GPUPipelineLayout', 'GPUShaderModule', 'GPUComputePipeline',
  'GPURenderPipeline', 'GPUQuerySet', 'GPUCommandEncoder', 'GPUCommandBuffer', 'GPUComputePassEncoder',
  'GPURenderPassEncoder', 'GPURenderBundleEncoder', 'GPURenderBundle', 'GPUError', 'GPUValidationError',
  'GPUOutOfMemoryError', 'GPUInternalError', 'GPUPipelineError']) {
  assert.equal(typeof target[name], 'function', `${name} is installed`);
}
const encoder = device.createCommandEncoder();
const pass = encoder.beginComputePass();
assert(target.navigator.gpu instanceof target.GPU && adapter instanceof target.GPUAdapter);
assert(device instanceof target.GPUDevice && device.queue instanceof target.GPUQueue);
assert(encoder instanceof target.GPUCommandEncoder && pass instanceof target.GPUComputePassEncoder);
pass.end();
assert(encoder.finish() instanceof target.GPUCommandBuffer);
console.log('ok: WebGPU interface globals match the objects the package returns');

const fallback = await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
assert(fallback === null || fallback.info.isFallbackAdapter === true,
  'forceFallbackAdapter resolves to a fallback adapter or null');
console.log(`ok: forceFallbackAdapter resolves to ${fallback ? 'a fallback adapter' : 'null'}`);
fallback?.destroy?.();

device.pushErrorScope('out-of-memory');
device.pushErrorScope('validation');
device.createTexture({ size: { width: 4, height: 4 }, format: 'rgba8unorm', usage: 0 });
const validation = await device.popErrorScope();
const outOfMemory = await device.popErrorScope();
assert(validation instanceof target.GPUValidationError, 'an invalid texture descriptor is a validation error');
assert.equal(outOfMemory, null, 'an invalid texture descriptor is not an out-of-memory error');
console.log('ok: createTexture reports descriptor errors as validation errors');
device.destroy();
