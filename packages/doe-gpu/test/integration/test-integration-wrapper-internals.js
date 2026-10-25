#!/usr/bin/env node
// WebGPU wrappers expose only their attributes: after exercising every wrapper
// kind (mapping, encoding, bundles, queries, destroy, device destroy), no
// wrapper enumerates an underscore internal field, and a buffer created from
// another buffer's enumerable attributes matches it (CTS reflection).

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

setupGlobals();
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUMapMode: M, GPUShaderStage: S } = globalThis;
const seen = new Map();
const note = (label, obj) => {
  if (obj == null || typeof obj !== 'object') return;
  const keys = [];
  for (const k in obj) if (k.startsWith('_')) keys.push(k);
  const name = `${label}(${obj.constructor?.name})`;
  const set = seen.get(name) ?? new Set(); keys.forEach((k) => set.add(k)); seen.set(name, set);
};
const gpu = navigator.gpu; note('gpu', gpu);
const adapter = await gpu.requestAdapter() ?? await gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('wrapper internals: skipped (GPU adapter unavailable)');
  process.exit(0);
}
note('adapter', adapter); note('adapterInfo', adapter.info);
const device = await adapter.requestDevice({ requiredFeatures: adapter.features.has('timestamp-query') ? ['timestamp-query'] : [] });
note('device', device); note('queue', device.queue); note('deviceInfo', device.adapterInfo);
device.addEventListener('uncapturederror', () => {}); device.onuncapturederror = () => {}; note('device', device);
const buf = device.createBuffer({ size: 64, usage: B.MAP_READ | B.COPY_DST }); note('buffer', buf);
const wbuf = device.createBuffer({ size: 64, usage: B.MAP_WRITE | B.COPY_SRC, mappedAtCreation: true }); note('buffer', wbuf);
wbuf.getMappedRange(0, 16); note('buffer', wbuf); wbuf.unmap(); note('buffer', wbuf);
await wbuf.mapAsync(M.WRITE); wbuf.getMappedRange(); note('buffer', wbuf); wbuf.unmap(); note('buffer', wbuf);
const sbuf = device.createBuffer({ size: 256, usage: B.STORAGE | B.COPY_SRC | B.COPY_DST | B.UNIFORM | B.VERTEX | B.INDEX | B.INDIRECT | B.QUERY_RESOLVE });
note('buffer', sbuf);
device.queue.writeBuffer(sbuf, 0, new Uint32Array(16)); note('queue', device.queue); note('buffer', sbuf);
const tex = device.createTexture({ size: [4, 4], format: 'rgba8unorm', usage: T.RENDER_ATTACHMENT | T.TEXTURE_BINDING | T.COPY_SRC | T.COPY_DST });
note('texture', tex); const view = tex.createView(); note('view', view);
device.queue.writeTexture({ texture: tex }, new Uint8Array(64), { bytesPerRow: 16 }, [4, 4]); note('texture', tex);
const sampler = device.createSampler(); note('sampler', sampler);
const module = device.createShaderModule({ code: `
@group(0) @binding(0) var<storage, read_write> o: array<u32>;
@compute @workgroup_size(1) fn main() { o[0] = 1u; }` });
const rmodule = device.createShaderModule({ code: `
@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0, 0, 0, 1); }
@fragment fn fs() -> @location(0) vec4f { return vec4f(1); }` });
note('module', module); note('compilationInfo', await module.getCompilationInfo());
const cp = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' } }); note('computePipeline', cp);
const bgl = cp.getBindGroupLayout(0); note('bindGroupLayout', bgl);
const bg = device.createBindGroup({ layout: bgl, entries: [{ binding: 0, resource: { buffer: sbuf } }] }); note('bindGroup', bg);
const ebgl = device.createBindGroupLayout({ entries: [{ binding: 0, visibility: S.COMPUTE, buffer: { type: 'storage' } }] }); note('bindGroupLayout', ebgl);
const pl = device.createPipelineLayout({ bindGroupLayouts: [ebgl] }); note('pipelineLayout', pl);
const cp2 = await device.createComputePipelineAsync({ layout: pl, compute: { module, entryPoint: 'main' } }); note('computePipeline', cp2);
const rp = device.createRenderPipeline({ layout: 'auto', vertex: { module: rmodule, entryPoint: 'vs' }, fragment: { module: rmodule, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] }, primitive: { topology: 'point-list' } });
note('renderPipeline', rp);
const qs = device.createQuerySet({ type: 'occlusion', count: 2 }); note('querySet', qs);
let enc = device.createCommandEncoder(); note('encoder', enc);
const cpass = enc.beginComputePass(); note('computePass', cpass);
cpass.setPipeline(cp); cpass.setBindGroup(0, bg); cpass.dispatchWorkgroups(1); cpass.dispatchWorkgroupsIndirect(sbuf, 0); cpass.end(); note('computePass', cpass);
const rpass = enc.beginRenderPass({ colorAttachments: [{ view, loadOp: 'clear', storeOp: 'store' }], occlusionQuerySet: qs }); note('renderPass', rpass);
rpass.setPipeline(rp); rpass.setVertexBuffer(0, sbuf); rpass.setIndexBuffer(sbuf, 'uint32'); rpass.setViewport(0, 0, 4, 4, 0, 1); rpass.setScissorRect(0, 0, 4, 4);
rpass.beginOcclusionQuery(0); rpass.draw(1); rpass.endOcclusionQuery(); rpass.drawIndirect(sbuf, 0); rpass.drawIndexed(1); note('renderPass', rpass);
const be = device.createRenderBundleEncoder({ colorFormats: ['rgba8unorm'] }); note('bundleEncoder', be);
be.setPipeline(rp); be.draw(1); const bundle = be.finish(); note('bundleEncoder', be); note('bundle', bundle);
rpass.executeBundles([bundle]); rpass.end(); note('renderPass', rpass);
enc.copyBufferToBuffer(sbuf, 0, buf, 0, 64); enc.copyTextureToBuffer({ texture: tex }, { buffer: sbuf, bytesPerRow: 256 }, [1, 1]);
enc.clearBuffer(sbuf, 128, 16); enc.resolveQuerySet(qs, 0, 2, sbuf, 0); note('encoder', enc);
const cb = enc.finish(); note('encoder', enc); note('commandBuffer', cb);
device.queue.submit([cb]); note('commandBuffer', cb); note('queue', device.queue);
await device.queue.onSubmittedWorkDone(); note('queue', device.queue);
await buf.mapAsync(M.READ); buf.getMappedRange(); note('buffer', buf); buf.unmap(); note('buffer', buf);
device.pushErrorScope('validation'); const bad = device.createBuffer({ size: 4, usage: 0 }); await device.popErrorScope(); note('buffer', bad);
const badTex = (device.pushErrorScope('validation'), device.createTexture({ size: [1, 1], format: 'rgba8unorm', usage: 0 })); await device.popErrorScope(); note('texture', badTex);
buf.destroy(); note('buffer', buf); tex.destroy(); note('texture', tex); qs.destroy(); note('querySet', qs);
const mapped = device.createBuffer({ size: 16, usage: B.MAP_READ }); const p = mapped.mapAsync(M.READ).catch(() => {}); note('buffer', mapped);
device.lost.then(() => {}); note('device', device);
device.destroy(); await p; note('device', device); note('buffer', mapped); note('queue', device.queue);
const exposed = [...seen].filter(([, keys]) => keys.size).map(([name, keys]) => `${name}: ${[...keys].join(' ')}`);
assert.deepEqual(exposed, [], 'no wrapper enumerates an internal field');
const reflectAdapter = await gpu.requestAdapter() ?? await gpu.requestAdapter({ forceFallbackAdapter: true });
assert.ok(reflectAdapter, 'a second adapter for the reflection check');
const reflectDevice = await reflectAdapter.requestDevice();
const source = reflectDevice.createBuffer({ size: 16, usage: B.COPY_DST, label: 'reflected' });
const copy = reflectDevice.createBuffer(source);
for (const key in source) {
  if (typeof source[key] !== 'function') assert.equal(copy[key], source[key], `buffer.${key} reflects`);
}
reflectDevice.destroy();
console.log(`ok: ${seen.size} wrapper kinds enumerate no internal fields; a buffer recreated from its reflection matches`);
