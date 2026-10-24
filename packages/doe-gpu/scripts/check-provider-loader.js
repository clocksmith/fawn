#!/usr/bin/env node
// Source-bound CPU loader controls; never invokes a WebGPU device or shader.
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { compilerCommand } from './build-addon.js';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const ADDON = join(ROOT, 'runtime/bridge/webgpu-addon');
const [destination, ...providers] = process.argv.slice(2);
if (!destination || (process.platform !== 'win32' && providers.length !== 2) ||
    (process.platform === 'win32' && providers.length)) {
  throw new Error('Usage: check-provider-loader.js <new-output-directory> [<POSIX-C-library> <POSIX-Zig-library>]');
}
const output = resolve(destination);
mkdirSync(output);
const isWindows = process.platform === 'win32';
const fixture = (name) => join(ADDON, 'tests', name);
const source = join(ADDON, 'doe_napi_library.c');
const inputs = [source, join(ADDON, 'doe_napi_library.h'), fixture(isWindows ? 'loader_windows.c' : 'loader_posix.c')];
if (isWindows) inputs.push(fixture('loader_dependency.c'), fixture('loader_provider.c'));
else inputs.push(...providers.map((path) => resolve(path)));
const hash = (path) => createHash('sha256').update(readFileSync(path)).digest('hex');
const sourceHashes = Object.fromEntries(inputs.map((path) => [path, hash(path)]));
let compiler = process.env.CC || 'cc';
let prefix = [];
if (isWindows) {
  if (!process.env.NODE_INCLUDE_DIR) throw new Error('Windows CPU control requires the declared Node build inputs');
  const command = compilerCommand(process.env.NODE_INCLUDE_DIR);
  compiler = command.compiler;
  prefix = command.prefixArgs;
}
const flags = [...prefix, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror'];
const executable = join(output, isWindows ? 'loader-control.exe' : 'loader-control');
const commands = [];
if (isWindows) {
  const fixtures = join(output, 'fixtures');
  mkdirSync(fixtures);
  const dependency = join(fixtures, 'doe_addon_probe_dependency');
  const provider = join(fixtures, 'doe_addon_probe_provider');
  commands.push([compiler, ...flags, '-shared', fixture('loader_dependency.c'), '-o', `${dependency}.dll`,
    `-Wl,/IMPLIB:${dependency}.lib`]);
  commands.push([compiler, ...flags, '-shared', fixture('loader_provider.c'), `${dependency}.lib`,
    '-o', `${provider}.dll`, `-Wl,/IMPLIB:${provider}.lib`]);
  commands.push([compiler, ...flags, fixture('loader_windows.c'), source, '-o', executable]);
  commands.push([executable, output]);
} else {
  commands.push([compiler, ...flags, fixture('loader_posix.c'), source,
    ...(process.platform === 'linux' ? ['-ldl'] : []), '-o', executable]);
  commands.push([executable, ...providers.map((path) => resolve(path))]);
}
const results = [];
try {
  for (const [index, command] of commands.entries()) {
    const [file, ...args] = command;
    try {
      const stdout = execFileSync(file, args, { cwd: output, encoding: 'utf8' });
      writeFileSync(join(output, `${index}-stdout.txt`), stdout);
      results.push({ command, exit: 0 });
      process.stdout.write(stdout);
    } catch (error) {
      writeFileSync(join(output, `${index}-stdout.txt`), error.stdout ?? '');
      writeFileSync(join(output, `${index}-stderr.txt`), error.stderr ?? '');
      results.push({ command, exit: error.status, signal: error.signal ?? null });
      throw error;
    }
  }
} finally {
  const isUnchanged = inputs.every((path) => hash(path) === sourceHashes[path]);
  writeFileSync(join(output, 'RESULTS.json'), JSON.stringify({
    scope: `Actual ${isWindows ? 'Windows SDK' : 'POSIX'} CPU loader controls only; no device/GPU`,
    sources: sourceHashes, unchanged: isUnchanged, commands: results,
  }, null, 2));
  if (!isUnchanged) throw new Error('Loader control input changed during execution');
}
