#!/usr/bin/env node
// WebGPU's fallback adapter is the CPU implementation. A forceFallbackAdapter
// request resolves to an adapter flagged isFallbackAdapter, or to null when
// there is none. When the process selects only lavapipe (VK_ICD_FILENAMES
// names lvp_icd), the default adapter is that CPU adapter, flagged as the
// fallback, and the forced request returns it with a working device.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';
import { isSkippableNativeAvailabilityError } from './native-device-test-helper.js';

let adapter;
try {
  setupGlobals();
  adapter = await navigator.gpu.requestAdapter();
} catch (error) {
  if (isSkippableNativeAvailabilityError(error)) {
    console.log('fallback adapter: skipped (native add-on unavailable)');
    process.exit(0);
  }
  throw error;
}
const forced = await navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (forced) assert.equal(forced.info.isFallbackAdapter, true, 'a forced request returns only a fallback adapter');
const cpuOnly = /lvp_icd/.test(process.env.VK_ICD_FILENAMES ?? '');
if (cpuOnly) {
  assert.ok(adapter, 'lavapipe is the default adapter on a CPU-only Vulkan host');
  assert.equal(adapter.info.isFallbackAdapter, true, 'the CPU adapter is the fallback adapter');
  assert.ok(forced, 'forceFallbackAdapter returns the CPU adapter');
  const device = await forced.requestDevice();
  assert.ok(device);
  device.destroy();
  console.log('ok: lavapipe is the fallback adapter for default and forced requests');
} else {
  console.log(`ok: forced request returned ${forced ? 'a fallback adapter' : 'null'}`);
}
