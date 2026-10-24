#!/usr/bin/env node
// Render pass and render bundle draws validate their bind groups against the
// pipeline: every group the pipeline layout (or, for an automatic layout, the
// shader) uses must be bound with a compatible layout. Layouts are compatible
// only when every entry matches, including buffer type and visibility.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('draw bind groups: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage, GPUShaderStage, GPUTextureUsage, GPUValidationError } = target;

const plain = device.createShaderModule({
  code: `
    @vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }
    @fragment fn fs() -> @location(0) vec4f { return vec4f(1.0); }
  `,
});
const reading = device.createShaderModule({
  code: `
    @group(0) @binding(0) var<uniform> color: vec4f;
    @vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }
    @fragment fn fs() -> @location(0) vec4f { return color; }
  `,
});
const buffer = device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.STORAGE });
const texture = device.createTexture({ size: [1, 1, 1], format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT });

function layout(type, visibility = GPUShaderStage.FRAGMENT) {
  return device.createBindGroupLayout({ entries: [{ binding: 0, visibility, buffer: { type } }] });
}
function pipeline(pipelineLayout, module = plain) {
  return device.createRenderPipeline({
    layout: pipelineLayout,
    vertex: { module, entryPoint: 'vs' },
    fragment: { module, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] },
  });
}
function explicit(groupLayout) {
  return pipeline(device.createPipelineLayout({ bindGroupLayouts: [groupLayout] }));
}
const uniformGroup = device.createBindGroup({ layout: layout('uniform'), entries: [{ binding: 0, resource: { buffer } }] });
const automatic = pipeline('auto', reading);
const automaticGroup = device.createBindGroup({ layout: automatic.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer } }] });

async function drawErrors(renderPipeline, group, inBundle) {
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({
    colorAttachments: [{ view: texture.createView(), loadOp: 'clear', storeOp: 'store' }],
  });
  const target = inBundle ? device.createRenderBundleEncoder({ colorFormats: ['rgba8unorm'] }) : pass;
  target.setPipeline(renderPipeline);
  if (group) target.setBindGroup(0, group);
  target.draw(3);
  if (inBundle) pass.executeBundles([target.finish()]);
  pass.end();
  encoder.finish();
  return device.popErrorScope();
}

const cases = [
  ['an equal explicit layout', explicit(layout('uniform')), uniformGroup, true],
  ['a missing explicit-layout group', explicit(layout('uniform')), null, false],
  ['a different buffer type', explicit(layout('storage')), uniformGroup, false],
  ['a different visibility', explicit(layout('uniform', GPUShaderStage.VERTEX)), uniformGroup, false],
  ['the automatic layout group', automatic, automaticGroup, true],
  ['a missing automatic-layout group', automatic, null, false],
];
for (const inBundle of [false, true]) {
  for (const [label, renderPipeline, group, valid] of cases) {
    const error = await drawErrors(renderPipeline, group, inBundle);
    const where = inBundle ? 'bundle draw' : 'pass draw';
    if (valid) assert.equal(error, null, `${where} with ${label} is valid`);
    else assert(error instanceof GPUValidationError, `${where} with ${label} is a validation error`);
  }
}
console.log('ok: render pass and bundle draws validate bind groups against the pipeline');
device.destroy();
