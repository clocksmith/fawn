#!/usr/bin/env node
// setImmediates validation on every provider: dataOffset and size count
// elements, a data range past the source or a byte size that is not a multiple
// of 4 throws OperationError (content timeline), and an unaligned or
// out-of-range rangeOffset invalidates the encoder (validation error at finish).

import assert from 'node:assert/strict';
import { providerInfo } from '../../src/index.js';
import { requestNativeDeviceOrSkip } from './native-device-test-helper.js';

const device = await requestNativeDeviceOrSkip('setImmediates');
const limit = device.limits.maxImmediateSize;

async function finishError(record) {
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  record(pass);
  pass.end();
  device.pushErrorScope('validation');
  encoder.finish();
  return device.popErrorScope();
}

function expectOperationError(label, args) {
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  assert.throws(() => pass.setImmediates(...args), (error) => error?.name === 'OperationError', label);
  pass.end();
}
expectOperationError('3-byte content', [0, new Uint8Array(3)]);
expectOperationError('data range past the source', [0, new Uint32Array(4), 3, 2]);
console.log('ok: content-timeline errors throw OperationError');

if (providerInfo().libraryFlavor === 'doe-c') {
  assert.equal(await finishError((pass) => pass.setImmediates(0, new Uint32Array(4))), null, 'aligned range');
  assert.equal(await finishError((pass) => pass.setImmediates(limit - 8, new Uint32Array(2))), null, 'range ending at the limit');
  // Elements, not bytes: offset 1 and size 2 of a 4-element Uint32Array is valid.
  assert.equal(await finishError((pass) => pass.setImmediates(0, new Uint32Array(4), 1, 2)), null, 'element offsets');
  console.log('ok: valid ranges record without errors');
  assert.ok(await finishError((pass) => pass.setImmediates(2, new Uint32Array(1))), 'unaligned rangeOffset');
  assert.ok(await finishError((pass) => pass.setImmediates(limit - 4, new Uint32Array(2))), 'range past maxImmediateSize');
  console.log('ok: rangeOffset alignment and maxImmediateSize invalidate the encoder');
} else {
  // The Zig drop-in reports maxImmediateSize 64 but rejects every setImmediates.
  console.log(`known gap: ${providerInfo().libraryFlavor} rejects all immediate data; device-timeline checks run on C`);
}

device.destroy();
