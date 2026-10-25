// Native getter access must match mapping mode, without a rejected probe.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

if (!process.argv.includes('--child')) {
  const result = spawnSync(process.execPath, [fileURLToPath(import.meta.url), '--child'], {
    encoding: 'utf8', timeout: 20000, env: process.env,
  });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr || result.stdout);
  assert.equal(result.stderr, '', 'valid mapping must not probe an invalid mutable getter');
  process.stdout.write(result.stdout);
} else {
  const { createNativeDirect, globals } = await import('../../src/native.js');
  const adapter = await createNativeDirect().requestAdapter();
  assert(adapter, 'provider adapter required');
  const device = await adapter.requestDevice();
  const { GPUBufferUsage: usage, GPUMapMode: mode } = globals;
  const expected = new Uint32Array([17, 29, 43, 71]);
  const source = device.createBuffer({ size: 16, usage: usage.MAP_WRITE | usage.COPY_SRC,
    mappedAtCreation: true });
  const target = device.createBuffer({ size: 16, usage: usage.MAP_READ | usage.COPY_DST });
  try {
    for (const remap of [false, true]) {
      if (remap) await source.mapAsync(mode.WRITE);
      const range = source.getMappedRange();
      new Uint32Array(range).set(expected);
      source.unmap();
      assert.equal(range.byteLength, 0);
      const encoder = device.createCommandEncoder();
      encoder.copyBufferToBuffer(source, 0, target, 0, 16);
      device.queue.submit([encoder.finish()]);
      await target.mapAsync(mode.READ);
      const read = target.getMappedRange();
      assert.deepEqual(new Uint32Array(read).slice(), expected);
      assert.deepEqual(new Uint32Array(target._readCopy(0, 16)), expected);
      new Uint32Array(read).fill(999);
      target.unmap();
      assert.equal(read.byteLength, 0);
      assert.deepEqual(new Uint32Array(target._mapReadCopyUnmap(mode.READ, 0, 16)), expected);
    }
    console.log('ok: direct READ/WRITE getters, copies, detachment, and discarded READ mutations');
  } finally {
    source.destroy();
    target.destroy();
    device.destroy();
    adapter.destroy();
  }
}
