// Native addon sources follow the C line limit (runtime/c/AGENTS.md). Files
// already over it are capped at their size when the limit was adopted, so they
// can shrink or split but not grow.
import assert from 'node:assert/strict';
import { readdir, readFile } from 'node:fs/promises';

const ADDON = new URL('../../../../runtime/bridge/webgpu-addon/', import.meta.url);
const MAX_LINES = 999;
const CAPPED = new Map([
  ['doe_napi_queue.c', 1923],
  ['doe_napi_internal.h', 961],
]);

const files = (await readdir(ADDON)).filter((name) => /\.(c|h)$/.test(name)).sort();
for (const file of files) {
  const source = await readFile(new URL(file, ADDON), 'utf8');
  const lines = source.split('\n').length - (source.endsWith('\n') ? 1 : 0);
  const limit = CAPPED.get(file) ?? MAX_LINES;
  assert(lines <= limit, `${file} has ${lines} lines; limit ${limit}${CAPPED.has(file) ? ' (capped, split it instead of growing it)' : ''}`);
}
for (const file of CAPPED.keys()) assert(files.includes(file), `${file} is gone; remove its cap`);
console.log(`ok: ${files.length} addon sources within line limits`);
