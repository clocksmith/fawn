#!/usr/bin/env node
// Binding numbers above 31 are ordinary WebGPU bindings: layouts, bind groups
// and pipelines are validated at every number below maxBindingsPerBindGroup,
// and a valid program using them runs.

import assert from 'node:assert/strict';
import { providerInfo, setupGlobals } from '../../src/index.js';

setupGlobals();
const adapter = await navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('high binding numbers: skipped (no adapter)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUShaderStage: S, GPUMapMode: M } = globalThis;
const flavor = providerInfo().libraryFlavor;

async function validationError(action) {
  device.pushErrorScope('validation');
  action();
  return (await device.popErrorScope()) !== null;
}

for (const base of [40, 900]) {
  const code = `@group(0) @binding(${base}) var<storage, read_write> out: array<u32>;
@group(0) @binding(${base + 1}) var<uniform> k: vec4u;
@compute @workgroup_size(1) fn main() { out[0] = k.x + 1u; }`;
  const module = device.createShaderModule({ code });
  const storage = { binding: base, visibility: S.COMPUTE, buffer: { type: 'storage' } };
  const uniform = { binding: base + 1, visibility: S.COMPUTE, buffer: { type: 'uniform' } };
  const layout = device.createBindGroupLayout({ entries: [storage, uniform] });
  const pipelineFor = (bindGroupLayout) => device.createComputePipeline({
    layout: device.createPipelineLayout({ bindGroupLayouts: [bindGroupLayout] }), compute: { module, entryPoint: 'main' } });

  // Pipeline layout checks: a missing binding and another buffer type.
  const missing = device.createBindGroupLayout({ entries: [storage] });
  const swapped = device.createBindGroupLayout({ entries: [storage, { ...uniform, buffer: { type: 'storage' } }] });
  assert.equal(await validationError(() => pipelineFor(missing)), true, `${base}: layout without @binding(${base + 1})`);
  assert.equal(await validationError(() => pipelineFor(swapped)), true, `${base}: storage layout for a uniform`);
  let pipeline;
  assert.equal(await validationError(() => { pipeline = pipelineFor(layout); }), false, `${base}: matching layout`);

  // Bind group checks: a missing entry, an extra entry, a resource of another type.
  const out = device.createBuffer({ size: 16, usage: B.STORAGE | B.COPY_SRC });
  const k = device.createBuffer({ size: 16, usage: B.UNIFORM | B.COPY_DST });
  const sampler = device.createSampler();
  const entries = [{ binding: base, resource: { buffer: out } }, { binding: base + 1, resource: { buffer: k } }];
  assert.equal(await validationError(() => device.createBindGroup({ layout, entries: entries.slice(0, 1) })), true, `${base}: missing entry`);
  assert.equal(await validationError(() => device.createBindGroup({ layout, entries: [...entries, { binding: base + 2, resource: { buffer: k } }] })), true, `${base}: extra entry`);
  assert.equal(await validationError(() => device.createBindGroup({ layout, entries: [entries[0], { binding: base + 1, resource: sampler }] })), true, `${base}: sampler for a buffer`);
  let group;
  assert.equal(await validationError(() => { group = device.createBindGroup({ layout, entries }); }), false, `${base}: matching bind group`);

  // The valid program runs.
  device.queue.writeBuffer(k, 0, new Uint32Array([base, 0, 0, 0]));
  const readback = device.createBuffer({ size: 16, usage: B.MAP_READ | B.COPY_DST });
  const rejected = await validationError(() => {
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginComputePass();
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, group);
    pass.dispatchWorkgroups(1);
    pass.end();
    encoder.copyBufferToBuffer(out, 0, readback, 0, 16);
    device.queue.submit([encoder.finish()]);
  });
  await readback.mapAsync(M.READ);
  const value = new Uint32Array(readback.getMappedRange())[0];
  readback.unmap();
  if (flavor === 'doe-dropin' && rejected) {
    // Its dispatch tables hold binding numbers below 32 per group; it reports
    // the dispatch instead of leaving the bindings out.
    assert.equal(value, 0, `${base}: a rejected dispatch writes nothing`);
    console.log(`known gap: ${flavor} validates @binding(${base}) but rejects its dispatch`);
  } else {
    assert.equal(rejected, false, `${base}: valid dispatch`);
    assert.equal(value, base + 1, `${base}: shader output`);
  }
}
console.log(`ok: bindings 40-41 and 900-901 are validated at layout, bind group and pipeline creation (${flavor ?? 'provider'})`);
device.destroy();
