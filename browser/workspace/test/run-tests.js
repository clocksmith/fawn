#!/usr/bin/env node
// Fawn workspace test runner: unit tests by default; `--browser` adds real-Chromium workflows.
//
// Each file runs in its own process with a time limit so a stuck browser cannot hang the run.

import { spawnSync } from 'node:child_process';
import { readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const TEST_ROOT = fileURLToPath(new URL('.', import.meta.url));
const FILE_TIME_LIMIT_MS = 180000;
const SUITES = Object.freeze({ unit: 'unit', browser: 'browser' });

const suites = process.argv.includes('--browser') ? [SUITES.unit, SUITES.browser] : [SUITES.unit];
let failures = 0;
for (const suite of suites) {
  const directory = `${TEST_ROOT}${suite}`;
  for (const file of readdirSync(directory).filter((name) => name.endsWith('.test.js')).sort()) {
    const result = spawnSync(process.execPath, [`${directory}/${file}`], {
      stdio: 'inherit',
      timeout: FILE_TIME_LIMIT_MS,
      killSignal: 'SIGKILL',
    });
    if (result.status !== 0) {
      failures++;
      console.error(`FAIL: ${suite}/${file} (${result.signal ?? `exit ${result.status}`})`);
    }
  }
}
process.exit(failures === 0 ? 0 : 1);
