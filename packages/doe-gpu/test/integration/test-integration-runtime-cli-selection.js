#!/usr/bin/env node
// createDoeRuntime().runBench runs the runtime CLI of the selected provider:
// doe-c-runtime for a doe-c provider, doe-zig-runtime otherwise. The result
// names the implementation, and the trace meta agrees (module and
// executionBackend), so a run is never attributed to the other runtime.

import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createDoeRuntime, providerInfo } from '../../src/index.js';

const repo = resolve(fileURLToPath(new URL('../../../..', import.meta.url)));
const flavor = providerInfo().libraryFlavor;
const expected = flavor === 'doe-c'
  ? { implementation: 'c', module: 'doe-c-runtime', backend: 'doe_c_vulkan' }
  : { implementation: 'zig', module: 'doe-zig-runtime', backend: 'doe_vulkan' };
let runtime;
try {
  runtime = createDoeRuntime();
} catch (error) {
  if (/Could not locate doe-(c|zig)-runtime/.test(String(error.message))) {
    console.log(`runtime CLI selection: skipped (${error.message.split('.')[0]})`);
    process.exit(0);
  }
  throw error;
}
assert.equal(runtime.runtimeImplementation, expected.implementation, `${flavor} selects the ${expected.module} CLI`);
assert.equal(runtime.runtimeSelectionReason, `provider-${flavor}`);
const work = mkdtempSync(join(tmpdir(), 'doe-runtime-cli-selection-'));
try {
  const args = { cwd: repo, commandsPath: 'examples/rmsnorm_tree_commands.json', kernelRoot: 'bench/kernels',
    traceJsonlPath: join(work, 'trace.jsonl'), traceMetaPath: join(work, 'trace.meta.json') };
  // doe-c-runtime applies no quirks; the Zig CLI keeps its defaults.
  const result = runtime.runBench(expected.implementation === 'c' ? { ...args, extraArgs: ['--quirk-mode', 'off'] } : args);
  assert.equal(result.ok, true, result.stderr);
  assert.equal(result.runtimeImplementation, expected.implementation);
  assert.equal(result.traceMeta.module, expected.module);
  assert.equal(result.traceMeta.executionBackend, expected.backend);
  console.log(`ok: ${flavor} ran ${expected.module} (${result.traceMeta.executionBackend}), final hash ${result.traceMeta.hash}`);
} finally {
  rmSync(work, { recursive: true, force: true });
}
