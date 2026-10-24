#!/usr/bin/env node
// copyTextureToTexture aspects reach the provider: a depth16unorm copy that
// names 'depth-only' on both sides moves the depth texels, and 'stencil-only'
// on a depth-only format invalidates the encoder at finish().

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('texture copy aspects: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!adapter) {
  console.log('texture copy aspects: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUBufferUsage: B, GPUTextureUsage: T, GPUMapMode: M } = globalThis;
const size = { width: 4, height: 2 };
const format = 'depth16unorm';
const texels = new Uint16Array([1, 2, 3, 0x7fff, 0x8000, 0xfffe, 0xffff, 42]);

const source = device.createTexture({ size, format, usage: T.COPY_SRC | T.COPY_DST });
const destination = device.createTexture({ size, format, usage: T.COPY_SRC | T.COPY_DST });
device.queue.writeTexture({ texture: source, aspect: 'depth-only' }, texels, { bytesPerRow: 8 }, size);

const readback = device.createBuffer({ size: 512, usage: B.COPY_DST | B.MAP_READ });
const encoder = device.createCommandEncoder();
encoder.copyTextureToTexture(
  { texture: source, aspect: 'depth-only' },
  { texture: destination, aspect: 'depth-only' },
  size,
);
encoder.copyTextureToBuffer({ texture: destination, aspect: 'depth-only' }, { buffer: readback, bytesPerRow: 256 }, size);
device.queue.submit([encoder.finish()]);
await readback.mapAsync(M.READ);
const mapped = new Uint8Array(readback.getMappedRange());
const rows = [0, 1].map(row => Array.from(new Uint16Array(mapped.slice(row * 256, row * 256 + 8).buffer)));
readback.unmap();
assert.deepEqual(rows.flat(), Array.from(texels));
console.log('ok: depth-only texture-to-texture copy moves depth16unorm texels');

device.pushErrorScope('validation');
const invalid = device.createCommandEncoder();
invalid.copyTextureToTexture({ texture: source, aspect: 'stencil-only' }, { texture: destination }, size);
invalid.finish();
const error = await device.popErrorScope();
assert.ok(error, 'stencil-only aspect on depth16unorm must be a validation error');
console.log('ok: stencil-only aspect on a depth-only format is rejected at finish');

device.destroy();
