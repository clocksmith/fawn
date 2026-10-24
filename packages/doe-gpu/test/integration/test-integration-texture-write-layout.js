#!/usr/bin/env node
// queue.writeTexture data needs no texel-aligned offset or bytesPerRow, and
// buffer/texture copies with a zero extent are valid no-ops.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('texture write layout: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!adapter) {
  console.log('texture write layout: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUMapMode: M } = globalThis;
const size = { width: 3, height: 2, depthOrArrayLayers: 2 };
const texture = device.createTexture({ size, format: 'rg8unorm', usage: T.COPY_SRC | T.COPY_DST });

// rg8unorm rows of 6 bytes at an odd offset and an odd 7-byte pitch, with a
// padded image of 3 rows; every byte is distinct.
const offset = 3, bytesPerRow = 7, rowsPerImage = 3;
const data = new Uint8Array(offset + bytesPerRow * rowsPerImage * 2).map((_, i) => (i * 7 + 1) & 0xff);
device.queue.writeTexture({ texture }, data, { offset, bytesPerRow, rowsPerImage }, size);

const readback = device.createBuffer({ size: 256 * 4, usage: B.COPY_DST | B.MAP_READ });
const upload = device.createBuffer({ size: 256 * 2, usage: B.COPY_SRC });
device.pushErrorScope('validation');
const encoder = device.createCommandEncoder();
encoder.copyTextureToBuffer({ texture }, { buffer: readback, bytesPerRow: 256, rowsPerImage: 2 }, size);
for (const empty of [{ width: 0, height: 2 }, { width: 3, height: 0 }, { width: 3, height: 2, depthOrArrayLayers: 0 }]) {
  encoder.copyBufferToTexture({ buffer: upload, bytesPerRow: 256 }, { texture }, empty);
  encoder.copyTextureToBuffer({ texture }, { buffer: readback, bytesPerRow: 256 }, empty);
}
device.queue.submit([encoder.finish()]);
assert.equal(await device.popErrorScope(), null);
await readback.mapAsync(M.READ);
const mapped = new Uint8Array(readback.getMappedRange());
for (let layer = 0; layer < 2; layer++) {
  for (let row = 0; row < 2; row++) {
    const from = offset + (layer * rowsPerImage + row) * bytesPerRow;
    const to = (layer * 2 + row) * 256;
    assert.deepEqual(Array.from(mapped.slice(to, to + 6)), Array.from(data.slice(from, from + 6)), `layer ${layer} row ${row}`);
  }
}
readback.unmap();
console.log('ok: writeTexture with unaligned offset and bytesPerRow lands every row');
console.log('ok: zero-extent buffer/texture copies are accepted no-ops');
device.destroy();
