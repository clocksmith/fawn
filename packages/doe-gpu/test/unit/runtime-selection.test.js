#!/usr/bin/env node
// createDoeRuntime selects its runtime CLI without fallback: an explicit
// `runtime` option wins, otherwise a doe-c provider selects doe-c-runtime and
// any other provider the Zig CLI; a configured binary for the other
// implementation, a missing configured binary and an unknown runtime are
// refused. Every run result names the implementation that ran.

import assert from 'node:assert/strict';
import { chmodSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createDoeRuntime } from '../../src/vendor/webgpu/runtime-cli.js';

const directory = mkdtempSync(join(tmpdir(), 'doe-runtime-selection-'));
const saved = { DOE_BIN: process.env.DOE_BIN, FAWN_DOE_BIN: process.env.FAWN_DOE_BIN,
  DOE_WEBGPU_LIB: process.env.DOE_WEBGPU_LIB, DOE_LIB: process.env.DOE_LIB };
function fakeCli(name) {
  const path = join(directory, name);
  writeFileSync(path, `#!/bin/sh\necho ${name} "$@"\n`);
  chmodSync(path, 0o755);
  return path;
}
function withEnv(env, fn) {
  for (const key of Object.keys(saved)) delete process.env[key];
  Object.assign(process.env, env);
  try { return fn(); } finally {
    for (const [key, value] of Object.entries(saved)) {
      if (value === undefined) delete process.env[key]; else process.env[key] = value;
    }
  }
}
try {
  const zig = fakeCli('doe-zig-runtime');
  const c = fakeCli('doe-c-runtime');
  const cLibrary = join(directory, 'libwebgpu_doe_c.so');
  const zigLibrary = join(directory, 'libwebgpu_doe.so');
  writeFileSync(cLibrary, '');
  writeFileSync(zigLibrary, '');

  withEnv({ DOE_WEBGPU_LIB: cLibrary, DOE_BIN: c }, () => {
    const runtime = createDoeRuntime();
    assert.equal(runtime.runtimeImplementation, 'c');
    assert.equal(runtime.runtimeSelectionReason, 'provider-doe-c');
    assert.equal(runtime.libPath, null, 'doe-c-runtime reads no provider library path');
    const result = runtime.runRaw(['--help']);
    assert.equal(result.runtimeImplementation, 'c');
    assert.match(result.stdout, /^doe-c-runtime --help/);
  });
  withEnv({ DOE_WEBGPU_LIB: zigLibrary, DOE_BIN: zig }, () => {
    const runtime = createDoeRuntime();
    assert.equal(runtime.runtimeImplementation, 'zig');
    assert.equal(runtime.runtimeSelectionReason, 'provider-doe-dropin');
    assert.equal(runtime.runRaw([]).runtimeImplementation, 'zig');
  });
  withEnv({ DOE_BIN: zig }, () => {
    assert.equal(createDoeRuntime().runtimeSelectionReason, 'provider-default');
  });
  withEnv({ DOE_WEBGPU_LIB: zigLibrary }, () => {
    const runtime = createDoeRuntime({ runtime: 'c', binPath: c });
    assert.equal(runtime.runtimeImplementation, 'c');
    assert.equal(runtime.runtimeSelectionReason, 'explicit-option');
  });
  withEnv({ DOE_WEBGPU_LIB: cLibrary }, () => {
    assert.equal(createDoeRuntime({ runtime: 'zig', binPath: zig }).runtimeImplementation, 'zig');
    // A doe-c provider with a Zig binary is refused, not run against the wrong provider.
    assert.throws(() => createDoeRuntime({ binPath: zig }), /needs doe-c-runtime/);
  });
  withEnv({ DOE_BIN: zig }, () => {
    assert.throws(() => createDoeRuntime({ runtime: 'c' }), /conflicts with binary/);
  });
  withEnv({}, () => {
    assert.throws(() => createDoeRuntime({ runtime: 'c', binPath: join(directory, 'missing', 'doe-c-runtime') }),
      /configured runtime binary does not exist/);
    assert.throws(() => createDoeRuntime({ runtime: 'rust' }), /must be "zig" or "c"/);
  });
  console.log('runtime-selection.test: provider and explicit selection, refused conflicts, no fallback, named results');
} finally {
  rmSync(directory, { recursive: true, force: true });
}
