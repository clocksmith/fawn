// Physical fragment regression for the scalar mix factor found in D4DA's shaders.
import assert from 'node:assert/strict';
import { readFileSync, realpathSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { requestAdapter, globals } from '../../../packages/doe-gpu/src/native.js';
import { loadNativeAddon } from '../../../packages/doe-gpu/test/integration/native-addon-test-helper.js';

assert(process.env.DOE_WEBGPU_LIB, 'Pin the candidate with DOE_WEBGPU_LIB');
const adapter = await requestAdapter({ backend: 'vulkan' });
const device = await adapter.requestDevice();
const addon = await loadNativeAddon();
const library = realpathSync(process.env.DOE_WEBGPU_LIB);
const loaded = readFileSync('/proc/self/maps', 'utf8').split('\n').filter(line => line.endsWith(library));
assert(loaded.length > 0, `Requested library was not loaded: ${library}`);
console.log(JSON.stringify({ library, sha256: createHash('sha256').update(readFileSync(library)).digest('hex'), loaded }));
const WIDTH = 3, ROW_BYTES = 256, TOLERANCE = 1;
const endpoints = [[0.1, 0.3, 0.7, 0.2], [0.9, 0.5, 0.1, 0.8]];
try {
  for (const lanes of [2, 3, 4]) {
    const type = `vec${lanes}<f32>`;
    const color = lanes === 2 ? 'vec3<f32>(mixed, 0.25)' : 'mixed.xyz';
    const source = `
      var<private> evaluations: u32;
      fn factor(x: f32) -> f32 { evaluations++; return (x - 0.5) * 0.5; }
      @vertex fn vertex(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
        return vec4<f32>(f32((index << 1u) & 2u) * 2.0 - 1.0, f32(index & 2u) * 2.0 - 1.0, 0.0, 1.0);
      }
      @fragment fn fragment(@builtin(position) position: vec4<f32>) -> @location(0) vec4<f32> {
        let mixed = mix(${type}(${endpoints[0].slice(0, lanes).join(', ')}),
          ${type}(${endpoints[1].slice(0, lanes).join(', ')}), factor(position.x));
        return vec4<f32>(${color}, f32(evaluations) * 0.5);
      }`;
    const retained = [];
    const own = (value, release) => { retained.push([value, release]); return value; };
    try {
      const module = own(addon.createShaderModule(device._native, source), addon.shaderModuleRelease);
      const pipeline = own(addon.createRenderPipeline(device._native, {
        vertex: { module, entryPoint: 'vertex' },
        fragment: { module, entryPoint: 'fragment', targets: [{ format: 'rgba8unorm' }] },
        primitive: { topology: 'triangle-list' },
      }), addon.renderPipelineRelease);
      const texture = own(addon.createTexture(device._native, { width: WIDTH, height: 1,
        format: 'rgba8unorm', usage: globals.GPUTextureUsage.RENDER_ATTACHMENT | globals.GPUTextureUsage.COPY_SRC }), addon.textureRelease);
      const view = own(addon.textureCreateView(texture), addon.textureViewRelease);
      const readback = own(addon.createBuffer(device._native, { size: ROW_BYTES,
        usage: globals.GPUBufferUsage.MAP_READ | globals.GPUBufferUsage.COPY_DST }), addon.bufferRelease);
      const encoder = own(addon.createCommandEncoder(device._native, 'mix-readback'), addon.commandEncoderRelease);
      const pass = own(addon.beginRenderPass(encoder, { colorAttachments: [{ view,
        loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }] }), addon.renderPassRelease);
      addon.renderPassSetPipeline(pass, pipeline);
      addon.renderPassDraw(pass, 3, 1, 0, 0);
      addon.renderPassEnd(pass);
      addon.commandEncoderCopyTextureToBuffer(encoder, texture, 0, 0, 0, 0, 1,
        readback, 0, ROW_BYTES, 1, WIDTH, 1, 1);
      const commands = own(addon.commandEncoderFinish(encoder), addon.commandBufferRelease);
      addon.queueSubmit(device.queue._native, [commands]);
      await device.queue.onSubmittedWorkDone();
      addon.bufferMapSync(device._instance, readback, globals.GPUMapMode.READ, 0, ROW_BYTES);
      const pixels = [...new Uint8Array(addon.bufferReadCopy(readback, 0, ROW_BYTES)).slice(0, WIDTH * 4)];
      addon.bufferUnmap(readback);
      const expected = [];
      for (let x = 0; x < WIDTH; x++) {
        const factor = x / 2;
        const rgb = endpoints[0].slice(0, 3).map((a, channel) =>
          lanes === 2 && channel === 2 ? 0.25 : a * (1 - factor) + endpoints[1][channel] * factor);
        expected.push(...[...rgb, 0.5].map(value => Math.round(value * 255)));
      }
      pixels.forEach((value, index) => assert(Math.abs(value - expected[index]) <= TOLERANCE,
        `vec${lanes} byte ${index}: expected ${expected[index]}, got ${value}`));
      console.log(JSON.stringify({ lanes, passed: true, pixels, expected }));
    } finally {
      for (const [value, release] of retained.reverse()) release(value);
    }
  }
} finally { device.destroy(); adapter.destroy(); }
