#!/usr/bin/env node
// Render raster state reaches the provider: WebGPU framebuffer orientation
// (+y up in clip space is row 0), sparse vertex.buffers slots, a
// multisampled color attachment resolved into a single-sample texture, and
// the multisample mask.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('render raster state: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!adapter) {
  console.log('render raster state: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUMapMode: M } = globalThis;
const SIZE = 4;
const GREEN = [0, 255, 0, 255];
const RED = [255, 0, 0, 255];

// The vertex shader reads clip-space y from slot 1; slot 0 is a hole.
const module = device.createShaderModule({ code: `
  @vertex fn vs(@builtin(vertex_index) i: u32, @location(0) top: f32) -> @builtin(position) vec4f {
    var p = array<vec2f, 6>(vec2f(-1.0, 0.0), vec2f(1.0, 0.0), vec2f(-1.0, 1.0),
                            vec2f(-1.0, 1.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0));
    return vec4f(p[i].x, p[i].y * top, 0.0, 1.0);
  }
  @fragment fn fs() -> @location(0) vec4f { return vec4f(0, 1, 0, 1); }
` });
const buffers = [];
buffers[1] = { arrayStride: 4, attributes: [{ shaderLocation: 0, format: 'float32', offset: 0 }] };
const top = device.createBuffer({ size: 4 * 6, usage: B.VERTEX | B.COPY_DST });
device.queue.writeBuffer(top, 0, new Float32Array(6).fill(1));

function pipeline(sampleCount, mask) {
  return device.createRenderPipeline({
    layout: 'auto',
    vertex: { module, entryPoint: 'vs', buffers },
    fragment: { module, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] },
    multisample: { count: sampleCount, mask },
  });
}

async function render(sampleCount, mask) {
  const output = device.createTexture({ size: [SIZE, SIZE], format: 'rgba8unorm', usage: T.RENDER_ATTACHMENT | T.COPY_SRC });
  const multisampled = sampleCount > 1
    ? device.createTexture({ size: [SIZE, SIZE], format: 'rgba8unorm', sampleCount, usage: T.RENDER_ATTACHMENT })
    : null;
  const readback = device.createBuffer({ size: 256 * SIZE, usage: B.COPY_DST | B.MAP_READ });
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({ colorAttachments: [{
    view: (multisampled ?? output).createView(),
    resolveTarget: multisampled ? output.createView() : undefined,
    loadOp: 'clear',
    storeOp: multisampled ? 'discard' : 'store',
    clearValue: [1, 0, 0, 1],
  }] });
  pass.setPipeline(pipeline(sampleCount, mask));
  pass.setVertexBuffer(1, top);
  pass.draw(6);
  pass.end();
  encoder.copyTextureToBuffer({ texture: output }, { buffer: readback, bytesPerRow: 256 }, [SIZE, SIZE]);
  device.queue.submit([encoder.finish()]);
  assert.equal(await device.popErrorScope(), null);
  await readback.mapAsync(M.READ);
  const bytes = new Uint8Array(readback.getMappedRange());
  const rows = Array.from({ length: SIZE }, (_, row) => Array.from(bytes.slice(row * 256, row * 256 + 4)));
  readback.unmap();
  return rows;
}

assert.deepEqual(await render(1, 0xffffffff), [GREEN, GREEN, RED, RED]);
console.log('ok: clip-space +y renders into the top rows through a sparse vertex buffer slot');
assert.deepEqual(await render(4, 0xffffffff), [GREEN, GREEN, RED, RED]);
console.log('ok: a 4x multisampled attachment resolves into its single-sample target');
assert.deepEqual(await render(4, 0), [RED, RED, RED, RED]);
console.log('ok: a zero multisample mask writes no samples');
device.destroy();
