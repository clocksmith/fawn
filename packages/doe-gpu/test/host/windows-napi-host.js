// Exercise Windows host resolution without loading a GPU provider.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

function option(name) {
  const index = process.argv.indexOf(name);
  return index < 0 ? undefined : process.argv[index + 1];
}

if (process.platform !== 'win32') {
  assert(!process.argv.includes('--required'), 'actual Windows is required');
  console.log('SKIP Windows N-API host control: non-Windows host');
  process.exit(0);
}
assert(!process.versions.bun, 'invoke this control with Node');
const packageRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const addon = resolve(option('--addon') ?? process.env.DOE_NAPI_ADDON
  ?? resolve(packageRoot, 'build/Release/doe_napi.node'));
const bun = option('--bun') ?? process.env.BUN_BIN;
assert(existsSync(addon), `missing actual addon: ${addon}`);
assert(bun && existsSync(bun), 'provide the actual Bun executable with --bun');
const script = `const a=require(${JSON.stringify(addon)});`
  + `if(typeof a.loadLibrary!=='function')throw Error('missing N-API export');`
  + `console.log(JSON.stringify({host:process.versions.bun?'bun':'node',loaded:true}));`;
const checks = [];
for (const [host, executable] of [['node', process.execPath], ['bun', bun]]) {
  const result = spawnSync(executable, ['-e', script], {
    encoding: 'utf8', timeout: 30_000, windowsHide: true,
  });
  assert.equal(result.status, 0,
    `${host} failed native host initialization: ${result.error ?? result.stderr}`);
  assert.deepEqual(JSON.parse(result.stdout), { host, loaded: true });
  checks.push({ host, executable, exitCode: result.status,
    executableSha256: createHash('sha256').update(readFileSync(executable)).digest('hex') });
}
console.log(JSON.stringify({ scope: 'Windows N-API host initialization only; no GPU calls',
  addon, addonSha256: createHash('sha256').update(readFileSync(addon)).digest('hex'),
  checks }, null, 2));
