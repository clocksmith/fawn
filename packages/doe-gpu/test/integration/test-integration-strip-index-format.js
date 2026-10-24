#!/usr/bin/env node
// primitive.stripIndexFormat reaches the provider: WebGPU requires a strip
// pipeline's indexed draws to use its declared index format, so a provider that
// never saw the format rejects every such draw at finish.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('strip index format: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage, GPUTextureUsage } = target;
const pipeline = device.createRenderPipeline({
  layout: 'auto',
  vertex: {
    module: device.createShaderModule({
      code: '@vertex fn main() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }',
    }),
    entryPoint: 'main',
  },
  fragment: {
    module: device.createShaderModule({
      code: '@fragment fn main() -> @location(0) vec4f { return vec4f(0.0, 1.0, 0.0, 1.0); }',
    }),
    entryPoint: 'main',
    targets: [{ format: 'rgba8unorm' }],
  },
  primitive: { topology: 'triangle-strip', stripIndexFormat: 'uint32' },
});
const indices = device.createBuffer({ size: 24, usage: GPUBufferUsage.INDEX });
const view = device.createTexture({
  size: [1, 1, 1],
  format: 'rgba8unorm',
  usage: GPUTextureUsage.RENDER_ATTACHMENT,
}).createView();

async function drawIndexed(format) {
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({
    colorAttachments: [{ view, clearValue: [0, 0, 0, 1], loadOp: 'clear', storeOp: 'store' }],
  });
  pass.setPipeline(pipeline);
  pass.setIndexBuffer(indices, format);
  pass.drawIndexed(6);
  pass.end();
  encoder.finish();
  return device.popErrorScope();
}

assert.equal(await drawIndexed('uint32'), null, 'a strip pipeline draws with its declared index format');
console.log('ok: strip pipelines draw indexed with their declared index format');
device.destroy();
