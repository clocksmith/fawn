#!/usr/bin/env bun
// Exercise the programmable-stage descriptor through Bun's native FFI boundary.
import assert from 'node:assert/strict';
import { globals, providerInfo, requestAdapter } from 'doe-gpu';
import { checkComputeStageBindings } from './compute-bindings-helper.js';

if (providerInfo().bunRuntimeProvider !== 'doe-ffi') {
  console.log('Bun compute override FFI test skipped: this provider uses N-API');
  process.exit(0);
}

const adapter = await requestAdapter();
assert(adapter, 'Bun compute override FFI test requires an adapter');
let device;
try {
  device = await adapter.requestDevice();
  await checkComputeStageBindings(device, globals);
  console.log('Bun FFI dispatch retained both overrides and bind-group changes');
} finally {
  device?.destroy();
  adapter.destroy?.();
}
