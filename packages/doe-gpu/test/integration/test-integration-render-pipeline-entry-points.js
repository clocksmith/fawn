#!/usr/bin/env node
// A render pipeline whose vertex or fragment entry point does not name an
// entry point of that stage in its module is invalid
// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpuprogrammablestage):
// createRenderPipeline reports a validation error and createRenderPipelineAsync
// rejects with a GPUPipelineError whose reason is "validation".

import assert from 'node:assert/strict';
import { setupGlobals } from '../../src/index.js';

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('render pipeline entry points: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUPipelineError, GPUValidationError } = target;
const vertex = device.createShaderModule({ code: '@vertex fn main() -> @builtin(position) vec4f { return vec4f(0.0, 0.0, 0.0, 1.0); }' });
const fragment = device.createShaderModule({ code: '@fragment fn main() -> @location(0) vec4f { return vec4f(1.0); }' });
const descriptor = (vertexEntry, fragmentEntry) => ({
  layout: 'auto',
  vertex: { module: vertex, entryPoint: vertexEntry },
  fragment: { module: fragment, entryPoint: fragmentEntry, targets: [{ format: 'rgba8unorm' }] },
});

for (const [label, vertexEntry, fragmentEntry, valid] of [
  ['existing entry points', 'main', 'main', true],
  ['a missing fragment entry point', 'main', 'does_not_exist', false],
  ['a missing vertex entry point', 'does_not_exist', 'main', false],
]) {
  device.pushErrorScope('validation');
  device.createRenderPipeline(descriptor(vertexEntry, fragmentEntry));
  const error = await device.popErrorScope();
  if (valid) assert.equal(error, null, `createRenderPipeline with ${label} is valid`);
  else assert(error instanceof GPUValidationError, `createRenderPipeline with ${label} is a validation error`);

  const result = await device.createRenderPipelineAsync(descriptor(vertexEntry, fragmentEntry)).then(
    () => null,
    (reason) => reason,
  );
  if (valid) {
    assert.equal(result, null, `createRenderPipelineAsync with ${label} resolves`);
  } else {
    assert(result instanceof GPUPipelineError, `createRenderPipelineAsync with ${label} rejects with GPUPipelineError`);
    assert.equal(result.reason, 'validation');
  }
}
console.log('ok: render pipelines reject entry points their modules do not define');
device.destroy();
