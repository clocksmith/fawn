#!/usr/bin/env node
// A device the application no longer references can be garbage collected,
// destroyed or not: the addon's callback bindings hold it weakly, and a
// collected undestroyed device releases its native device. Uses V8's gc()
// (Bun.gc under Bun).

import assert from 'node:assert/strict';
import { setFlagsFromString } from 'node:v8';
import { runInNewContext } from 'node:vm';
import { requestNativeDeviceOrSkip } from './native-device-test-helper.js';

// Bun exposes its collector directly; V8 needs its flag first.
const gc = typeof Bun !== 'undefined'
  ? () => Bun.gc(true)
  : (setFlagsFromString('--expose_gc'), runInNewContext('gc'));
const collected = new Set();
const registry = new FinalizationRegistry((name) => collected.add(name));

async function useAndDrop(name, destroy) {
  const device = await requestNativeDeviceOrSkip('device collect');
  device.addEventListener('uncapturederror', () => {});
  device.onuncapturederror = () => {};
  void device.lost;
  device.pushErrorScope('validation');
  device.createSampler({ lodMinClamp: 5, lodMaxClamp: 1 });
  await device.popErrorScope();
  const encoder = device.createCommandEncoder();
  device.queue.submit([encoder.finish()]);
  await device.queue.onSubmittedWorkDone();
  registry.register(device, name);
  if (destroy) device.destroy();
}

await useAndDrop('destroyed', true);
await useAndDrop('dropped', false);
for (let attempt = 0; attempt < 40 && collected.size < 2; attempt += 1) {
  await new Promise((resolve) => setTimeout(resolve, 25));
  gc();
}
assert(collected.has('destroyed'), 'a destroyed device is collected');
assert(collected.has('dropped'), 'an undestroyed device nothing references is collected');
console.log('ok: destroyed and dropped devices are garbage collected');
