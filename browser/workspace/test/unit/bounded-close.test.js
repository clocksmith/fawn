// Releasing a workspace is bounded: a browser that will not exit is ended, and only the main
// process that owns exactly this profile is chosen.
import assert from 'node:assert/strict';
import { closeWithin, profileBrowserPids } from '../../src/adapters/bounded-close.js';

const listing = [
  '  101 /Applications/Google Chrome.app/Contents/MacOS/Google Chrome --headless --user-data-dir=/state/profiles/a --remote-debugging-pipe',
  '  102 /Applications/Google Chrome.app/Contents/Frameworks/Helper (Renderer) --type=renderer --user-data-dir=/state/profiles/a',
  '  103 /Applications/Google Chrome.app/Contents/MacOS/Google Chrome --user-data-dir=/state/profiles/a-other',
  '  104 /Applications/Google Chrome.app/Contents/MacOS/Google Chrome --user-data-dir=/state/profiles/b',
  '  105 /usr/bin/chrome --user-data-dir=/state/profiles/a',
].join('\n');
if (process.platform !== 'win32') {
  assert.deepEqual(profileBrowserPids('/state/profiles/a', { list: () => listing }), [101, 105]);
  assert.deepEqual(profileBrowserPids('/state/profiles/none', { list: () => listing }), []);
}

const killed = [];
const prompt = { close: async () => {} };
assert.equal(await closeWithin(prompt, { profileDir: '/p', timeoutMs: 1000, findPids: () => [1], kill: (pid) => killed.push(pid) }), 'graceful');
assert.deepEqual(killed, [], 'a browser that exits in time is never signalled');

// A browser that never answers the graceful close exits only once its process is ended.
let release;
const stuck = { close: () => new Promise((resolve) => { release = resolve; }) };
const started = performance.now();
const outcome = await closeWithin(stuck, { profileDir: '/state/profiles/a', timeoutMs: 40,
  findPids: (dir) => { assert.equal(dir, '/state/profiles/a'); return [101, 105]; },
  kill: (pid, signal) => { killed.push([pid, signal]); if (killed.length === 2) release(); } });
assert.equal(outcome, 'forced');
assert.deepEqual(killed, [[101, 'SIGKILL'], [105, 'SIGKILL']]);
assert.ok(performance.now() - started < 2000, 'the wait is bounded');

// A process that vanished before the signal, and a close that rejects after the forced exit, are not errors.
let fail;
const gone = { close: () => new Promise((_, reject) => { fail = reject; }) };
assert.equal(await closeWithin(gone, { profileDir: '/p', timeoutMs: 20, findPids: () => [7],
  kill: () => { fail(Error('Target closed')); throw Object.assign(Error('no such process'), { code: 'ESRCH' }); } }), 'forced');
// A close that never settles even after the browser is ended is abandoned after the grace period.
const silent = { close: () => new Promise(() => {}) };
const began = performance.now();
assert.equal(await closeWithin(silent, { profileDir: '/p', timeoutMs: 20, settleMs: 30, findPids: () => [9], kill: () => {} }), 'forced');
assert.ok(performance.now() - began < 2000, 'an unsettled close cannot hold the release open');
console.log('ok: bounded workspace release, exact-profile main process selection, forced exit after the deadline');
