// Compiler command controls: explicit Windows SDK ABI and unchanged source inventory.
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { compilerCommand } from '../../scripts/build-addon.js';

const root = fileURLToPath(new URL('../..', import.meta.url));
const directory = mkdtempSync(join(tmpdir(), 'doe-addon-command-'));
try {
  const library = join(directory, 'node import library.lib');
  writeFileSync(library, 'command control only, never linked');
  const environment = { CC: 'C:\\LLVM SDK\\bin\\clang.exe', NODE_LIB: library, ZIG: 'must-not-run' };
  const command = compilerCommand(directory, { platform: 'win32', arch: 'x64', environment });
  assert.equal(command.compiler, environment.CC);
  assert.deepEqual(command.prefixArgs, ['--target=x86_64-pc-windows-msvc', '-fms-runtime-lib=dll', '-Wl,/NODEFAULTLIB:libcmt']);
  assert.deepEqual(command.args.slice(0, command.prefixArgs.length), command.prefixArgs);
  assert(command.args.includes(library));
  assert(command.args.includes('-shared'));
  assert(!command.args.some((arg) => arg.includes('windows-gnu') || arg === 'cc'));
  const target = JSON.parse(readFileSync(join(root, 'binding.gyp'), 'utf8'))
    .targets.find((entry) => entry.target_name === 'doe_napi');
  assert.deepEqual(command.args.filter((arg) => arg.endsWith('.c')),
    target.sources.map((source) => resolve(root, source)));
  const defaultCompiler = compilerCommand(directory, {
    platform: 'win32', arch: 'arm64', environment: { NODE_LIB: library, ZIG: 'must-not-run' },
  });
  assert.equal(defaultCompiler.compiler, 'clang');
  assert.deepEqual(defaultCompiler.prefixArgs, ['--target=aarch64-pc-windows-msvc', '-fms-runtime-lib=dll', '-Wl,/NODEFAULTLIB:libcmt']);
  assert.throws(() => compilerCommand(directory, {
    platform: 'win32', arch: 'x64', environment: { NODE_LIB: join(directory, 'missing.lib') },
  }), /Unable to locate/);
  assert.throws(() => compilerCommand(directory, {
    platform: 'win32', arch: 'unsupported', environment,
  }), /Unsupported Windows Node architecture/);
  const posix = compilerCommand(directory, { platform: 'linux', arch: 'x64', environment: { CC: 'local-cc' } });
  assert.equal(posix.compiler, 'local-cc');
  assert(posix.args.includes('-fPIC') && posix.args.includes('-shared'));
  console.log('ok: addon command uses explicit LLVM MSVC target, CC, Node import library and canonical sources; no Zig selection');
} finally {
  rmSync(directory, { recursive: true, force: true });
}

// Guard the two distinct discovery roles. Actual loading is exercised by the
// native first-kernel consumer; this source control prevents a wrong-list edit
// from replacing addon candidates with runtime shared-library candidates.
const providerSource = readFileSync(join(root, 'src/vendor/webgpu/index.js'), 'utf8');
const addonDiscovery = providerSource.slice(providerSource.indexOf('function loadAddon()'),
  providerSource.indexOf('function currentAddon()'));
assert.match(addonDiscovery, /'build', 'Release', 'doe_napi\.node'/);
assert.doesNotMatch(addonDiscovery, /cWorkspaceLibraryCandidates/);
const runtimeDiscovery = providerSource.slice(providerSource.indexOf('function resolveDoeLibraryPath()'),
  providerSource.indexOf('function currentDoeLibraryPath()'));
assert.match(runtimeDiscovery, /cWorkspaceLibraryCandidates\(WORKSPACE_ROOT\)/);
assert.doesNotMatch(runtimeDiscovery, /zig-out/);
