#!/usr/bin/env node
// copyTextureToTexture within one texture (WebGPU): disjoint subresources, a
// different mip level or disjoint array layers, copy and read back the source
// texels; the same mip level with overlapping layers invalidates the encoder.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('same-texture copy: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!adapter) {
  console.log('same-texture copy: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUMapMode: M } = globalThis;

async function scoped(fn) {
  device.pushErrorScope('validation');
  const value = fn();
  return { value, error: await device.popErrorScope() };
}

// A 2x2x2 rgba8unorm texture with 2 mips; layer 0 of mip 0 holds 9s.
function filled() {
  const texture = device.createTexture({ size: [2, 2, 2], format: 'rgba8unorm', mipLevelCount: 2,
    usage: T.COPY_SRC | T.COPY_DST });
  device.queue.writeTexture({ texture }, new Uint8Array(16).fill(9), { bytesPerRow: 8 }, [2, 2, 1]);
  return texture;
}

async function copyAndRead(name, source, destination, size) {
  const texture = filled();
  const readback = device.createBuffer({ size: 4, usage: B.COPY_DST | B.MAP_READ });
  const encoder = device.createCommandEncoder();
  encoder.copyTextureToTexture({ texture, ...source }, { texture, ...destination }, size);
  encoder.copyTextureToBuffer({ texture, ...destination }, { buffer: readback, bytesPerRow: 256 }, [1, 1]);
  const submitted = await scoped(() => device.queue.submit([encoder.finish()]));
  assert.equal(submitted.error, null, `${name}: copy within one texture is valid`);
  await readback.mapAsync(M.READ);
  assert.deepEqual(Array.from(new Uint8Array(readback.getMappedRange())), [9, 9, 9, 9], `${name}: copied texels`);
  readback.unmap();
  texture.destroy();
  console.log(`ok: ${name} copies within one texture`);
}

await copyAndRead('mip 0 to mip 1', { mipLevel: 0 }, { mipLevel: 1 }, [1, 1]);
await copyAndRead('layer 0 to layer 1', { origin: [0, 0, 0] }, { origin: [0, 0, 1] }, [2, 2, 1]);

{
  const texture = filled();
  const encoder = device.createCommandEncoder();
  encoder.copyTextureToTexture({ texture, origin: [0, 0, 0] }, { texture, origin: [0, 0, 1] }, [2, 2, 2]);
  const finished = await scoped(() => encoder.finish());
  assert.ok(finished.error, 'overlapping layers of one mip are a validation error');
  texture.destroy();
  console.log('ok: overlapping layers of one mip invalidate the encoder');
}
device.destroy();
