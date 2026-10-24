#!/usr/bin/env node
// A feature that extends another is offered only with it: the sliced 3D
// compression features need their base compression feature. When offered, a
// device with the feature creates a 3D texture of that family.

import assert from 'node:assert/strict';
import { providerInfo, setupGlobals } from '../../src/index.js';

setupGlobals();
const adapter = await navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('feature dependencies: skipped (no adapter)');
  process.exit(0);
}
const offered = adapter.features;
const families = [
  { sliced: 'texture-compression-bc-sliced-3d', base: 'texture-compression-bc', format: 'bc1-rgba-unorm' },
  { sliced: 'texture-compression-astc-sliced-3d', base: 'texture-compression-astc', format: 'astc-4x4-unorm' },
];
for (const { sliced, base } of families) {
  if (offered.has(sliced)) assert.ok(offered.has(base), `${sliced} is offered without ${base}`);
}

const required = families.filter(({ sliced }) => offered.has(sliced)).flatMap(({ sliced, base }) => [sliced, base]);
if (required.length > 0) {
  const device = await adapter.requestDevice({ requiredFeatures: required });
  for (const { sliced, format } of families) {
    if (!offered.has(sliced)) continue;
    device.pushErrorScope('validation');
    const texture = device.createTexture({ size: [4, 4, 2], dimension: '3d', format, usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
    assert.equal(await device.popErrorScope(), null, `${sliced}: a 3D ${format} texture is created`);
    texture.destroy();
  }
  device.destroy();
}
console.log(`ok: sliced 3D compression features come with their base features (${providerInfo().libraryFlavor ?? 'provider'}: ${required.join(', ') || 'none offered'})`);
