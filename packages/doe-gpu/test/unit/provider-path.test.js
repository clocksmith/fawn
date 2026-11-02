#!/usr/bin/env node
// A relative DOE_WEBGPU_LIB names a file under the working directory. The
// package resolves it to an absolute path, so the library it reports is the
// one the loader opens (dlopen would search the system path for a bare name),
// and a library that fails to load is reported as not loaded. An override that
// names a missing file selects nothing: it never falls back to another library.

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { resolveDoeLibrarySelection } from '../../src/vendor/webgpu/platform-package.js';
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
// Both execution bindings consume this resolver; exercise precedence directly.
for (const environment of [
  { DOE_WEBGPU_LIB: 'present.so', DOE_LIB: 'ignored.so' },
  { DOE_WEBGPU_LIB: 'missing.so', DOE_LIB: 'present.so' },
  { DOE_LIB: 'present.so' },
  {},
]) {
  let discoveryCalls = 0;
  const result = resolveDoeLibrarySelection({
    environment, cwd: '/working', packageRoot: '/package', workspaceRoot: '/workspace',
    existsFn: path => path.endsWith('/present.so'),
    packageLibrary: () => { discoveryCalls++; return null; },
    workspaceLibraries: () => { discoveryCalls++; return []; },
  });
  const configured = environment.DOE_WEBGPU_LIB || environment.DOE_LIB;
  assert.equal(result.libraryPath, configured === 'present.so' ? '/working/present.so' : null);
  assert.equal(result.missingOverride, configured === 'missing.so' ? '/working/missing.so' : null);
  assert.equal(discoveryCalls, configured ? 0 : 3);
}
for (const [packageRoot, expected] of [
  ['/source/package', '/workspace/provider'],
  ['/installed/node_modules/doe-gpu', '/packaged/provider'],
]) {
  const options = {
    environment: {}, packageRoot, workspaceRoot: '/workspace', cwd: '/working',
    packageLibrary: () => '/packaged/provider',
    workspaceLibraries: root => [root + '/provider'],
  };
  assert.equal(resolveDoeLibrarySelection({ ...options, existsFn: () => true }).libraryPath, expected);
  assert.equal(resolveDoeLibrarySelection({ ...options, existsFn: path => path === '/working/provider' }).libraryPath,
    '/working/provider');
  assert.equal(resolveDoeLibrarySelection({ ...options, existsFn: () => false }).libraryPath, null);
}
for (const [platform, arch, name] of [
  ['linux', 'x64', 'libwebgpu_doe_c.so'],
  ['darwin', 'arm64', 'libwebgpu_doe_c.dylib'],
  ['win32', 'x64', 'webgpu_doe_c.dll'],
]) {
  const expected = `/package/prebuilds/${platform}-${arch}/${name}`;
  const result = resolveDoeLibrarySelection({
    environment: {}, platform, arch, packageRoot: '/package',
    packageLibrary: () => null, workspaceLibraries: () => [],
    existsFn: path => path === expected,
  });
  assert.equal(result.libraryPath, expected);
}
console.log('provider-path.test: ok');
