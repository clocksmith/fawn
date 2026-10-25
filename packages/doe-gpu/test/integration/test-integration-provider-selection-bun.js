#!/usr/bin/env bun
// Bun selects its FFI surface only for a provider that exports the Doe flat
// entry points (the Zig drop-in): webgpu.h passes async callback structs by
// value, which Bun FFI cannot call. Any other provider (the C runtime) runs
// through the N-API surface. An explicit DOE_BUN_WEBGPU_BACKEND=ffi is honored
// or refused with the reason, never silently replaced by N-API.

import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { providerInfo } from '../../src/bun.js';

const info = providerInfo();
if (!info.loaded) {
  console.log('provider selection (bun): skipped (no provider library)');
  process.exit(0);
}
const flat = info.libraryFlavor === 'doe-dropin';
assert.equal(info.bunRuntimeProvider, flat ? 'doe-ffi' : 'doe-full', `${info.libraryFlavor} default surface`);

const probe = "import('./src/bun.js').then(m => console.log('selected=' + m.providerInfo().bunRuntimeProvider), e => console.log('refused=' + e.message))";
const child = spawnSync(process.execPath, ['-e', probe], {
  cwd: new URL('../..', import.meta.url).pathname,
  env: { ...process.env, DOE_BUN_WEBGPU_BACKEND: 'ffi' },
  encoding: 'utf8',
});
const output = child.stdout.trim();
if (flat) assert.equal(output, 'selected=doe-ffi', output || child.stderr);
else assert.match(output, /^refused=.*DOE_BUN_WEBGPU_BACKEND=ffi needs a provider exporting the Doe flat entry points/, output || child.stderr);
console.log(`ok: Bun runs the ${info.libraryFlavor} provider through ${info.bunRuntimeProvider}; an explicit FFI request is ${flat ? 'honored' : 'refused'}`);
