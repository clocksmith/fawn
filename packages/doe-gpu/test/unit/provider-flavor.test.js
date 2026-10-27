// Producer artifact names retain C/Zig attribution without accepting lookalikes.
import assert from 'node:assert/strict';

import { libraryFlavor } from '../../src/vendor/webgpu/shared/public-surface.js';

for (const name of ['libwebgpu_doe.so', 'libwebgpu_doe.dylib',
  'libwebgpu_doe_full.so', 'libwebgpu_doe_full.dylib', 'libwebgpu_doe_compute.so',
  'libwebgpu_doe_compute.dylib', 'webgpu_doe.dll', 'libwebgpu_doe.dll',
  'webgpu_doe_full.dll', 'webgpu_doe_compute.dll']) {
  assert.equal(libraryFlavor(`/providers/${name}`), 'doe-dropin', name);
  assert.equal(libraryFlavor(`C:\\providers\\${name}`), 'doe-dropin', name);
}
for (const name of ['libwebgpu_doe_c.so', 'libwebgpu_doe_c.dylib',
  'webgpu_doe_c.dll', 'libwebgpu_doe_c.dll']) {
  assert.equal(libraryFlavor(`/providers/${name}`), 'doe-c', name);
  assert.equal(libraryFlavor(`C:\\providers\\${name}`), 'doe-c', name);
}
for (const name of ['evil_libwebgpu_doe.dylib', 'libwebgpu_doe_full_backup.dylib',
  'libwebgpu_doe_c_full.so', 'libwebgpu_doe_full.dylib.bak', 'libwebgpu_doe_full.so.1',
  'libwebgpu_doe_compute.so.malicious', 'webgpu_doe_full.dylib', 'libwebgpu_doe_headless.so']) {
  assert.equal(libraryFlavor(`/providers/${name}`), 'unknown', name);
}
assert.equal(libraryFlavor('/providers/libwebgpu_dawn.dylib'), 'delegate');
assert.equal(libraryFlavor(''), 'missing');
console.log('provider artifact flavor controls passed');
