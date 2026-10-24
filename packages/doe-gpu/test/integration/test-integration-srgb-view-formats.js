#!/usr/bin/env node
// srgb views through viewFormats on every provider: an rgba8unorm texture, and
// a bc1-rgba-unorm texture when texture-compression-bc is available, viewed as
// its srgb pair must decode bit-identically to a native srgb texture holding
// the same bytes, and differently from the unorm view.

import assert from 'node:assert/strict';
import { setupGlobals, providerInfo } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('srgb view formats: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!adapter) {
  console.log('srgb view formats: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const bc = adapter.features.has('texture-compression-bc');
const device = await adapter.requestDevice({ requiredFeatures: bc ? ['texture-compression-bc'] : [] });
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUMapMode: M } = globalThis;
const TEXELS = 4;

const module = device.createShaderModule({ code: `
  @group(0) @binding(0) var as_srgb: texture_2d<f32>;
  @group(0) @binding(1) var native_srgb: texture_2d<f32>;
  @group(0) @binding(2) var as_unorm: texture_2d<f32>;
  @group(0) @binding(3) var<storage, read_write> out: array<vec4<f32>, ${3 * TEXELS}>;
  @compute @workgroup_size(${TEXELS}) fn main(@builtin(local_invocation_index) i: u32) {
    let p = vec2<i32>(i32(i), 0);
    out[i] = textureLoad(as_srgb, p, 0);
    out[${TEXELS}u + i] = textureLoad(native_srgb, p, 0);
    out[${2 * TEXELS}u + i] = textureLoad(as_unorm, p, 0);
  }
` });
const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' } });

async function decode(format, srgbFormat, size, bytes, layout) {
  const usage = T.TEXTURE_BINDING | T.COPY_DST;
  const base = device.createTexture({ size, format, usage, viewFormats: [srgbFormat] });
  const reference = device.createTexture({ size, format: srgbFormat, usage });
  for (const texture of [base, reference]) device.queue.writeTexture({ texture }, bytes, layout, size);
  const output = device.createBuffer({ size: 3 * TEXELS * 16, usage: B.STORAGE | B.COPY_SRC });
  const readback = device.createBuffer({ size: 3 * TEXELS * 16, usage: B.MAP_READ | B.COPY_DST });
  const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
    { binding: 0, resource: base.createView({ format: srgbFormat }) },
    { binding: 1, resource: reference.createView() },
    { binding: 2, resource: base.createView() },
    { binding: 3, resource: { buffer: output } },
  ] });
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, group);
  pass.dispatchWorkgroups(1);
  pass.end();
  encoder.copyBufferToBuffer(output, 0, readback, 0, readback.size);
  device.queue.submit([encoder.finish()]);
  await readback.mapAsync(M.READ);
  const values = new Float32Array(readback.getMappedRange().slice(0));
  readback.unmap();
  for (const resource of [base, reference, output, readback]) resource.destroy();
  const texel = (block, i) => Array.from(values.subarray((block * TEXELS + i) * 4, (block * TEXELS + i) * 4 + 4));
  return Array.from({ length: TEXELS }, (_, i) => ({ view: texel(0, i), native: texel(1, i), unorm: texel(2, i) }));
}

function check(label, texels, distinct) {
  for (const [i, { view, native, unorm }] of texels.entries()) {
    assert.deepEqual(view, native, `${label} texel ${i}: srgb view ${view} differs from native srgb ${native}`);
    if (distinct[i]) assert.notDeepEqual(view.slice(0, 3), unorm.slice(0, 3), `${label} texel ${i}: srgb view decoded as unorm`);
  }
  console.log(`ok: ${label} srgb view decodes bit-identically to a native srgb texture`);
}

const codes = [64, 128, 200, 255];
const rgba = new Uint8Array(codes.flatMap((code) => [code, code, code, 255]));
check('rgba8unorm', await decode('rgba8unorm', 'rgba8unorm-srgb', { width: TEXELS, height: 1 }, rgba,
  { bytesPerRow: TEXELS * 4 }), [true, true, true, false]);

if (bc) {
  // One BC1 block: color0 = 565 (16, 32, 16), color1 = 0, every index 0.
  const block = new Uint8Array([0x10, 0x84, 0x00, 0x00, 0, 0, 0, 0]);
  check('bc1-rgba-unorm', await decode('bc1-rgba-unorm', 'bc1-rgba-unorm-srgb', { width: 4, height: 4 }, block,
    { bytesPerRow: 8, rowsPerImage: 4 }), [true, true, true, true]);
} else {
  console.log('skip: bc1-rgba-unorm srgb view (adapter lacks texture-compression-bc)');
}
console.log(`PASS srgb view formats (${providerInfo().libraryFlavor ?? 'provider'})`);
device.destroy();
