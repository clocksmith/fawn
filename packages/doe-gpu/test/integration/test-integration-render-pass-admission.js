#!/usr/bin/env node
// Render pass admission the encoder reports at finish():
// - depth/stencil load and store ops are given exactly for each aspect the
//   format has that is not read-only
//   (https://gpuweb.github.io/gpuweb/#abstract-opdef-gpurenderpassdepthstencilattachment-gpurenderpassdepthstencilattachment-valid-usage);
// - setPipeline requires the pipeline's render pass layout (color formats,
//   depth-stencil format, sample count) to equal the pass's, even with no
//   draw, including a depth-only multisampled pass
//   (https://gpuweb.github.io/gpuweb/#dom-gpurendercommandsmixin-setpipeline).

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('render pass admission: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUTextureUsage, GPUValidationError } = target;

function depthView(format, sampleCount = 1) {
  return device.createTexture({ size: [1, 1, 1], format, sampleCount, usage: GPUTextureUsage.RENDER_ATTACHMENT }).createView();
}

async function finishError(encode) {
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  encode(encoder);
  encoder.finish();
  return device.popErrorScope();
}

const ops = { depthLoadOp: 'clear', depthStoreOp: 'store', depthClearValue: 1 };
const stencilOps = { stencilLoadOp: 'clear', stencilStoreOp: 'store' };
for (const [label, format, attachment, valid] of [
  ['both aspects with all ops', 'depth24plus-stencil8', { ...ops, ...stencilOps }, true],
  ['a stencil aspect without stencil ops', 'depth24plus-stencil8', { ...ops }, false],
  ['a read-only depth aspect with depth ops', 'depth32float', { ...ops, depthReadOnly: true }, false],
  ['a read-only depth aspect without depth ops', 'depth32float', { depthReadOnly: true }, true],
  ['stencil ops on a depth-only format', 'depth32float', { ...ops, ...stencilOps }, false],
  ['depth ops on stencil8', 'stencil8', { ...ops, ...stencilOps }, false],
  ['stencil8 with stencil ops only', 'stencil8', { ...stencilOps }, true],
]) {
  const error = await finishError((encoder) => {
    encoder.beginRenderPass({ colorAttachments: [], depthStencilAttachment: { view: depthView(format), ...attachment } }).end();
  });
  if (valid) assert.equal(error, null, `a pass with ${label} is valid`);
  else assert(error instanceof GPUValidationError, `a pass with ${label} is a validation error`);
}
console.log('ok: depth/stencil load and store ops match the format and read-only aspects');

const vertexOnly = device.createShaderModule({ code: '@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0); }' });
function depthPipeline(sampleCount) {
  return device.createRenderPipeline({
    layout: 'auto',
    vertex: { module: vertexOnly, entryPoint: 'vs' },
    depthStencil: { format: 'depth32float', depthWriteEnabled: false, depthCompare: 'always' },
    multisample: { count: sampleCount },
  });
}
for (const [passSamples, pipelineSamples] of [[4, 4], [4, 1], [1, 4]]) {
  const error = await finishError((encoder) => {
    const pass = encoder.beginRenderPass({
      colorAttachments: [],
      depthStencilAttachment: { view: depthView('depth32float', passSamples), depthLoadOp: 'clear', depthStoreOp: 'store', depthClearValue: 1 },
    });
    pass.setPipeline(depthPipeline(pipelineSamples));
    pass.end();
  });
  if (passSamples === pipelineSamples) assert.equal(error, null, `a ${passSamples}x depth-only pass takes a ${pipelineSamples}x pipeline`);
  else assert(error instanceof GPUValidationError, `a ${passSamples}x depth-only pass rejects a ${pipelineSamples}x pipeline at setPipeline`);
}
console.log('ok: setPipeline checks the pipeline against a depth-only pass layout');
device.destroy();
