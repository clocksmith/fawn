// The Node backend never skips an addon entry point silently
// (runtime/bridge/webgpu-addon/AGENTS.md): a guarded entry point throws when
// absent, and the addon exports every entry point guarded that way.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const backend = await readFile(new URL('../../src/vendor/webgpu/index.js', import.meta.url), 'utf8');
const init = await readFile(new URL('../../../../runtime/bridge/webgpu-addon/doe_napi_init.c', import.meta.url), 'utf8');

const silent = [...backend.matchAll(
  /\n(\s*)if \(typeof addon\.(\w+) === 'function'\) \{\n\s*(?:return )?addon\.\2\([^;]*\);\n\1\}\n\s*\},/g,
)].map((match) => match[2]);
assert.deepEqual(silent, [], `backend methods that skip a missing addon entry point: ${silent.join(', ')}`);

const exported = new Set([...init.matchAll(/EXPORT_FN\("(\w+)"/g)].map((match) => match[1]));
const required = [...backend.matchAll(
  /typeof addon\.(\w+) !== 'function'\) \{\n\s*throw unsupportedNodeDeviceCapability/g,
)].map((match) => match[1]);
assert(required.length > 0, 'the backend guards some addon entry points');
const missing = required.filter((name) => !exported.has(name));
assert.deepEqual(missing, [], `entry points the backend requires but the addon does not export: ${missing.join(', ')}`);
console.log(`ok: ${required.length} required addon entry points are exported and none is skipped silently`);
