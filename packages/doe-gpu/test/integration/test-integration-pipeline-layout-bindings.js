#!/usr/bin/env node
// A pipeline with an explicit layout is invalid when its entry points use a
// binding the layout does not have, or has with another resource type
// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-shader-binding).
// A null bind group layout in the pipeline layout has no bindings.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('pipeline layout bindings: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUShaderStage, GPUValidationError } = target;
const stages = GPUShaderStage.COMPUTE | GPUShaderStage.FRAGMENT;
const uniform = device.createBindGroupLayout({ entries: [{ binding: 0, visibility: stages, buffer: { type: 'uniform' } }] });
const texture = device.createBindGroupLayout({ entries: [{ binding: 0, visibility: stages, texture: {} }] });
const declarations = '@group(1) @binding(0) var<uniform> value: vec4f;';
const compute = device.createShaderModule({ code: `${declarations}\n@compute @workgroup_size(1) fn main() { _ = value; }` });
const render = device.createShaderModule({
  code: `${declarations}
    @vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }
    @fragment fn fs() -> @location(0) vec4f { return value; }`,
});

const create = {
  compute: (layout) => device.createComputePipeline({ layout, compute: { module: compute, entryPoint: 'main' } }),
  render: (layout) => device.createRenderPipeline({
    layout,
    vertex: { module: render, entryPoint: 'vs' },
    fragment: { module: render, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] },
  }),
};
const cases = [
  ['a matching layout', [uniform, uniform], true],
  ['a null layout in the used group', [uniform, null], false],
  ['too few groups', [uniform], false],
  ['another resource type', [uniform, texture], false],
];
for (const [kind, createPipeline] of Object.entries(create)) {
  for (const [label, bindGroupLayouts, valid] of cases) {
    device.pushErrorScope('validation');
    createPipeline(device.createPipelineLayout({ bindGroupLayouts }));
    const error = await device.popErrorScope();
    if (valid) assert.equal(error, null, `${kind} pipeline with ${label} is valid`);
    else assert(error instanceof GPUValidationError, `${kind} pipeline with ${label} is a validation error`);
  }
}
console.log('ok: pipelines reject explicit layouts that lack the bindings their entry points use');
device.destroy();
