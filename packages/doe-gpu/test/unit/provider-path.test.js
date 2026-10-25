#!/usr/bin/env node
// A relative DOE_WEBGPU_LIB names a file under the working directory. The
// package resolves it to an absolute path, so the library it reports is the
// one the loader opens (dlopen would search the system path for a bare name),
// and a library that fails to load is reported as not loaded. An override that
// names a missing file selects nothing: it never falls back to another library.

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { runInNewContext } from 'node:vm';
import { fileURLToPath } from 'node:url';

const entry = fileURLToPath(new URL('../../src/index.js', import.meta.url));
const directory = mkdtempSync(join(tmpdir(), 'doe-provider-path-'));
try {
  writeFileSync(join(directory, 'libwebgpu_doe_c.so'), '');
  const reported = execFileSync(process.execPath, ['--input-type=module', '-e',
    `const { providerInfo } = await import(${JSON.stringify(entry)}); process.stdout.write(JSON.stringify(providerInfo()));`],
  { cwd: directory, env: { ...process.env, DOE_WEBGPU_LIB: 'libwebgpu_doe_c.so' }, encoding: 'utf8' });
  const info = JSON.parse(reported);
  assert.equal(info.doeLibraryPath, join(realpathSync(directory), 'libwebgpu_doe_c.so'));
  // The empty file cannot load; providerInfo reports that instead of loaded.
  assert.equal(info.loaded, false);
  assert.ok(info.loadError.length > 0, 'loadError names the load failure');
  const missing = JSON.parse(execFileSync(process.execPath, ['--input-type=module', '-e',
    `const { providerInfo } = await import(${JSON.stringify(entry)}); process.stdout.write(JSON.stringify(providerInfo()));`],
  { cwd: directory, env: { ...process.env, DOE_WEBGPU_LIB: 'missing/libwebgpu_doe_c.so' }, encoding: 'utf8' }));
  assert.equal(missing.loaded, false);
  assert.equal(missing.doeLibraryPath, '');
  assert.match(missing.loadError, /selected provider .*missing\/libwebgpu_doe_c\.so does not exist/);
  const failure = JSON.parse(execFileSync(process.execPath, ['--input-type=module', '-e',
    `const { create } = await import(${JSON.stringify(entry)});
     try { create(); throw new Error('unexpected provider creation'); }
     catch (error) { process.stdout.write(JSON.stringify({ message: error.message, reason: error.providerFailureReason })); }`],
  { cwd: directory, env: { ...process.env, DOE_WEBGPU_LIB: 'missing/libwebgpu_doe_c.so' }, encoding: 'utf8' }));
  assert.match(failure.message, /selected provider .*missing\/libwebgpu_doe_c\.so does not exist/);
  assert.equal(failure.reason, 'runtime_library_unavailable', 'explicit selection fails before addon discovery');
  for (const variable of ['DOE_WEBGPU_LIB', 'DOE_LIB']) {
    for (const selected of ['libwebgpu_doe_c.so', 'missing/libwebgpu_doe_c.so']) {
      const probe = JSON.parse(execFileSync(process.execPath, ['--input-type=module', '-e',
        `import childProcess from 'node:child_process';
         import { syncBuiltinESMExports } from 'node:module';
         let gitCalls = 0;
         const original = childProcess.spawnSync;
         childProcess.spawnSync = (...args) => { if (args[0] === 'git') gitCalls++; return original(...args); };
         syncBuiltinESMExports();
         const { create } = await import(${JSON.stringify(entry)});
         try { create(); } catch { /* Empty and missing providers intentionally fail. */ }
         process.stdout.write(JSON.stringify({ gitCalls }));`],
      { cwd: directory, env: { ...process.env, DOE_WEBGPU_LIB: '', DOE_LIB: '', [variable]: selected }, encoding: 'utf8' }));
      assert.equal(probe.gitCalls, 0, 'explicit provider selection must not discover Git workspace candidates');
    }
  }
} finally {
  rmSync(directory, { recursive: true, force: true });
}
// Execute each pure resolver body with discovery spies, including Bun's body
// without pretending Node can execute Bun FFI. Default discovery stays intact.
for (const [name, endMarker] of [['index.js', '\nfunction currentDoeLibraryPath'],
  ['bun-ffi.js', '\nconst DOE_LIB_PATH']]) {
  const source = readFileSync(new URL(`../../src/vendor/webgpu/${name}`, import.meta.url), 'utf8');
  const start = source.indexOf('function resolveDoeLibraryPath() {');
  const body = source.slice(start, source.indexOf(endMarker, start));
  for (const selected of ['present.so', 'missing.so', '']) {
    let discoveryCalls = 0;
    const context = {
      process: { env: { DOE_WEBGPU_LIB: selected }, platform: 'linux', arch: 'x64', cwd: () => '/workspace' },
      resolve, require: {}, PACKAGE_ROOT: '/package', WORKSPACE_ROOT: '/workspace',
      existsSync: (path) => path.endsWith('/present.so'),
      cLibraryBasenamesForPlatform: () => ['libwebgpu_doe_c.so'],
      resolvePlatformPackageLibraryPath: () => null,
      cWorkspaceLibraryCandidates: () => { discoveryCalls++; return []; },
      isInstalledPackageRoot: () => false,
    };
    const result = runInNewContext(`${body}\nresolveDoeLibraryPath()`, context);
    assert.equal(result, selected === 'present.so' ? resolve(selected) : null);
    assert.equal(discoveryCalls, selected ? 0 : 2, `${name}: preserve default discovery only when unselected`);
  }
}
console.log('provider-path.test: ok');
