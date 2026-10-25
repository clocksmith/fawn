#!/usr/bin/env node

// WebGPU wrapper internals stay out of enumeration: every underscore field the
// package assigns is in WRAPPER_INTERNAL_FIELDS, and the wrapper prototypes
// turn its first assignment into a non-enumerable own field.

import assert from 'node:assert/strict';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { createFullSurfaceClasses } from '../../src/vendor/webgpu/shared/full-surface.js';
import {
  WRAPPER_INTERNAL_FIELDS,
  hideInternalFields,
} from '../../src/vendor/webgpu/shared/resource-lifecycle.js';

const SOURCE_ROOT = fileURLToPath(new URL('../../src/vendor/', import.meta.url));
// `_raw` belongs to the compute-surface facades only (they hide it themselves):
// a full-surface wrapper must not answer `'_raw' in value`.
const FACADE_ONLY_FIELDS = new Set(['_raw']);
// Buffer map state is a prototype accessor over its map cell.
const PROTOTYPE_ACCESSOR_FIELDS = new Set(['_mapState']);
const ASSIGNMENT = /\.(_[A-Za-z0-9_]+)\s*(?:=(?!=)|\?\?=|\|\|=|&&=|\+=|-=)/g;

function sourceFiles(directory) {
  return readdirSync(directory).flatMap((name) => {
    const path = join(directory, name);
    if (statSync(path).isDirectory()) return sourceFiles(path);
    return name.endsWith('.js') ? [path] : [];
  });
}

{
  const listed = new Set(WRAPPER_INTERNAL_FIELDS);
  const missing = new Set();
  for (const path of sourceFiles(SOURCE_ROOT)) {
    for (const match of readFileSync(path, 'utf8').matchAll(ASSIGNMENT)) {
      const name = match[1];
      if (!listed.has(name) && !FACADE_ONLY_FIELDS.has(name)) missing.add(`${name} (${path})`);
    }
  }
  assert.deepEqual([...missing], [], 'every assigned wrapper internal is in WRAPPER_INTERNAL_FIELDS');
  for (const name of FACADE_ONLY_FIELDS) assert.ok(!listed.has(name), `${name} stays facade-only`);
  for (const name of PROTOTYPE_ACCESSOR_FIELDS) assert.ok(listed.has(name), `${name} is listed`);
}

{
  class Wrapper {
    constructor() {
      this._native = { kind: 'native' };
      this.label = '';
    }
  }
  hideInternalFields(Wrapper.prototype);
  const wrapper = new Wrapper();
  wrapper._destroyed = true;
  assert.deepEqual(Object.keys(wrapper), ['label']);
  assert.deepEqual(JSON.parse(JSON.stringify(wrapper)), { label: '' });
  assert.equal(wrapper._native.kind, 'native');
  assert.equal(wrapper._destroyed, true);
  wrapper._destroyed = false;
  assert.equal(wrapper._destroyed, false);
  assert.equal(Object.getOwnPropertyDescriptor(wrapper, '_destroyed').enumerable, false);
  assert.equal(new Wrapper()._destroyed, undefined);
}

{
  const backend = {
    deviceGetQueue() {
      return { kind: 'queue' };
    },
    deviceDestroy() {},
    queueHasPendingSubmissions() {
      return false;
    },
    queueMarkSubmittedWorkDone() {},
  };
  const { DoeGPUDevice } = createFullSurfaceClasses({ globals: {}, backend, encoderClasses: {} });
  const device = new DoeGPUDevice({ kind: 'device' }, { kind: 'instance' });
  const internals = [];
  for (const key in device) if (key.startsWith('_')) internals.push(key);
  assert.deepEqual(internals, [], 'a device enumerates no internal fields');
  for (const key in device.queue) if (key.startsWith('_')) internals.push(key);
  assert.deepEqual(internals, [], 'its queue enumerates no internal fields');
}

{
  const firstPrototype = {};
  const secondPrototype = {};
  let mapState = 'custom';
  Object.defineProperty(secondPrototype, '_mapState', {
    get() { return mapState; },
    set(value) { mapState = value; },
    configurable: true,
  });
  const originalMapState = Object.getOwnPropertyDescriptor(secondPrototype, '_mapState');
  hideInternalFields(firstPrototype);
  hideInternalFields(secondPrototype);
  assert.deepEqual(Object.getOwnPropertyDescriptor(secondPrototype, '_mapState'), originalMapState);
  const first = Object.create(firstPrototype);
  const second = Object.create(secondPrototype);
  first._native = { id: 1 };
  second._native = { id: 2 };
  assert.notEqual(first._native, second._native, 'descriptor reuse never shares receiver state');
  assert.deepEqual(Object.keys(first), []);
  assert.deepEqual(Object.keys(second), []);
  delete first._native;
  assert.equal(first._native, undefined);
  first._native = { id: 3 };
  assert.equal(second._native.id, 2);
  assert.equal(Object.getOwnPropertyDescriptor(first, '_native').enumerable, false);
  second._mapState = 'changed';
  assert.equal(mapState, 'changed', 'existing prototype accessors retain their behavior');
  const frozen = Object.freeze(Object.create(firstPrototype));
  assert.throws(() => { frozen._native = {}; }, TypeError);
  assert.equal(second._native.id, 2);
  const customPrototype = {};
  hideInternalFields(customPrototype, new Set(['customInternal']));
  const custom = Object.create(customPrototype);
  custom.customInternal = 7;
  assert.equal(custom.customInternal, 7);
  assert.deepEqual(Object.keys(custom), [], 'custom iterable names retain original behavior');
}

console.log('ok: wrapper internals are listed and non-enumerable');
