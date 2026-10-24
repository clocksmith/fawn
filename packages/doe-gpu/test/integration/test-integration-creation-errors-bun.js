#!/usr/bin/env bun
// Bun runs the full surface without the Node addon's provider error log, so
// a creation the provider rejects without reporting must still report one
// error (review of 9b7f5819b, finding 3): createView with baseMipLevel past
// the texture's mip count.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/bun.js';

setupGlobals();
const adapter = await navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('creation errors (bun): skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const texture = device.createTexture({ size: [4, 4], format: 'rgba8unorm', usage: globalThis.GPUTextureUsage.TEXTURE_BINDING });

device.pushErrorScope('validation');
const view = texture.createView({ baseMipLevel: 3 });
const error = await device.popErrorScope();
assert(error instanceof globalThis.GPUValidationError, `the rejected view is reported, got ${error}`);
assert.equal(view._invalid, true, 'the view is invalid');

const events = [];
device.addEventListener('uncapturederror', (event) => events.push(event.error));
texture.createView({ baseMipLevel: 3 });
await new Promise((resolve) => setTimeout(resolve, 20));
assert.equal(events.length, 1, `one uncapturederror, got ${events.length}`);
console.log('ok: a provider rejection reports one error on Bun');
device.destroy();
