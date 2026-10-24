#!/usr/bin/env node
// A consumed adapter's native object is released once it has produced a
// device: repeated requestAdapter/requestDevice/destroy cycles keep a flat
// count of open file descriptors (each native adapter opens backend handles,
// e.g. a Vulkan context on the C provider). Linux only (/proc/self/fd).

import assert from 'node:assert/strict';
import { readdirSync } from 'node:fs';
import { create } from '../../src/index.js';

if (process.platform !== 'linux') {
  console.log('adapter release: skipped (needs /proc/self/fd)');
  process.exit(0);
}

const gpu = create();
const openDescriptors = () => readdirSync('/proc/self/fd').length;
async function cycle() {
  const adapter = await gpu.requestAdapter() ?? await gpu.requestAdapter({ forceFallbackAdapter: true });
  if (!adapter) return false;
  const device = await adapter.requestDevice();
  assert.ok(adapter.features.size >= 0 && adapter.limits.maxBindGroups > 0 && typeof adapter.info.vendor === 'string',
    'a consumed adapter still answers features, limits and info');
  await assert.rejects(adapter.requestDevice(), (error) => error?.name === 'OperationError');
  device.destroy();
  return true;
}

if (!(await cycle())) {
  console.log('adapter release: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const before = openDescriptors();
for (let i = 0; i < 100; i += 1) await cycle();
const after = openDescriptors();
assert.ok(after - before < 20, `100 adapter/device cycles grew open descriptors from ${before} to ${after}`);
console.log(`ok: 100 adapter/device cycles keep open descriptors flat (${before} -> ${after})`);
