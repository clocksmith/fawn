#!/usr/bin/env node
// A 3d view renders into the one depth slice its color attachment's
// depthSlice names, at the view's mip level; other slices keep their contents
// (https://gpuweb.github.io/gpuweb/#dom-gpurenderpasscolorattachment-depthslice).

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('3d color attachment: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUTextureUsage, GPUBufferUsage, GPUMapMode } = target;

const module = device.createShaderModule({
  code: `
    @vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
      let pos = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
      return vec4f(pos[i], 0.0, 1.0);
    }
    @group(0) @binding(0) var<uniform> value: vec4f;
    @fragment fn fs() -> @location(0) vec4f { return value; }
  `,
});
const pipeline = device.createRenderPipeline({
  layout: 'auto',
  vertex: { module, entryPoint: 'vs' },
  fragment: { module, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] },
});

// 4x4x4 with two mip levels (mip 1 is 2x2x2).
const texture = device.createTexture({
  dimension: '3d',
  size: [4, 4, 4],
  mipLevelCount: 2,
  format: 'rgba8unorm',
  usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
});

function draw(mipLevel, depthSlice, rgba, loadOp) {
  const uniform = device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
  device.queue.writeBuffer(uniform, 0, new Float32Array(rgba));
  const bindGroup = device.createBindGroup({
    layout: pipeline.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: uniform } }],
  });
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({
    colorAttachments: [{
      view: texture.createView({ baseMipLevel: mipLevel, mipLevelCount: 1 }),
      depthSlice,
      loadOp,
      clearValue: [0, 0, 1, 1],
      storeOp: 'store',
    }],
  });
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, bindGroup);
  pass.draw(3);
  pass.end();
  device.queue.submit([encoder.finish()]);
}

async function readSlices(mipLevel) {
  const size = 4 >> mipLevel;
  const bytesPerRow = 256;
  const buffer = device.createBuffer({ size: bytesPerRow * size * size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const encoder = device.createCommandEncoder();
  encoder.copyTextureToBuffer(
    { texture, mipLevel },
    { buffer, bytesPerRow, rowsPerImage: size },
    [size, size, size],
  );
  device.queue.submit([encoder.finish()]);
  await buffer.mapAsync(GPUMapMode.READ);
  const bytes = new Uint8Array(buffer.getMappedRange()).slice();
  buffer.unmap();
  // The first texel of each slice; a full-screen triangle covers every texel.
  return Array.from({ length: size }, (_, z) => Array.from(bytes.subarray(z * bytesPerRow * size, z * bytesPerRow * size + 4)));
}

device.pushErrorScope('validation');
draw(0, 2, [1, 0, 0, 1], 'clear');
draw(0, 0, [0, 1, 0, 1], 'clear');
draw(1, 1, [1, 1, 0, 1], 'load');
assert.equal(await device.popErrorScope(), null, '3d attachments with in-range depth slices are valid');

const mip0 = await readSlices(0);
assert.deepEqual(mip0[0], [0, 255, 0, 255], 'mip 0 slice 0 holds the second draw');
assert.deepEqual(mip0[2], [255, 0, 0, 255], 'mip 0 slice 2 holds the first draw');
assert.deepEqual(mip0[1], [0, 0, 0, 0], 'mip 0 slice 1 is untouched');
assert.deepEqual(mip0[3], [0, 0, 0, 0], 'mip 0 slice 3 is untouched');
const mip1 = await readSlices(1);
assert.deepEqual(mip1[1], [255, 255, 0, 255], 'mip 1 slice 1 holds the third draw');
assert.deepEqual(mip1[0], [0, 0, 0, 0], 'mip 1 slice 0 is untouched');
console.log('ok: 3d color attachments render into the depth slice and mip level they name');
device.destroy();
