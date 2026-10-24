#!/usr/bin/env node
// A pipeline's automatic layout is exclusive to that pipeline
// (https://gpuweb.github.io/gpuweb/#default-pipeline-layout): bind groups made
// from pipeline.getBindGroupLayout() work only with that pipeline, never with
// another automatic-layout pipeline or an explicit layout, and an explicit
// layout's groups never work with an automatic-layout pipeline, even when the
// entries are identical. Checked for dispatches and draws.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('automatic layout exclusivity: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage, GPUShaderStage, GPUTextureUsage, GPUValidationError } = target;
const buffer = device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM });
const texture = device.createTexture({ size: [1, 1, 1], format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT });

const computeModule = device.createShaderModule({
  code: `
    @group(0) @binding(0) var<uniform> a: vec4f;
    @group(1) @binding(0) var<uniform> b: vec4f;
    @compute @workgroup_size(1) fn main() { _ = a; _ = b; }
  `,
});
const renderModule = device.createShaderModule({
  code: `
    @group(0) @binding(0) var<uniform> a: vec4f;
    @group(1) @binding(0) var<uniform> b: vec4f;
    @vertex fn vs() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }
    @fragment fn fs() -> @location(0) vec4f { return a + b; }
  `,
});

const kinds = {
  compute: {
    stage: GPUShaderStage.COMPUTE,
    pipeline: (layout) => device.createComputePipeline({ layout, compute: { module: computeModule, entryPoint: 'main' } }),
    encode(pipeline, groups) {
      const encoder = device.createCommandEncoder();
      const pass = encoder.beginComputePass();
      pass.setPipeline(pipeline);
      groups.forEach((group, index) => pass.setBindGroup(index, group));
      pass.dispatchWorkgroups(1);
      pass.end();
      encoder.finish();
    },
  },
  render: {
    stage: GPUShaderStage.FRAGMENT,
    pipeline: (layout) => device.createRenderPipeline({
      layout,
      vertex: { module: renderModule, entryPoint: 'vs' },
      fragment: { module: renderModule, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] },
    }),
    encode(pipeline, groups) {
      const encoder = device.createCommandEncoder();
      const pass = encoder.beginRenderPass({
        colorAttachments: [{ view: texture.createView(), loadOp: 'clear', storeOp: 'store' }],
      });
      pass.setPipeline(pipeline);
      groups.forEach((group, index) => pass.setBindGroup(index, group));
      pass.draw(3);
      pass.end();
      encoder.finish();
    },
  },
};

for (const [name, kind] of Object.entries(kinds)) {
  const explicitGroupLayout = device.createBindGroupLayout({
    entries: [{ binding: 0, visibility: kind.stage, buffer: {} }],
  });
  const explicit = kind.pipeline(device.createPipelineLayout({ bindGroupLayouts: [explicitGroupLayout, explicitGroupLayout] }));
  const auto0 = kind.pipeline('auto');
  const auto1 = kind.pipeline('auto');
  const groups = (layouts) => layouts.map((layout) => device.createBindGroup({ layout, entries: [{ binding: 0, resource: { buffer } }] }));
  const fromAuto0 = groups([auto0.getBindGroupLayout(0), auto0.getBindGroupLayout(1)]);
  const cases = [
    ['its own automatic layout', auto0, fromAuto0, true],
    ['its own equal groups swapped', auto0, [fromAuto0[1], fromAuto0[0]], true],
    ['an explicit layout with equal entries', explicit, groups([explicitGroupLayout, explicitGroupLayout]), true],
    ["another pipeline's automatic layout", auto1, fromAuto0, false],
    ['explicit-layout groups on an automatic pipeline', auto0, groups([explicitGroupLayout, explicitGroupLayout]), false],
    ['automatic-layout groups on an explicit pipeline', explicit, fromAuto0, false],
  ];
  for (const [label, pipeline, bound, valid] of cases) {
    device.pushErrorScope('validation');
    kind.encode(pipeline, bound);
    const error = await device.popErrorScope();
    if (valid) assert.equal(error, null, `${name}: ${label} is valid`);
    else assert(error instanceof GPUValidationError, `${name}: ${label} is a validation error`);
  }
}
console.log('ok: automatic layouts are exclusive to their pipeline for dispatches and draws');
device.destroy();
