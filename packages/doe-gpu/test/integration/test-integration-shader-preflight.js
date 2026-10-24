#!/usr/bin/env node
// preflightShaderSource checks WGSL without a device on every provider: invalid
// source reports ok:false with the failing stage and source position, never an
// unchecked success, and an accepted module reports ok:true with no diagnostic.

import assert from 'node:assert/strict';
import { preflightShaderSource, providerInfo } from '../../src/index.js';

const info = providerInfo();
if (!info.loaded) {
  console.log(`shader preflight: skipped (${info.loadError || 'no provider library'})`);
  process.exit(0);
}

const parse = preflightShaderSource('this is not wgsl');
assert.equal(parse.ok, false, 'malformed WGSL is rejected');
assert.equal(parse.stage, 'parser');
assert.ok(parse.message && parse.reasons.length === 1, 'the rejection carries a diagnostic');
assert.equal(parse.line, 1);
assert.equal(parse.column, 1);

const sema = preflightShaderSource('@compute @workgroup_size(1) fn main() {\n let x: u32 = nope;\n}');
assert.equal(sema.ok, false, 'an unresolved name is rejected');
assert.equal(sema.stage, 'sema');
assert.equal(sema.line, 2);
console.log('ok: invalid WGSL reports its stage and position');

const accepted = preflightShaderSource('@group(0) @binding(0) var<storage, read_write> d: array<u32>;\n'
  + '@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3u) { d[g.x] = g.x; }');
assert.deepEqual(accepted, { ok: true, stage: '', kind: '', message: '', reasons: [] });
console.log(`ok: ${info.libraryFlavor} accepts a valid module without a diagnostic`);
