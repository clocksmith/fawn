#!/usr/bin/env node
// GPUBindingResource admits a GPUBuffer (its whole range) and a GPUTexture
// (its default view) directly, not only { buffer } and GPUTextureView.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('binding resource shorthand: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!adapter) {
  console.log('binding resource shorthand: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUMapMode: M } = globalThis;
const module = device.createShaderModule({ code: `
  @group(0) @binding(0) var tex: texture_2d<f32>;
  @group(0) @binding(1) var<uniform> scale: vec4f;
  @vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    return vec4f(p[i], 0.0, 1.0);
  }
  @fragment fn fs() -> @location(0) vec4f { return textureLoad(tex, vec2i(0, 0), 0) * scale; }
` });
const pipeline = device.createRenderPipeline({ layout: 'auto', vertex: { module, entryPoint: 'vs' },
  fragment: { module, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] } });
const source = device.createTexture({ size: [1, 1], format: 'rgba8unorm', usage: T.TEXTURE_BINDING | T.COPY_DST });
device.queue.writeTexture({ texture: source }, new Uint8Array([200, 100, 50, 255]), { bytesPerRow: 4 }, [1, 1]);
const scale = device.createBuffer({ size: 16, usage: B.UNIFORM | B.COPY_DST });
device.queue.writeBuffer(scale, 0, new Float32Array([0.5, 1, 1, 1]));
const target = device.createTexture({ size: [1, 1], format: 'rgba8unorm', usage: T.RENDER_ATTACHMENT | T.COPY_SRC });
const readback = device.createBuffer({ size: 256, usage: B.COPY_DST | B.MAP_READ });
device.pushErrorScope('validation');
const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0),
  entries: [{ binding: 0, resource: source }, { binding: 1, resource: scale }] });
const encoder = device.createCommandEncoder();
const pass = encoder.beginRenderPass({ colorAttachments: [{ view: target, loadOp: 'clear', storeOp: 'store' }] });
pass.setPipeline(pipeline);
pass.setBindGroup(0, group);
pass.draw(3);
pass.end();
encoder.copyTextureToBuffer({ texture: target }, { buffer: readback, bytesPerRow: 256 }, [1, 1]);
device.queue.submit([encoder.finish()]);
assert.equal(await device.popErrorScope(), null);
await readback.mapAsync(M.READ);
const texel = Array.from(new Uint8Array(readback.getMappedRange().slice(0, 4)));
readback.unmap();
assert.deepEqual(texel.map((v, i) => Math.abs(v - [100, 100, 50, 255][i]) <= 1), [true, true, true, true], `texel ${texel}`);
console.log('ok: a GPUTexture and a GPUBuffer bind directly as resources');
device.destroy();
