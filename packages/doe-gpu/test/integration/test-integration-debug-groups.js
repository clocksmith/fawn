#!/usr/bin/env node
// pushDebugGroup/popDebugGroup must balance on command encoders, compute and
// render passes and render bundle encoders: popping with no open group, or
// ending a pass or finishing an encoder with one open, is a validation error
// at finish(). Compute passes record lazily, so their debug commands also
// cover the materialized pass.

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('debug groups: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUTextureUsage, GPUValidationError } = target;
const texture = device.createTexture({ size: [1, 1, 1], format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT });

function debugCommands(encoder, push, pop) {
  for (let index = 0; index < push; index += 1) encoder.pushDebugGroup(`group ${index}`);
  encoder.insertDebugMarker('marker');
  for (let index = 0; index < pop; index += 1) encoder.popDebugGroup();
}

const kinds = {
  encoder(push, pop) {
    const encoder = device.createCommandEncoder();
    debugCommands(encoder, push, pop);
    encoder.finish();
  },
  'compute pass'(push, pop) {
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginComputePass();
    debugCommands(pass, push, pop);
    pass.end();
    encoder.finish();
  },
  'render pass'(push, pop) {
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
      colorAttachments: [{ view: texture.createView(), loadOp: 'clear', storeOp: 'store' }],
    });
    debugCommands(pass, push, pop);
    pass.end();
    encoder.finish();
  },
  'render bundle'(push, pop) {
    const encoder = device.createRenderBundleEncoder({ colorFormats: ['rgba8unorm'] });
    debugCommands(encoder, push, pop);
    encoder.finish();
  },
};

for (const [kind, encode] of Object.entries(kinds)) {
  for (const [push, pop] of [[0, 0], [2, 2], [1, 0], [0, 1], [1, 2]]) {
    device.pushErrorScope('validation');
    encode(push, pop);
    const error = await device.popErrorScope();
    if (push === pop) {
      assert.equal(error, null, `${kind}: ${push} balanced debug groups are valid`);
    } else {
      assert(error instanceof GPUValidationError, `${kind}: ${push} pushes and ${pop} pops is a validation error`);
    }
  }
}
console.log('ok: debug groups balance on encoders, compute and render passes and render bundles');
device.destroy();
