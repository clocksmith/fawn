import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdirSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { resolve } from 'node:path';
import { cStageFiles, cLibraryName } from '../../scripts/c-native-stage.js';
const root = mkdtempSync(resolve(tmpdir(), 'doe-c-staging-'));
const hash = (text) => createHash('sha256').update(text).digest('hex');
function write(name, bytes) {
  const path = resolve(root, name);
  mkdirSync(resolve(path, '..'), { recursive: true });
  writeFileSync(path, bytes);
  return path;
}
try {
  assert.equal(spawnSync('git', ['init', '-q', root]).status, 0);
  write('runtime/c/owner.c', 'source');
  assert.equal(spawnSync('git', ['add', 'runtime/c'], { cwd: root }).status, 0);
  const library = write('stage/lib/libwebgpu_doe_c.so', 'native');
  write('stage/lib/libdoe_compiler.so', 'compiler');
  const manifest = { source_commit: 'committed', source_inputs: { 'runtime/c/owner.c': hash('source') },
    artifacts: { 'lib/libwebgpu_doe_c.so': hash('native'), 'lib/libdoe_compiler.so': hash('compiler') } };
  write('stage/MANIFEST.json', JSON.stringify(manifest));
  assert.equal(cStageFiles(library, root).compilerPath, resolve(root, 'stage/lib/libdoe_compiler.so'));
  write('stage/lib/libdoe_compiler.so', 'stale');
  assert.throws(() => cStageFiles(library, root), /artifact digest mismatch/);
  write('stage/lib/libdoe_compiler.so', 'compiler');
  write('runtime/c/owner.c', 'changed source');
  assert.throws(() => cStageFiles(library, root), /predates or differs/);
  write('runtime/c/owner.c', 'source');
  const windowsLibrary = write('windows/bin/webgpu_doe_c.dll', 'windows-native');
  const windowsFiles = {
    'bin/webgpu_doe_c.dll': 'windows-native', 'bin/doe_compiler.dll': 'windows-compiler',
    'bin/dxcompiler.dll': 'dxc', 'bin/dxil.dll': 'dxil',
    'licenses/DXC/LICENSE.txt': 'license', 'NATIVE_BUILD.json': '{}',
  };
  const windowsManifest = { ...manifest, artifacts: {} };
  for (const [name, bytes] of Object.entries(windowsFiles)) {
    write(`windows/${name}`, bytes); windowsManifest.artifacts[name] = hash(bytes);
  }
  write('windows/MANIFEST.json', JSON.stringify(windowsManifest));
  assert.equal(cStageFiles(windowsLibrary, root).files.length, 6);
  write('windows/bin/dxil.dll', 'wrong');
  assert.throws(() => cStageFiles(windowsLibrary, root), /artifact digest mismatch/);
  write('windows/bin/dxil.dll', 'dxil');
  delete windowsManifest.artifacts['licenses/DXC/LICENSE.txt'];
  write('windows/MANIFEST.json', JSON.stringify(windowsManifest));
  assert.throws(() => cStageFiles(windowsLibrary, root), /license\/build provenance/);
  write('runtime/c/new.c', 'new');
  assert.equal(spawnSync('git', ['add', 'runtime/c/new.c'], { cwd: root }).status, 0);
  assert.throws(() => cStageFiles(library, root), /omits current source/);
  assert.equal(cLibraryName('win32'), 'webgpu_doe_c.dll');
  assert.throws(() => cLibraryName('unknown'), /Unsupported/);
} finally { rmSync(root, { recursive: true, force: true }); }
console.log('C package source inventory, changed source, compiler/runtime hashes and explicit platform controls passed');
