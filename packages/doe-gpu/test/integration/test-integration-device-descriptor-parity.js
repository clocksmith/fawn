#!/usr/bin/env node
// The same descriptor admission runs before standard and native-direct provider calls.
import assert from 'node:assert/strict';
import { createDeviceDescriptorEntryPoints } from './device-descriptor-test-helper.js';
import { loadNativeAddon } from './native-addon-test-helper.js';

const addon = await loadNativeAddon();
const { entryPoints, getAdapterInfo } = createDeviceDescriptorEntryPoints(addon);

for (const entry of entryPoints) {
  const omittedLimits = await entry.request({ requiredLimits: { maxBindGroups: undefined } });
  assert.ok(omittedLimits.maxBindGroups > 0, `${entry.name}: omitted limit preserves default maxBindGroups`);
  assert.ok(omittedLimits.minUniformBufferOffsetAlignment > 0, `${entry.name}: absent alignment limit stays undefined rather than zero`);
  const fractionalLimits = await entry.request({ requiredLimits: {
    maxBindGroups: omittedLimits.maxBindGroups + 0.75,
    maxBufferSize: omittedLimits.maxBufferSize + 0.75,
  } });
  assert.equal(fractionalLimits.maxBindGroups, omittedLimits.maxBindGroups,
    `${entry.name}: representable positive u32 fractions retain truncation`);
  assert.equal(fractionalLimits.maxBufferSize, omittedLimits.maxBufferSize,
    `${entry.name}: representable positive u64 fractions retain truncation`);

  for (const value of [NaN, Infinity, -Infinity, -1, 2 ** 32, Number.MAX_VALUE]) {
    await assert.rejects(
      () => entry.request({ requiredLimits: { maxBindGroups: value } }),
      /descriptor\.requiredLimits/,
      `${entry.name}: unrepresentable u32 limit ${String(value)} is rejected before narrowing`,
    );
  }
  for (const value of [NaN, Infinity, -Infinity, 2 ** 64, Number.MAX_VALUE]) {
    await assert.rejects(
      () => entry.request({ requiredLimits: { maxBufferSize: value } }),
      /descriptor\.requiredLimits/,
      `${entry.name}: unrepresentable u64 limit ${String(value)} is rejected before narrowing`,
    );
  }
  await assert.rejects(
    () => entry.request({ requiredFeatures: ['shader-f16\0ignored'] }),
    /descriptor\.requiredFeatures contains an unsupported feature/,
    `${entry.name}: a known feature followed by embedded NUL and extra bytes is not an exact feature name`,
  );
}
console.log('device descriptor parity adapter:', JSON.stringify(getAdapterInfo()));
console.log('ok: standard and native-direct device descriptors preserve omission/fractions and reject non-finite, wrapping and NUL-suffixed values');

