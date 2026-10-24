#!/usr/bin/env node
// Render pipelines with an explicit layout of two bind groups: bind groups made
// from pipeline.getBindGroupLayout(i) draw with group 0 and group 1 resources
// at their own @group numbers, and a group bound with an incompatible layout is
// a validation error at encoding (never a native crash).

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('render bind groups: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!adapter) {
  console.log('render bind groups: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUMapMode: M, GPUShaderStage: S } = globalThis;
const module = device.createShaderModule({ code: `
  @group(0) @binding(0) var<uniform> scale: vec4f;
  @group(1) @binding(0) var tex: texture_2d<f32>;
  @vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    return vec4f(p[i], 0.0, 1.0);
  }
  @fragment fn fs() -> @location(0) vec4f { return textureLoad(tex, vec2i(0, 0), 0) * scale; }
` });
const layouts = [
  device.createBindGroupLayout({ entries: [{ binding: 0, visibility: S.FRAGMENT, buffer: {} }] }),
  device.createBindGroupLayout({ entries: [{ binding: 0, visibility: S.FRAGMENT, texture: { sampleType: 'unfilterable-float' } }] }),
];
const pipeline = device.createRenderPipeline({ layout: device.createPipelineLayout({ bindGroupLayouts: layouts }),
  vertex: { module, entryPoint: 'vs' }, fragment: { module, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] } });
const source = device.createTexture({ size: [1, 1], format: 'rgba8unorm', usage: T.TEXTURE_BINDING | T.COPY_DST });
device.queue.writeTexture({ texture: source }, new Uint8Array([200, 100, 50, 255]), { bytesPerRow: 4 }, [1, 1]);
const scale = device.createBuffer({ size: 16, usage: B.UNIFORM | B.COPY_DST });
device.queue.writeBuffer(scale, 0, new Float32Array([0.5, 1, 1, 1]));
const target = device.createTexture({ size: [1, 1], format: 'rgba8unorm', usage: T.RENDER_ATTACHMENT | T.COPY_SRC });

async function draw(groups) {
  const readback = device.createBuffer({ size: 256, usage: B.COPY_DST | B.MAP_READ });
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({ colorAttachments: [{ view: target.createView(), loadOp: 'clear', storeOp: 'store' }] });
  pass.setPipeline(pipeline);
  groups.forEach((group, index) => pass.setBindGroup(index, group));
  pass.draw(3);
  pass.end();
  encoder.copyTextureToBuffer({ texture: target }, { buffer: readback, bytesPerRow: 256 }, [1, 1]);
  device.queue.submit([encoder.finish()]);
  const error = await device.popErrorScope();
  await readback.mapAsync(M.READ);
  const texel = Array.from(new Uint8Array(readback.getMappedRange().slice(0, 4)));
  readback.unmap();
  return { error, texel };
}

const group0 = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: scale } }] });
const group1 = device.createBindGroup({ layout: pipeline.getBindGroupLayout(1), entries: [{ binding: 0, resource: source.createView() }] });
const drawn = await draw([group0, group1]);
assert.equal(drawn.error, null);
assert.deepEqual(drawn.texel.map((v, i) => Math.abs(v - [100, 100, 50, 255][i]) <= 1), [true, true, true, true], `texel ${drawn.texel}`);
console.log('ok: groups 0 and 1 from getBindGroupLayout draw at their own @group numbers');

// Group 1 bound in slot 0: its layout (a texture) is not the pipeline's group 0 layout.
const swapped = await draw([group1, group1]);
assert.ok(swapped.error, 'an incompatible bind group is a validation error');
console.log('ok: an incompatible bind group is rejected at encoding');
device.destroy();
