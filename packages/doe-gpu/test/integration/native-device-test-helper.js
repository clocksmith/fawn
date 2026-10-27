// Shared native-device admission for integration entrypoints.

import { setupGlobals } from '../../src/index.js';

const SKIPPABLE_PROVIDER_FAILURE_REASON = 'native_addon_unavailable';
const NATIVE_BACKENDS = Object.freeze({ darwin: 'metal', linux: 'vulkan', win32: 'd3d12' });

function nativeTestBackend(platform = process.platform) {
  const backend = NATIVE_BACKENDS[platform];
  if (!backend) throw new Error(`No native integration backend for ${platform}`);
  return backend;
}

function isSkippableNativeAvailabilityError(error) {
  return error?.providerFailureReason === SKIPPABLE_PROVIDER_FAILURE_REASON;
}

function exitSkipped(label, reason) {
  console.log(`${label}: skipped (${reason})`);
  process.exit(0);
}

async function requestNativeDeviceOrSkip(label) {
  try {
    setupGlobals();
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) {
      exitSkipped(label, 'GPU adapter unavailable');
    }
    return await adapter.requestDevice();
  } catch (error) {
    if (isSkippableNativeAvailabilityError(error)) {
      exitSkipped(label, 'native add-on unavailable');
    }
    throw error;
  }
}

export {
  nativeTestBackend,
  isSkippableNativeAvailabilityError,
  requestNativeDeviceOrSkip,
};
