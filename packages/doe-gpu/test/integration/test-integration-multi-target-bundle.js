#!/usr/bin/env node
// A render bundle whose layout names several color slots (a null slot in the
// middle, and a trailing null slot, which layout equality ignores) draws into
// every target of a matching pass. A bundle whose layout differs from the
// pass, or that is not depth read-only in a depth read-only pass, is a
// validation error (https://gpuweb.github.io/gpuweb/#dom-gpurenderpassencoder-executebundles).

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('multi-target bundle: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage, GPUMapMode, GPUTextureUsage, GPUValidationError } = target;

const module = device.createShaderModule({
  code: `
    struct Out { @location(0) a: vec4f, @location(2) b: vec4f }
    @vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
      let corners = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
      return vec4f(corners[i], 0.0, 1.0);
    }
    @fragment fn fs() -> Out { return Out(vec4f(1.0, 0.0, 0.0, 1.0), vec4f(0.0, 0.0, 1.0, 1.0)); }
  `,
});
const formats = ['rgba8unorm', null, 'rgba8unorm'];
const pipeline = device.createRenderPipeline({
  layout: 'auto',
  vertex: { module, entryPoint: 'vs' },
  fragment: { module, entryPoint: 'fs', targets: formats.map((format) => (format ? { format } : null)) },
});
const textures = [0, 2].map(() => device.createTexture({
  size: [2, 2, 1],
  format: 'rgba8unorm',
  usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
}));

function bundle(colorFormats, extra = {}) {
  const encoder = device.createRenderBundleEncoder({ colorFormats, ...extra });
  encoder.setPipeline(pipeline);
  encoder.draw(3);
  return encoder.finish();
}

function pass(encoder, depthStencilAttachment = undefined) {
  return encoder.beginRenderPass({
    colorAttachments: [
      { view: textures[0].createView(), clearValue: [0, 0, 0, 0], loadOp: 'clear', storeOp: 'store' },
      null,
      { view: textures[1].createView(), clearValue: [0, 0, 0, 0], loadOp: 'clear', storeOp: 'store' },
    ],
    depthStencilAttachment,
  });
}

async function firstPixel(texture) {
  const buffer = device.createBuffer({ size: 512, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const encoder = device.createCommandEncoder();
  encoder.copyTextureToBuffer({ texture }, { buffer, bytesPerRow: 256 }, [2, 2, 1]);
  device.queue.submit([encoder.finish()]);
  await buffer.mapAsync(GPUMapMode.READ);
  const pixel = Array.from(new Uint8Array(buffer.getMappedRange(), 0, 4));
  buffer.unmap();
  return pixel;
}

// Matching layout, with a trailing null slot the comparison ignores.
device.pushErrorScope('validation');
const encoder = device.createCommandEncoder();
const renderPass = pass(encoder);
renderPass.executeBundles([bundle([...formats, null])]);
renderPass.end();
device.queue.submit([encoder.finish()]);
assert.equal(await device.popErrorScope(), null, 'a bundle with the pass layout executes without errors');
assert.deepEqual(await firstPixel(textures[0]), [255, 0, 0, 255], 'the bundle draws into color slot 0');
assert.deepEqual(await firstPixel(textures[1]), [0, 0, 255, 255], 'the bundle draws into color slot 2');

async function executeError(bundleToRun, depthStencilAttachment) {
  device.pushErrorScope('validation');
  const commands = device.createCommandEncoder();
  const p = pass(commands, depthStencilAttachment);
  p.executeBundles([bundleToRun]);
  p.end();
  commands.finish();
  return device.popErrorScope();
}
const mismatch = await executeError(bundle(['rgba8unorm']));
assert(mismatch instanceof GPUValidationError, 'a bundle whose color slots differ from the pass is a validation error');
const shifted = await executeError(bundle(['rgba8unorm', 'rgba8unorm']));
assert(shifted instanceof GPUValidationError, 'slots are compared position by position, null slots included');

// Depth read-only pass: the bundle must be depth read-only too.
const depth = device.createTexture({ size: [2, 2, 1], format: 'depth32float', usage: GPUTextureUsage.RENDER_ATTACHMENT });
const readOnly = { view: depth.createView(), depthReadOnly: true };
const depthPipelineBundle = (depthReadOnly) => {
  const depthPipeline = device.createRenderPipeline({
    layout: 'auto',
    vertex: { module, entryPoint: 'vs' },
    fragment: { module, entryPoint: 'fs', targets: formats.map((format) => (format ? { format } : null)) },
    depthStencil: { format: 'depth32float', depthWriteEnabled: false, depthCompare: 'always' },
  });
  const bundleEncoder = device.createRenderBundleEncoder({ colorFormats: formats, depthStencilFormat: 'depth32float', depthReadOnly });
  bundleEncoder.setPipeline(depthPipeline);
  bundleEncoder.draw(3);
  return bundleEncoder.finish();
};
assert.equal(await executeError(depthPipelineBundle(true), readOnly), null, 'a depth read-only bundle runs in a depth read-only pass');
const writable = await executeError(depthPipelineBundle(false), readOnly);
assert(writable instanceof GPUValidationError, 'a bundle that may write depth is a validation error in a depth read-only pass');
console.log('ok: multi-target bundles draw every slot and must match the pass layout');
device.destroy();
