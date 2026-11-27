#!/usr/bin/env node
// Each discovered contract gets an isolated process (globals and module cache).
import { spawnSync } from 'node:child_process';
import { readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const unit = new URL('./unit/', import.meta.url);
for (const name of readdirSync(unit).filter((file) => file.endsWith('.test.js')).sort()) {
  const result = spawnSync(process.execPath, [fileURLToPath(new URL(name, unit))], { stdio: 'inherit' });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    console.error(`Failed contract ${name} (${result.signal ?? result.status})`);
    process.exitCode = 1;
  }
}
