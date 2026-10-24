#!/usr/bin/env node
// A relative DOE_WEBGPU_LIB names a file under the working directory. The
// package resolves it to an absolute path, so the library it reports is the
// one the loader opens (dlopen would search the system path for a bare name).

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const entry = fileURLToPath(new URL('../../src/index.js', import.meta.url));
const directory = mkdtempSync(join(tmpdir(), 'doe-provider-path-'));
try {
  writeFileSync(join(directory, 'libwebgpu_doe_c.so'), '');
  const reported = execFileSync(process.execPath, ['--input-type=module', '-e',
    `const { providerInfo } = await import(${JSON.stringify(entry)}); process.stdout.write(String(providerInfo().doeLibraryPath));`],
  { cwd: directory, env: { ...process.env, DOE_WEBGPU_LIB: 'libwebgpu_doe_c.so' }, encoding: 'utf8' });
  assert.equal(reported, join(directory, 'libwebgpu_doe_c.so'));
} finally {
  rmSync(directory, { recursive: true, force: true });
}
console.log('provider-path.test: ok');
