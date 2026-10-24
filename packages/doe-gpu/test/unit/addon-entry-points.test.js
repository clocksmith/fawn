// The Node backend never skips an addon entry point silently
// (runtime/bridge/webgpu-addon/AGENTS.md). Every `typeof addon.X` guard is
// either an optional fast path listed here, whose code falls back to standard
// entry points, or a required entry point that throws when absent and that the
// addon exports.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const backend = await readFile(new URL('../../src/vendor/webgpu/index.js', import.meta.url), 'utf8');
const init = await readFile(new URL('../../../../runtime/bridge/webgpu-addon/doe_napi_init.c', import.meta.url), 'utf8');

const OPTIONAL_FAST_PATHS = new Set([
  'bufferFlushStagedRange',
  'bufferGetMapState',
  'bufferMapReadCopyUnmap',
  'bufferReadCopy',
  'computePassDispatchBound',
  'createComputeDispatchBatchCopyCommandBuffer',
  'createBufferBindGroupFlat4',
  'createComputeDispatchCopyCommandBuffer',
  'createPipelineLayoutOne',
  'queueSubmitOne',
  'queueWriteBufferBatch',
  'queueWriteBufferBatchDataPtrs',
]);

const guards = [...backend.matchAll(/typeof addon\.(\w+) (===|!==) 'function'/g)];
assert(guards.length > 0, 'the backend guards some addon entry points');
const optional = new Set();
const required = [];
for (const match of guards) {
  const throwsWhenAbsent = match[2] === '!=='
    && /^typeof addon\.\w+ !== 'function'\)\s*\{\s*throw /.test(backend.slice(match.index, match.index + 240));
  if (throwsWhenAbsent) required.push(match[1]);
  else optional.add(match[1]);
}
const unlisted = [...optional].filter((name) => !OPTIONAL_FAST_PATHS.has(name));
assert.deepEqual(unlisted, [], `addon entry points guarded as optional without a reviewed fallback: ${unlisted.join(', ')}`);
const exported = new Set([...init.matchAll(/EXPORT_FN\("(\w+)"/g)].map((match) => match[1]));
const missing = required.filter((name) => !exported.has(name));
assert.deepEqual(missing, [], `entry points the backend requires but the addon does not export: ${missing.join(', ')}`);
console.log(`ok: ${required.length} required addon entry points throw when absent and are exported; ${optional.size} optional fast paths are listed`);
