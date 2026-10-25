#!/usr/bin/env node
// Actual host C module runner through both public package methods; no GPU or Zig.
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createDoeRuntime } from '../../src/vendor/webgpu/runtime-cli.js';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');
const work = mkdtempSync(join(tmpdir(), 'doe-c-module-consumer-'));
try {
  const runtime = createDoeRuntime({ runtime: 'c', binPath: process.env.DOE_BIN,
    moduleRunnerPath: process.env.DOE_MODULE_RUNNER });
  const fixtures = [
    ['fawn_compute_services', 'fawn-compute-services', 'compute-services'],
    ['fawn_effects_pipeline', 'fawn-effects-pipeline', 'effects-pipeline'],
    ['fawn_path_engine', 'fawn-path-engine', 'path-engine'],
    ['fawn_resource_scheduler', 'fawn-resource-scheduler', 'resource-scheduler'],
    ['fawn_2d_sdf_renderer', 'fawn-2d-sdf-renderer', 'sdf-renderer'],
  ];
  for (const [moduleId, stem, policy] of fixtures) {
    const request = JSON.parse(readFileSync(join(root,
      `browser/chromium/module-incubation/fixtures/${stem}.request.json`), 'utf8'));
    const result = runtime.runModule({ moduleId, request, cwd: root,
      policyPath: `config/${policy}.policy.json` });
    assert.equal(result.ok, true, result.stderr);
    assert.equal(result.runtimeImplementation, 'c');
    assert.equal(result.output.moduleId, moduleId);
    assert.equal(result.output.traceLink.moduleIdentity, moduleId);
  }
  const result = runtime.runNumericStabilityMatmulLogitsSlice({
    cwd: root, hiddenState: [1, 1, 1],
    candidates: [{ tokenId: 817, label: 'go', weights: [10000, 0.01, -10000] },
      { tokenId: 4721, label: 'stop', weights: [0, 0.001, 0] }],
    receiptPath: join(work, 'receipt.jsonl'), traceMetaPath: join(work, 'meta.json'),
  });
  assert.equal(result.ok, true, result.stderr);
  assert.equal(result.runtimeImplementation, 'c');
  assert.equal(result.output.routeDecision, 'prefer-stable');
  assert.equal(result.output.selectedToken, 817);
  assert.equal(result.output.receipt.executionIdentity, null, 'host calculation has no GPU identity');
  assert.equal(result.output.executionStats.dispatchCount, 0);
  assert.equal(JSON.parse(readFileSync(join(work, 'receipt.jsonl'), 'utf8')).route.effectApplied, false);
  assert.equal(JSON.parse(readFileSync(join(work, 'meta.json'), 'utf8')).numericStability.receipt_count, 1);
  console.log('PASS public C runModule: five host models and actual numeric matmul service');
} finally {
  rmSync(work, { recursive: true, force: true });
}
