#!/usr/bin/env node
// createDoeRuntime selects its runtime CLI without fallback: an explicit
// `runtime` option wins, otherwise a doe-c provider selects doe-c-runtime and
// an explicit legacy provider the Zig CLI; absent provider defaults C. A binary for the other
// implementation, a missing configured binary and an unknown runtime are
// refused. Every run result names the implementation that ran. Module runs
// select the same implementation without fallback, and the Doe
// namespace's runtime helpers use the factory the package surface passes.

import assert from 'node:assert/strict';
import { chmodSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createDoeRuntime, resolveDoeBinaryPath, resolveDoeModuleRunnerPath } from '../../src/vendor/webgpu/runtime-cli.js';
import { createDoeNamespace } from '../../src/vendor/doe-namespace.js';

const directory = mkdtempSync(join(tmpdir(), 'doe-runtime-selection-'));
const saved = { DOE_BIN: process.env.DOE_BIN, FAWN_DOE_BIN: process.env.FAWN_DOE_BIN,
  DOE_WEBGPU_LIB: process.env.DOE_WEBGPU_LIB, DOE_LIB: process.env.DOE_LIB, DOE_MODULE_RUNNER: process.env.DOE_MODULE_RUNNER };
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
  withEnv({ DOE_BIN: c }, () => {
    assert.equal(createDoeRuntime().runtimeSelectionReason, 'provider-default');
    assert.equal(createDoeRuntime().runtimeImplementation, 'c');
  });
  withEnv({ DOE_BIN: zig }, () => {
    assert.throws(() => createDoeRuntime(), /needs doe-c-runtime/);
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
    const moduleRunner = fakeCli('doe-c-module-runner');
    writeFileSync(moduleRunner, '#!/bin/sh\nprintf \'{"model":"selection-test"}\\n\'\n');
    const runtime = createDoeRuntime({ runtime: 'c', binPath: c, moduleRunnerPath: moduleRunner });
    const request = { moduleId: 'm', request: {}, policyPath: cLibrary };
    const result = runtime.runModule(request);
    assert.equal(result.runtimeImplementation, 'c');
    assert.deepEqual(result.output, { model: 'selection-test' });
    const zigModule = fakeCli('module-core-runner');
    assert.throws(() => runtime.runModule({ ...request, moduleRunnerPath: zigModule }), /needs doe-c-module-runner/);
    assert.throws(() => createDoeRuntime({ runtime: 'zig', binPath: zig, moduleRunnerPath: moduleRunner }), /needs module-core-runner/);
    assert.throws(() => createDoeRuntime({ runtime: 'c', binPath: c, moduleRunnerPath: join(directory, 'missing-module') }),
      /configured module runner does not exist/);
  });
  withEnv({}, () => {
    assert.throws(() => createDoeRuntime({ runtime: 'c', binPath: join(directory, 'missing', 'doe-c-runtime') }),
      /configured runtime binary does not exist/);
    assert.throws(() => createDoeRuntime({ runtime: 'rust' }), /must be "zig" or "c"/);
  });
  withEnv({}, () => {
    // Discovery control only: these files are never executed as Windows code.
    const platform = Object.getOwnPropertyDescriptor(process, 'platform');
    const cwd = process.cwd();
    const runtimeDir = join(directory, 'runtime/c/runtime-cli/bin');
    const modulesDir = join(directory, 'runtime/c/modules/bin');
    mkdirSync(runtimeDir, { recursive: true });
    mkdirSync(modulesDir, { recursive: true });
    writeFileSync(join(runtimeDir, 'doe-c-runtime.exe'), 'Windows executable path control');
    writeFileSync(join(runtimeDir, 'doe-c-runtime'), 'other-host path must not win');
    writeFileSync(join(modulesDir, 'doe-c-module-runner.exe'), 'Windows module path control');
    try {
      Object.defineProperty(process, 'platform', { ...platform, value: 'win32' });
      process.chdir(directory);
      assert.equal(resolveDoeBinaryPath(undefined, 'c'), join(runtimeDir, 'doe-c-runtime.exe'));
      assert.equal(resolveDoeModuleRunnerPath(undefined, { implementation: 'c' }), join(modulesDir, 'doe-c-module-runner.exe'));
      assert.throws(() => resolveDoeBinaryPath(join(directory, 'missing.exe'), 'c'), /configured runtime binary does not exist/);
    } finally {
      Object.defineProperty(process, 'platform', platform);
      process.chdir(cwd);
    }
  });
  const factoryCalls = [];
  const gpu = createDoeNamespace({
    requestDevice: async () => ({}),
    createDoeRuntime(options) {
      factoryCalls.push(options);
      return { runOrdinaryExecution: (run) => ({ ranBy: 'surface factory', commandsPath: run.commandsPath }) };
    },
  });
  const viaNamespace = await gpu.ordinaryExecution({ commandsPath: 'stream.json', runtimeOptions: { runtime: 'c' } });
  const viaBound = await gpu.bind({}).numericStability.ordinaryExecution({ commandsPath: 'bound.json' });
  assert.deepEqual([viaNamespace.ranBy, viaBound.ranBy, viaBound.commandsPath], ['surface factory', 'surface factory', 'bound.json']);
  assert.deepEqual(factoryCalls, [{ runtime: 'c' }, {}]);
  console.log('runtime-selection.test: provider and explicit selection, refused conflicts, no fallback, named results, '
    + 'module runner selection without fallback, namespace helpers use the surface factory');
} finally {
  rmSync(directory, { recursive: true, force: true });
}
