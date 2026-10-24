#!/usr/bin/env node
// Native-direct compute-pass setImmediates reaches the provider on both
// runtimes: through doeNativeComputePassSetImmediates on Zig and
// wgpuComputePassEncoderSetImmediates on C (runtime/bridge/webgpu-addon
// AGENTS.md: call the exact webgpu.h procedure or throw, never a silent
// no-op). A native-direct device also routes its provider errors to the
// addon's error log on providers without the Doe setters, so an invalid
// range is observable. Query set labels reach the provider the same way.

import assert from 'node:assert/strict';
import { createNativeDirect } from '../../src/native.js';
import { setupGlobals } from '../../src/index.js';
import { loadNativeAddon } from './native-addon-test-helper.js';

const addon = await loadNativeAddon();
const nativeAdapter = await createNativeDirect().requestAdapter().catch(() => null);
if (!nativeAdapter) {
  console.log('native-direct setters: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await nativeAdapter.requestDevice();
assert.equal(addon.deviceSetUncapturedErrorCallback(device.__doe_native, () => {}), true,
  'the native-direct device takes an uncaptured-error binding');

async function immediatesErrors(offset, bytes) {
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setImmediates(offset, new Uint8Array(bytes));
  pass.end();
  encoder.finish();
  await new Promise((resolve) => setTimeout(resolve, 20));
  return addon.deviceTakeErrors(device.__doe_native);
}

assert.deepEqual(await immediatesErrors(0, 4), [], 'an aligned setImmediates range within maxImmediateSize is valid');
const invalid = await immediatesErrors(2, 4);
assert.equal(invalid.length, 1, 'an unaligned setImmediates offset reaches the provider and invalidates the encoder');
assert.equal(invalid[0].type, 'validation');
console.log('ok: native-direct compute setImmediates reaches the provider');

// Query set labels go to the provider's label store or wgpuQuerySetSetLabel.
const target = {};
setupGlobals(target);
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
const gpuDevice = await adapter.requestDevice();
gpuDevice.pushErrorScope('validation');
const querySet = gpuDevice.createQuerySet({ type: 'occlusion', count: 1, label: 'labeled queries' });
assert.equal(await gpuDevice.popErrorScope(), null, 'a labeled query set is created without errors');
assert.equal(querySet.label, 'labeled queries');
console.log('ok: query set labels reach the provider');
gpuDevice.destroy();
device.destroy?.();
