#!/usr/bin/env node
// Render pipelines and render bundle encoders reject color targets whose
// bytes per sample exceed maxColorAttachmentBytesPerSample
// (https://gpuweb.github.io/gpuweb/#abstract-opdef-calculating-color-attachment-bytes-per-sample):
// each target is aligned to its component alignment, then adds its byte cost.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('color bytes per sample: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUValidationError } = target;
const limit = device.limits.maxColorAttachmentBytesPerSample;
assert.equal(limit, 32, 'the device reports the WebGPU default limit');

// r8unorm (1) + rgba32uint aligned to 4 (4..20) + rgba16uint (20..28) = 28;
// another rgba8unorm (8) makes 36.
const atLimit = ['r8unorm', 'rgba32uint', 'rgba16uint'];
const overLimit = [...atLimit, 'rgba8unorm'];

function fragmentModule(count) {
  const outputs = Array.from({ length: count }, (_, i) => `@location(${i}) o${i}: vec4u`).join(', ');
  const values = Array.from({ length: count }, () => 'vec4u(0u)').join(', ');
  return device.createShaderModule({
    code: `
      struct Out { ${outputs} }
      @vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }
      @fragment fn fs() -> Out { return Out(${values}); }
    `,
  });
}

async function errorFor(create) {
  device.pushErrorScope('validation');
  create();
  return device.popErrorScope();
}

for (const [label, formats, valid] of [['at the limit', atLimit, true], ['over the limit', overLimit, false]]) {
  const bundleError = await errorFor(() => device.createRenderBundleEncoder({ colorFormats: formats }));
  if (valid) assert.equal(bundleError, null, `a bundle encoder with color formats ${label} is valid`);
  else assert(bundleError instanceof GPUValidationError, `a bundle encoder with color formats ${label} is a validation error`);
}

// Pipelines whose shader writes as many targets as the descriptor names; the
// fragment outputs are uint, so float formats are not used here.
const pipelineFormats = (count) => ['rgba32uint', 'rg32uint', 'r32uint'].slice(0, count);
for (const [label, formats, valid] of [
  ['at the limit (rgba32uint + rg32uint = 24)', pipelineFormats(2), true],
  ['over the limit (rgba32uint + rgba32uint + rg32uint = 40)', ['rgba32uint', 'rgba32uint', 'rg32uint'], false],
]) {
  const module = fragmentModule(formats.length);
  const pipelineError = await errorFor(() => device.createRenderPipeline({
    layout: 'auto',
    vertex: { module, entryPoint: 'vs' },
    fragment: { module, entryPoint: 'fs', targets: formats.map((format) => ({ format })) },
  }));
  if (valid) assert.equal(pipelineError, null, `a render pipeline with color targets ${label} is valid`);
  else assert(pipelineError instanceof GPUValidationError, `a render pipeline with color targets ${label} is a validation error`);
}
console.log('ok: color targets over maxColorAttachmentBytesPerSample are validation errors');
device.destroy();
