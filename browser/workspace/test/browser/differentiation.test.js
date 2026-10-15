// Real Chromium: one clean agent-workflow run per arm completes, judged by the workbench's records.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const RUNNER = fileURLToPath(new URL('../../qualification/differentiate.js', import.meta.url));
const RUN_TIME_LIMIT_MS = 120000;

const outDir = join(mkdtempSync(join(tmpdir(), 'fawn-differentiation-test-')), 'result');
const ran = spawnSync(process.execPath, [RUNNER, '--out', outDir, '--scenarios', 'agent-clean', '--repetitions', '1'], {
  encoding: 'utf8',
  timeout: RUN_TIME_LIMIT_MS,
  killSignal: 'SIGKILL',
});
const result = JSON.parse(readFileSync(join(outDir, 'differentiation.json'), 'utf8'));
assert.equal(result.scope, 'subset');
assert.equal(result.productAdvantage.established, false, 'a subset never establishes product advantage');
assert.equal(result.sameBrowserBinary, true);
for (const row of result.rows) assert.equal(row.taskSuccess, true, `${row.arm}: ${JSON.stringify(row.details)}\n${ran.stdout}`);
assert.deepEqual(result.rows.map((row) => row.arm).sort(), ['control', 'treatment']);
console.log('ok: differentiation clean run on both arms');
