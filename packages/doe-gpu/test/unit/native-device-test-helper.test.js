import assert from 'node:assert/strict';

import {
  isSkippableNativeAvailabilityError,
  nativeTestBackend,
} from '../integration/native-device-test-helper.js';

assert.equal(
  isSkippableNativeAvailabilityError({
    providerFailureReason: 'native_addon_unavailable',
  }),
  true,
);
assert.equal(
  isSkippableNativeAvailabilityError({
    providerFailureReason: 'runtime_library_unavailable',
  }),
  false,
);
assert.equal(
  isSkippableNativeAvailabilityError(new Error('native addon unavailable')),
  false,
);

assert.equal(nativeTestBackend('win32'), 'd3d12');
assert.equal(nativeTestBackend('darwin'), 'metal');
assert.equal(nativeTestBackend('linux'), 'vulkan');
assert.throws(() => nativeTestBackend('unknown'), /No native integration backend/);
console.log('native device test helper contract: ok');
