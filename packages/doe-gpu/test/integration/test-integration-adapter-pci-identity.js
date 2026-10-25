#!/usr/bin/env node
// GPUAdapter.info carries the adapter's PCI vendorID and deviceID on every
// provider, and driverVersion where the provider reports one
// (doeNativeAdapterGetPciIdentity). With a single Vulkan ICD selected, the
// values must equal what vulkaninfo reports for that device.

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { setupGlobals, providerInfo } from '../../src/index.js';

const target = {};
setupGlobals(target);
const adapter = await target.navigator.gpu.requestAdapter();
if (!adapter) {
  console.log('adapter PCI identity: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const { info } = adapter;
const flavor = providerInfo?.()?.libraryFlavor ?? 'unknown';
assert(Number.isInteger(info.vendorID) && info.vendorID >= 0, `${flavor}: vendorID is an integer`);
assert(Number.isInteger(info.deviceID) && info.deviceID >= 0, `${flavor}: deviceID is an integer`);
if (info.driverVersion !== undefined) {
  assert(Number.isInteger(info.driverVersion) && info.driverVersion > 0, `${flavor}: driverVersion is a positive integer`);
}
console.log(`ok: ${flavor} reports vendorID 0x${info.vendorID.toString(16)}, deviceID 0x${info.deviceID.toString(16)}` +
  (info.driverVersion === undefined ? ', no driverVersion' : `, driverVersion 0x${info.driverVersion.toString(16)}`));

// An independent reference: vulkaninfo for the one selected ICD.
const icds = (process.env.VK_ICD_FILENAMES ?? '').split(':').filter(Boolean);
let summary = '';
if (icds.length === 1) {
  try {
    summary = execFileSync('vulkaninfo', ['--summary'], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
  } catch {
    summary = '';
  }
}
const devices = summary.split(/\n(?=GPU\d+:)/).filter((block) => /^GPU\d+:/m.test(block));
if (devices.length !== 1) {
  console.log('adapter PCI identity: vulkaninfo comparison skipped (needs exactly one Vulkan ICD and device)');
  process.exit(0);
}
const field = (name) => devices[0].match(new RegExp(`\\b${name}\\s*=\\s*(\\S+)`))?.[1];
assert.equal(info.vendorID, Number(field('vendorID')), 'vendorID equals vulkaninfo');
assert.equal(info.deviceID, Number(field('deviceID')), 'deviceID equals vulkaninfo');
if (info.driverVersion !== undefined) {
  const v = info.driverVersion;
  // vulkaninfo prints the Vulkan-packed version as major.minor.patch for these drivers.
  const decoded = `${v >>> 22}.${(v >>> 12) & 0x3ff}.${v & 0xfff}`;
  assert.equal(decoded, field('driverVersion'), 'driverVersion equals vulkaninfo');
}
console.log(`ok: ${flavor} identity equals vulkaninfo for ${field('deviceName') ?? 'the selected device'}`);
