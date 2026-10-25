#!/usr/bin/env node
// Provider-neutral control: READ mapping mutations must not reach later maps.
// Pass an explicit module URL to compare another provider; default is Doe.
// https://gpuweb.github.io/gpuweb/#dom-gpumapmode-read
import assert from 'node:assert/strict';

const moduleUrl = process.argv[2] ?? new URL('../../src/index.js', import.meta.url).href;
const { create, globals } = await import(moduleUrl);
const gpu = create([]);
const adapter = await gpu.requestAdapter({ powerPreference: 'high-performance' });
assert(adapter, 'a real provider adapter is required');
const device = await adapter.requestDevice();
const buffer = device.createBuffer({
  size: 16,
  usage: globals.GPUBufferUsage.MAP_READ | globals.GPUBufferUsage.COPY_DST,
});
try {
  const expected = [42, 43, 44, 45];
  device.queue.writeBuffer(buffer, 0, new Uint32Array(expected));
  await buffer.mapAsync(globals.GPUMapMode.READ);
  const first = buffer.getMappedRange();
  assert.deepEqual(Array.from(new Uint32Array(first)), expected);
  new Uint32Array(first)[0] = 999;
  buffer.unmap();
  assert.equal(first.byteLength, 0, 'unmap detaches the returned ArrayBuffer');
  await buffer.mapAsync(globals.GPUMapMode.READ);
  const second = buffer.getMappedRange();
  const actual = Array.from(new Uint32Array(second));
  buffer.unmap();
  assert.equal(second.byteLength, 0, 'remapped ArrayBuffer also detaches');
  console.log(JSON.stringify({ expected, actual, detached: true }));
  assert.deepEqual(actual, expected, 'READ mapping mutations are discarded, not committed');
} finally {
  buffer.destroy();
  device.destroy();
}
