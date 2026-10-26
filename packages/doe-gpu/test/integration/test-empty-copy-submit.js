// Empty copies still require live resources at submission. Optional provider
// module URL permits the same control on retained Zig or pinned npm Dawn.
import assert from 'node:assert/strict';

const moduleUrl = process.argv[2] ?? new URL('../../src/index.js', import.meta.url).href;
const { create, globals } = await import(moduleUrl);
const adapter = await create([]).requestAdapter();
assert(adapter, 'provider adapter required');
const device = await adapter.requestDevice();
const usage = globals.GPUBufferUsage;
const textureUsage = globals.GPUTextureUsage;
const results = [];
try {
  for (const direction of ['buffer-texture', 'texture-buffer']) {
    for (const axis of [0, 1, 2]) for (const destroyed of ['buffer', 'texture']) {
      const texture = device.createTexture({ size: [1, 1, 1], dimension: '2d', format: 'rgba8unorm',
        usage: textureUsage.COPY_SRC | textureUsage.COPY_DST });
      const buffer = device.createBuffer({ size: 256, usage: usage.COPY_SRC | usage.COPY_DST });
      try {
        const encoder = device.createCommandEncoder();
        const size = [1, 1, 1];
        size[axis] = 0;
        const bufferCopy = { buffer, offset: 0, bytesPerRow: 256, rowsPerImage: 1 };
        if (direction === 'buffer-texture') encoder.copyBufferToTexture(bufferCopy, { texture }, size);
        else encoder.copyTextureToBuffer({ texture }, bufferCopy, size);
        const commands = encoder.finish();
        (destroyed === 'buffer' ? buffer : texture).destroy();
        device.pushErrorScope('validation');
        device.queue.submit([commands]);
        const error = await device.popErrorScope();
        results.push({ direction, axis, destroyed, rejected: Boolean(error) });
      } finally {
        buffer.destroy();
        texture.destroy();
      }
    }
  }
  console.log(JSON.stringify({ results }));
  assert.equal(results.length, 12);
  assert(results.every(result => result.rejected), 'empty copies must retain submission resource validation');
} finally {
  device.destroy();
}
