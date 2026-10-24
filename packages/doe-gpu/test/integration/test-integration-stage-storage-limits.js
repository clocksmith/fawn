#!/usr/bin/env node
// Stage-specific storage limits (WebGPU "a new device"): a required
// maxStorage{Buffers,Textures}In{Vertex,Fragment}Stage raises the per-stage
// limit; on an adapter with core-features-and-limits the stage-specific limits
// then equal the per-stage limit. The native device receives the raised value:
// a fragment-visible layout at the adapter's limit is valid (a native device at
// the default per-stage limit rejects it when the adapter exceeds the default).

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

async function adapter() {
  return await navigator.gpu.requestAdapter() ?? await navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
}

let first;
try {
  setupGlobals();
  first = await adapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('stage storage limits: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
if (!first) {
  console.log('stage storage limits: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const S = globalThis.GPUShaderStage;
const cases = [
  ['maxStorageBuffersPerShaderStage', 'maxStorageBuffersInFragmentStage', 'maxStorageBuffersInVertexStage',
    (binding) => ({ binding, visibility: S.FRAGMENT, buffer: { type: 'storage' } })],
  ['maxStorageTexturesPerShaderStage', 'maxStorageTexturesInFragmentStage', 'maxStorageTexturesInVertexStage',
    (binding) => ({ binding, visibility: S.FRAGMENT, storageTexture: { access: 'write-only', format: 'r32uint' } })],
];
let gpuAdapter = first;
for (const [perStage, fragment, vertex, entry] of cases) {
  for (const required of [fragment, perStage]) {
    gpuAdapter ??= await adapter();
    const value = gpuAdapter.limits[required];
    const core = gpuAdapter.features.has('core-features-and-limits');
    assert.equal(gpuAdapter.limits[fragment], gpuAdapter.limits[perStage]);
    const device = await gpuAdapter.requestDevice({ requiredLimits: { [required]: value } });
    gpuAdapter = null;
    for (const name of core ? [perStage, fragment, vertex] : [perStage, required]) {
      assert.equal(device.limits[name], value, `${required} -> ${name}`);
    }
    device.pushErrorScope('validation');
    device.createBindGroupLayout({ entries: Array.from({ length: value }, (_, i) => entry(i)) });
    assert.equal(await device.popErrorScope(), null, `${value} fragment entries at ${required}`);
    device.destroy();
    console.log(`ok: requiring ${required}=${value} sets ${perStage}${core ? `, ${fragment} and ${vertex}` : ''}; the device admits that many`);
  }
}
