#!/usr/bin/env node
// navigator.gpu.wgslLanguageFeatures is the provider instance's own report: it is
// the same object on every read, and a `requires` directive is accepted exactly
// for the advertised names and rejected for every other pinned name.

import assert from 'node:assert/strict';
import { create, providerInfo } from '../../src/index.js';
import { WGSL_LANGUAGE_FEATURE_VALUES } from '../../src/vendor/webgpu/shared/capabilities.js';
import { requestNativeDeviceOrSkip } from './native-device-test-helper.js';

const device = await requestNativeDeviceOrSkip('WGSL language features');
const gpu = create();
const advertised = gpu.wgslLanguageFeatures;
assert.equal(gpu.wgslLanguageFeatures, advertised, 'wgslLanguageFeatures is the same object on every read');
const pinned = new Set(WGSL_LANGUAGE_FEATURE_VALUES.map(([name]) => name));
for (const name of advertised) assert.ok(pinned.has(name), `${name} is a pinned WGSL language feature`);

const body = '@group(0) @binding(0) var<storage, read_write> d: array<u32>;\n'
  + '@compute @workgroup_size(1) fn main() { d[0] = 1u; }';
async function accepted(name) {
  device.pushErrorScope('validation');
  const module = device.createShaderModule({ code: `requires ${name};\n${body}` });
  try {
    device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' } });
  } catch {
    // A synchronous pipeline rejection is reported through the error scope.
  }
  return (await device.popErrorScope()) === null;
}
// Known compiler gap: the C compiler accepts `requires subgroup_uniformity`
// (runtime/c/compiler/src/parse.c) while the C instance reports it unsupported.
const knownGaps = providerInfo().libraryFlavor === 'doe-c' ? new Set(['subgroup_uniformity']) : new Set();
for (const name of pinned) {
  const result = await accepted(name);
  if (knownGaps.has(name) && result && !advertised.has(name)) {
    console.log(`known gap: ${providerInfo().libraryFlavor} accepts requires ${name} without advertising it`);
    continue;
  }
  assert.equal(result, advertised.has(name), `requires ${name} is ${advertised.has(name) ? 'accepted' : 'rejected'}`);
}
console.log(`ok: ${providerInfo().libraryFlavor} advertises [${[...advertised].join(', ')}] and its compiler agrees`);
device.destroy();
