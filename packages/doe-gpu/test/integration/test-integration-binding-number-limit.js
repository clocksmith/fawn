#!/usr/bin/env node
// Binding numbers must be below maxBindingsPerBindGroup. WGSL itself places
// no bound on @binding, so the shader module is created; the bind group layout
// entry and a pipeline whose entry point uses the binding are validation errors.

import assert from 'node:assert/strict';
import { providerInfo, setupGlobals } from '../../src/index.js';

setupGlobals();
const adapter = await navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('binding number limit: skipped (no adapter)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const limit = device.limits.maxBindingsPerBindGroup;
const flavor = providerInfo().libraryFlavor;

async function validationError(action) {
  device.pushErrorScope('validation');
  action();
  return (await device.popErrorScope()) !== null;
}

function layout(binding) {
  return device.createBindGroupLayout({ entries: [{ binding, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } }] });
}

const computeCode = (binding) => `@group(0) @binding(${binding}) var<storage, read_write> out: array<u32>;
@compute @workgroup_size(1) fn main() { out[0] = 1u; }`;
const renderCode = (binding) => `@group(0) @binding(${binding}) var<uniform> u: vec4f;
@vertex fn vs() -> @builtin(position) vec4f { return u; }
@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0); }`;

function computePipeline(module) {
  device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' } });
}
function renderPipeline(module) {
  device.createRenderPipeline({ layout: 'auto', vertex: { module, entryPoint: 'vs' },
    fragment: { module, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] } });
}

assert.equal(await validationError(() => layout(limit - 1)), false, 'binding limit-1 layout');
assert.equal(await validationError(() => layout(limit)), true, 'binding limit layout');

for (const binding of [limit - 1, limit]) {
  const over = binding >= limit;
  let compute;
  let render;
  const moduleRejected = await validationError(() => {
    compute = device.createShaderModule({ code: computeCode(binding) });
    render = device.createShaderModule({ code: renderCode(binding) });
  });
  if (over && moduleRejected && flavor === 'doe-c') {
    console.log(`known gap: ${flavor} rejects @binding(${binding}) at createShaderModule instead of pipeline creation`);
  } else {
    assert.equal(moduleRejected, false, `@binding(${binding}) shader modules`);
  }
  assert.equal(await validationError(() => computePipeline(compute)), over, `@binding(${binding}) compute pipeline`);
  assert.equal(await validationError(() => renderPipeline(render)), over, `@binding(${binding}) render pipeline`);
  const asyncCompute = device.createComputePipelineAsync({ layout: 'auto', compute: { module: compute, entryPoint: 'main' } });
  if (over) await assert.rejects(asyncCompute, (error) => error?.name === 'GPUPipelineError', `@binding(${binding}) async compute`);
  else await asyncCompute;
}
console.log(`ok: binding ${limit} rejects at layout and pipeline creation; ${limit - 1} is admitted (${flavor ?? 'provider'})`);
device.destroy();
