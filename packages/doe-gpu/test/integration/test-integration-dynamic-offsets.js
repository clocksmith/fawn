#!/usr/bin/env node
// setBindGroup dynamic offsets reach the provider on compute passes, render
// passes and render bundles, in both the sequence and the Uint32Array forms:
// each shader reads the uniform at the offset it was given.
//
// Known provider gap: the Zig drop-in's render bundles discard dynamic offsets
// (runtime/zig/src/native/render/doe_bundle_native.zig
// doeNativeRenderBundleEncoderSetBindGroup), so its bundles read offset 0. On
// that provider the bundle draw checks encoding only.

import assert from 'node:assert/strict';
import { providerInfo, setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('dynamic offsets: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const flavor = providerInfo().libraryFlavor;
const bundlesApplyOffsets = flavor !== 'doe-dropin';
if (!bundlesApplyOffsets) console.log(`known gap: ${flavor} bundles discard dynamic offsets; checking bundle encoding only`);
const { GPUBufferUsage, GPUMapMode, GPUShaderStage, GPUTextureUsage } = target;
const stride = Math.max(device.limits.minUniformBufferOffsetAlignment, device.limits.minStorageBufferOffsetAlignment);

// Slot k of the uniform buffer holds the value 10 + k in its first word.
const uniforms = device.createBuffer({ size: stride * 4, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
const words = new Uint32Array(stride);
for (let slot = 0; slot < 4; slot += 1) words[(slot * stride) / 4] = 10 + slot;
device.queue.writeBuffer(uniforms, 0, words);

async function readWords(buffer, count) {
  const readback = device.createBuffer({ size: count * 4, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
  const encoder = device.createCommandEncoder();
  encoder.copyBufferToBuffer(buffer, 0, readback, 0, count * 4);
  device.queue.submit([encoder.finish()]);
  await readback.mapAsync(GPUMapMode.READ);
  const result = Array.from(new Uint32Array(readback.getMappedRange().slice(0)));
  readback.unmap();
  return result;
}

// Compute: one dispatch per offset form copies the uniform into its output slot.
{
  const layout = device.createBindGroupLayout({
    entries: [
      { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform', hasDynamicOffset: true } },
      { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage', hasDynamicOffset: true } },
    ],
  });
  const output = device.createBuffer({ size: stride * 2, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
  const group = device.createBindGroup({
    layout,
    entries: [
      { binding: 0, resource: { buffer: uniforms, size: 16 } },
      { binding: 1, resource: { buffer: output, size: 16 } },
    ],
  });
  const pipeline = device.createComputePipeline({
    layout: device.createPipelineLayout({ bindGroupLayouts: [layout] }),
    compute: {
      module: device.createShaderModule({
        code: `
          @group(0) @binding(0) var<uniform> source: vec4u;
          @group(0) @binding(1) var<storage, read_write> sink: vec4u;
          @compute @workgroup_size(1) fn main() { sink = source; }
        `,
      }),
      entryPoint: 'main',
    },
  });
  device.pushErrorScope('validation');
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, group, [stride * 2, 0]);
  pass.dispatchWorkgroups(1);
  pass.setBindGroup(0, group, new Uint32Array([99, stride * 3, stride, 99]), 1, 2);
  pass.dispatchWorkgroups(1);
  pass.end();
  device.queue.submit([encoder.finish()]);
  assert.equal(await device.popErrorScope(), null, 'compute dynamic offsets encode without errors');
  const result = await readWords(output, stride / 2);
  assert.deepEqual([result[0], result[stride / 4]], [12, 13], 'each dispatch read the uniform at its own offset');
  console.log('ok: compute passes apply sequence and Uint32Array dynamic offsets');
}

// Render pass and bundle: the fragment colour is the uniform at the offset.
{
  const layout = device.createBindGroupLayout({
    entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform', hasDynamicOffset: true } }],
  });
  const group = device.createBindGroup({ layout, entries: [{ binding: 0, resource: { buffer: uniforms, size: 16 } }] });
  const module = device.createShaderModule({
    code: `
      @group(0) @binding(0) var<uniform> source: vec4u;
      @vertex fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
        let corners = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
        return vec4f(corners[index], 0.0, 1.0);
      }
      @fragment fn fs() -> @location(0) vec4f { return vec4f(f32(source.x) / 255.0, 0.0, 0.0, 1.0); }
    `,
  });
  const pipeline = device.createRenderPipeline({
    layout: device.createPipelineLayout({ bindGroupLayouts: [layout] }),
    vertex: { module, entryPoint: 'vs' },
    fragment: { module, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] },
  });
  async function redChannel(encode) {
    const texture = device.createTexture({
      size: [1, 1, 1],
      format: 'rgba8unorm',
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
    });
    const pixels = device.createBuffer({ size: 4, usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
    device.pushErrorScope('validation');
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
      colorAttachments: [{ view: texture.createView(), clearValue: [0, 0, 0, 0], loadOp: 'clear', storeOp: 'store' }],
    });
    encode(pass);
    pass.end();
    encoder.copyTextureToBuffer({ texture }, { buffer: pixels, bytesPerRow: 256 }, [1, 1, 1]);
    device.queue.submit([encoder.finish()]);
    assert.equal(await device.popErrorScope(), null, 'render dynamic offsets encode without errors');
    return (await readWords(pixels, 1))[0] & 0xff;
  }
  const direct = await redChannel((pass) => {
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, group, new Uint32Array([stride, stride * 2]), 1, 1);
    pass.draw(3);
  });
  assert.equal(direct, 12, 'a render pass reads the uniform at its Uint32Array dynamic offset');
  const bundleEncoder = device.createRenderBundleEncoder({ colorFormats: ['rgba8unorm'] });
  bundleEncoder.setPipeline(pipeline);
  bundleEncoder.setBindGroup(0, group, [stride * 3]);
  bundleEncoder.draw(3);
  const bundle = bundleEncoder.finish();
  const bundled = await redChannel((pass) => pass.executeBundles([bundle]));
  if (bundlesApplyOffsets) assert.equal(bundled, 13, 'a render bundle reads the uniform at its dynamic offset');
  console.log(`ok: render passes apply dynamic offsets; bundles ${bundlesApplyOffsets ? 'apply' : 'encode'} them`);
}

assert.throws(() => {
  const pass = device.createCommandEncoder().beginComputePass();
  pass.setBindGroup(0, null, new Uint32Array(2), 1, 2);
}, RangeError, 'an offset range past the Uint32Array end throws RangeError');
console.log('ok: dynamicOffsetsDataStart + dynamicOffsetsDataLength is range-checked');
device.destroy();
