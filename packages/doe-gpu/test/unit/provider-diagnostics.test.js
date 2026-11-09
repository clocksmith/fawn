#!/usr/bin/env node

import assert from 'node:assert/strict';

import { providerDiagnostics } from '../../src/vendor/webgpu/index.js';
import {
  QUEUE_SUBMIT_BREAKDOWN_FIELDS,
  zeroQueueSubmitBreakdown,
} from '../../src/vendor/webgpu/shared/submit-breakdown.js';

const snapshot = providerDiagnostics();
assert.deepEqual(Object.keys(snapshot.fastPathStats).sort(), [
  'commandBufferBuild',
  'dispatchFlush',
  'flushAndMap',
]);
for (const field of [
  'queueSubmitCalls',
  'submittedCommandBuffers',
  'submittedBatchedCommands',
  'queueWriteBufferCalls',
  'queueWriteBufferBytes',
  'queueWriteBufferTotalNs',
  'queueWriteBufferBatchCalls',
  'queueWriteBufferBatchTotalNs',
  'mapReadCalls',
]) {
  assert.equal(typeof snapshot[field], 'number', field);
  assert.ok(snapshot[field] >= 0, field);
}
assert.equal(typeof snapshot.submitBreakdownNs, 'object');
assert.deepEqual(Object.keys(snapshot.submitBreakdownNs), [...QUEUE_SUBMIT_BREAKDOWN_FIELDS]);
assert.deepEqual(zeroQueueSubmitBreakdown(), Object.fromEntries(
  QUEUE_SUBMIT_BREAKDOWN_FIELDS.map((field) => [field, 0]),
));
assert.equal(typeof snapshot.submitBreakdownNs.submitAddonCallTotalNs, 'number');

console.log('provider-diagnostics.test: ok');
