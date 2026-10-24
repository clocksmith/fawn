#!/usr/bin/env node
// Indirect draw arguments are GPU data the provider must validate before use:
// an instance, vertex or index range past the bound buffers skips the draw
// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-draw), and a huge
// index count never runs. A valid indirect draw still renders.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('indirect draw limits: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage, GPUMapMode, GPUTextureUsage } = target;

const module = device.createShaderModule({
  code: `
    struct Out { @builtin(position) position: vec4f, @location(0) tint: f32 }
    @vertex fn vs(@builtin(vertex_index) v: u32, @location(0) tint: f32) -> Out {
      let corners = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
      var out: Out;
      out.position = vec4f(corners[v % 3u], 0.0, 1.0);
      out.tint = tint;
      return out;
    }
    @fragment fn fs(input: Out) -> @location(0) vec4f { return vec4f(1.0, input.tint, 0.0, 1.0); }
  `,
});
const pipeline = device.createRenderPipeline({
  layout: 'auto',
  vertex: {
    module,
    entryPoint: 'vs',
    buffers: [{ arrayStride: 4, stepMode: 'instance', attributes: [{ shaderLocation: 0, offset: 0, format: 'float32' }] }],
  },
  fragment: { module, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] },
});
// Two instances and three indices are bound.
const instances = device.createBuffer({ size: 8, usage: GPUBufferUsage.VERTEX, mappedAtCreation: true });
new Float32Array(instances.getMappedRange()).set([0.5, 0.5]);
instances.unmap();
const indices = device.createBuffer({ size: 12, usage: GPUBufferUsage.INDEX, mappedAtCreation: true });
new Uint32Array(indices.getMappedRange()).set([0, 1, 2]);
indices.unmap();

async function drew(args, indexed) {
  const indirect = device.createBuffer({ size: 20, usage: GPUBufferUsage.INDIRECT | GPUBufferUsage.COPY_DST });
  device.queue.writeBuffer(indirect, 0, new Uint32Array(args));
  const texture = device.createTexture({
    size: [1, 1],
    format: 'rgba8unorm',
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
  });
  const pixels = device.createBuffer({ size: 4, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({
    colorAttachments: [{ view: texture.createView(), clearValue: [0, 0, 0, 0], loadOp: 'clear', storeOp: 'store' }],
  });
  pass.setPipeline(pipeline);
  pass.setVertexBuffer(0, instances);
  if (indexed) {
    pass.setIndexBuffer(indices, 'uint32');
    pass.drawIndexedIndirect(indirect, 0);
  } else {
    pass.drawIndirect(indirect, 0);
  }
  pass.end();
  encoder.copyTextureToBuffer({ texture }, { buffer: pixels, bytesPerRow: 256 }, [1, 1]);
  device.queue.submit([encoder.finish()]);
  assert.equal(await device.popErrorScope(), null, 'indirect draws encode without validation errors');
  await pixels.mapAsync(GPUMapMode.READ);
  const red = new Uint8Array(pixels.getMappedRange())[0];
  pixels.unmap();
  return red !== 0;
}

assert(await drew([3, 2, 0, 0], false), 'a valid drawIndirect renders');
assert(await drew([3, 2, 0, 0, 0], true), 'a valid drawIndexedIndirect renders');
for (const [name, args, indexed] of [
  ['instanceCount past the instance buffer', [3, 3, 0, 0], false],
  ['firstInstance + instanceCount past the instance buffer', [3, 1, 0, 2], false],
  ['indexCount past the index buffer', [4, 1, 0, 0, 0], true],
  ['firstIndex + indexCount past the index buffer', [3, 1, 1, 0, 0], true],
  ['indexed instanceCount past the instance buffer', [3, 3, 0, 0, 0], true],
  ['a huge indexCount', [0x80000000, 1, 0, 0, 0], true],
]) {
  assert(!(await drew(args, indexed)), `${name} skips the draw`);
}
console.log('ok: indirect draws outside the bound vertex, instance and index ranges are skipped');
device.destroy();
