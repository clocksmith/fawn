// Exercise npm through the real CLI, including literal shell metacharacters in paths.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { npmInvocation } from '../lib/npm-invocation.js';

const invocation = npmInvocation();
const version = spawnSync(invocation.executable, [...invocation.prefixArgs, '--version'], { encoding: 'utf8' });
assert.equal(version.status, 0, version.stderr);
assert.match(version.stdout.trim(), /^\d+\.\d+\.\d+/u);
const scratch = mkdtempSync(join(tmpdir(), 'doe npm & literal '));
try {
  const packageRoot = join(scratch, 'package & literal');
  const destination = join(scratch, 'packed & literal');
  mkdirSync(packageRoot);
  mkdirSync(destination);
  writeFileSync(join(packageRoot, 'package.json'), JSON.stringify({ name: 'doe-npm-cli-control', version: '1.0.0' }));
  const packed = spawnSync(invocation.executable, [...invocation.prefixArgs, 'pack', '--ignore-scripts',
    '--pack-destination', destination, '--json'], { cwd: packageRoot, encoding: 'utf8' });
  assert.equal(packed.status, 0, packed.stderr);
  const manifest = JSON.parse(packed.stdout)[0];
  assert(readFileSync(join(destination, manifest.filename)).length > 0);
  assert.throws(() => npmInvocation({ executable: join(scratch, 'absent/node.exe'), environment: {} }), /Cannot locate/);
} finally {
  rmSync(scratch, { recursive: true, force: true });
}
console.log('ok: actual npm CLI packs literal paths without a command shell');
