#!/usr/bin/env node
// Explicit C-provider CPU WARP developer control; never a fallback or hardware gate.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, realpathSync } from 'node:fs';
import { isAbsolute } from 'node:path';

const EXPECTED_OUTPUT = Object.freeze([2, 4, 6, 8, 10, 12, 14, 16]);
const EXPECTED_OUTPUT_SHA256 = '9d42cad41af4aaf3ae973e5a48d96f61e4708edc57bf7dd29a497ebd96f506cf';
const EXPECTED_WGSL_SHA256 = 'f03b12ba1e7df16b58babac43b326771a30d3e7de9278bfcd3daa8d95faf80b2';
const EXPECTED_INPUT_SHA256 = 'af7de0621354bafceb193edf0fcf5d421cf21de7146580062fff53c7907f54e5';
const WARP_DEVICE = 'Microsoft Basic Render Driver';

assert.equal(process.platform, 'win32', 'This control requires actual Windows and CPU WARP');
assert(process.env.DOE_WEBGPU_LIB && isAbsolute(process.env.DOE_WEBGPU_LIB),
  'Set DOE_WEBGPU_LIB to the exact absolute C provider artifact');
const selectedPath = realpathSync(process.env.DOE_WEBGPU_LIB);
const selectedSha256 = createHash('sha256').update(readFileSync(selectedPath)).digest('hex');
const { providerInfo } = await import('doe-gpu/native');
const before = providerInfo();
assert.equal(before.loaded, true);
assert.equal(before.doeNative, true);
assert.equal(before.libraryFlavor, 'doe-c');
assert.equal(realpathSync(before.doeLibraryPath), selectedPath);
const { runFirstKernel } = await import('../../examples/first-kernel.js');
let adapterInfo;
const receipt = await runFirstKernel('node', async (device, adapter) => {
  adapterInfo = { ...adapter.info };
  assert.equal(adapterInfo.isFallbackAdapter, true, 'Actual selected adapter must be CPU');
  assert.equal(adapterInfo.device, WARP_DEVICE, 'Actual selected adapter must be Microsoft WARP');
  assert.equal(typeof device.destroy, 'function');
  assert.equal(typeof adapter.destroy, 'function');
}, { forceFallbackAdapter: true });
assert.equal(receipt.provider.loaded, true);
assert.equal(receipt.provider.doeNative, true);
assert.equal(receipt.provider.libraryFlavor, 'doe-c');
assert.equal(realpathSync(receipt.provider.doeLibraryPath), selectedPath);
assert.equal(createHash('sha256').update(readFileSync(selectedPath)).digest('hex'), selectedSha256,
  'Selected provider artifact changed during execution');
assert.deepEqual(receipt.result.output, EXPECTED_OUTPUT);
assert.equal(receipt.result.outputSha256, EXPECTED_OUTPUT_SHA256);
assert.equal(receipt.workload.wgslSha256, EXPECTED_WGSL_SHA256);
assert.equal(receipt.workload.inputSha256, EXPECTED_INPUT_SHA256);
console.log(JSON.stringify({
  kind: 'doe-gpu.c-provider-warp.developer',
  schemaVersion: 1,
  scope: 'Node public JS -> selected C provider -> Microsoft CPU WARP; no physical/performance qualification',
  node: process.version,
  architecture: process.arch,
  providerArtifact: { path: selectedPath, sha256: selectedSha256 },
  adapter: adapterInfo,
  backendObservation: 'Numeric backend enum is not exposed by the current public JS adapter info; bind the matching native C WARP control separately',
  cleanup: 'Existing first-kernel finally returned after device.destroy and adapter.destroy',
  kernel: receipt,
}, null, 2));
