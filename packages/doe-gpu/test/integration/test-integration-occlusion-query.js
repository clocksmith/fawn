#!/usr/bin/env node
// beginOcclusionQuery/endOcclusionQuery reach the provider: a query around a
// visible draw resolves nonzero and a query around no draw resolves zero.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
const adapter = await target.navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('occlusion query: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage, GPUMapMode, GPUTextureUsage } = target;
const module = device.createShaderModule({
  code: `
    @vertex fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
      let corners = array<vec2f, 3>(vec2f(-0.5, -0.5), vec2f(0.5, -0.5), vec2f(-0.5, 0.5));
      return vec4f(corners[index], 0.0, 1.0);
    }
    @fragment fn fs() -> @location(0) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }
  `,
});
const pipeline = device.createRenderPipeline({
  layout: 'auto',
  vertex: { module, entryPoint: 'vs' },
  fragment: { module, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] },
});
const querySet = device.createQuerySet({ type: 'occlusion', count: 2 });
const resolved = device.createBuffer({ size: 16, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
const readback = device.createBuffer({ size: 16, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
const view = device.createTexture({
  size: [4, 4, 1],
  format: 'rgba8unorm',
  usage: GPUTextureUsage.RENDER_ATTACHMENT,
}).createView();

device.pushErrorScope('validation');
const encoder = device.createCommandEncoder();
const pass = encoder.beginRenderPass({
  colorAttachments: [{ view, loadOp: 'clear', storeOp: 'store' }],
  occlusionQuerySet: querySet,
});
pass.setPipeline(pipeline);
pass.beginOcclusionQuery(0);
pass.draw(3);
pass.endOcclusionQuery();
pass.beginOcclusionQuery(1);
pass.endOcclusionQuery();
pass.end();
encoder.resolveQuerySet(querySet, 0, 2, resolved, 0);
encoder.copyBufferToBuffer(resolved, 0, readback, 0, 16);
device.queue.submit([encoder.finish()]);
assert.equal(await device.popErrorScope(), null, 'occlusion queries encode without errors');
await readback.mapAsync(GPUMapMode.READ);
const [visible, empty] = new BigUint64Array(readback.getMappedRange().slice(0));
readback.unmap();
assert(visible > 0n, `a visible draw passes samples (resolved ${visible})`);
assert.equal(empty, 0n, 'a query without draws resolves zero');
console.log('ok: occlusion queries count a visible draw and leave an empty query zero');
device.destroy();
