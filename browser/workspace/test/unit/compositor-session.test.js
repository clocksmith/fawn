import assert from 'node:assert/strict';
import { CompositorSessions } from '../../src/adapters/compositor-capture.js';
import { buildSnapshot } from '../../src/adapters/page-query.js';

// A page can answer a delayed CDP request after the service's read deadline.
// Neither a late world nor a late initializer can open a new chooser.
for (const delayed of ['Page.createIsolatedWorld', 'Runtime.evaluate']) {
  const sessions = new CompositorSessions(), abort = new AbortController();
  let resume, reached;
  const blocked = new Promise(resolve => { reached = resolve; });
  const calls = [];
  const send = async (method, args) => {
    calls.push(method);
    if (method === 'Page.getFrameTree') return { frameTree: { frame: { loaderId: 'doc', id: 'frame' } } };
    if (method === delayed && (method !== 'Runtime.evaluate' || args.expression.includes('globalThis.__blitzenSurface ='))) {
      reached(); await new Promise(done => { resume = done; });
    }
    if (method === 'Page.createIsolatedWorld') return { executionContextId: 1 };
    return { result: { value: true } };
  };
  const request = sessions.snapshot('work', 'main', send, {}, { session: 'start', selector: '#canvas' }, abort.signal);
  await blocked; abort.abort(); resume();
  await assert.rejects(request);
  assert.equal(sessions.entries.size, 0);
  assert.ok(!calls.includes('Runtime.callFunctionOn'));
}
console.log('ok: late isolated-world creation and renderer initialization cannot reopen a timed-out capture');

// The private controller numbers successful samples. The ordinary renderer
// follows the same CDP sequence and receives no scope-probe bracket.
async function capturePair(privateWorldPerCapture) {
  const sessions = new CompositorSessions({ privateWorldPerCapture });
  const calls = [], names = [], frames = [];
  let revision = 3, scale = 1, x = 0;
  const send = async (method, args) => {
    calls.push(method);
    if (method === 'Page.getFrameTree') return { frameTree: { frame: { loaderId: 'doc', id: 'frame' } } };
    if (method === 'Page.createIsolatedWorld') {
      names.push(args.worldName);
      return { executionContextId: names.length };
    }
    if (method === 'Runtime.callFunctionOn') {
      const id = args.arguments[2].value;
      return { result: { value: { regions: [{ selector: '#canvas', frame: { sessionId: id, surfaceId: id,
        mediaTimestampUs: calls.length, binding: { deviceScaleFactor: scale, viewportRect: { x, y: 0, width: 16, height: 8 } } } }] } } };
    }
    return { result: { value: true } };
  };
  async function read(session) {
    const answer = await sessions.snapshot('work', 'main', send, {},
      { session, selector: '#canvas', transport: 'external-texture', grantId: 'read', grantRevision: revision });
    frames.push(answer.regions[0].frame);
    return frames.at(-1).sessionId;
  }
  try {
    const id = await read('start');
    await read(id);
    scale = 2; await read(id);
    x = 0.5; await read(id);
    revision++;
    await assert.rejects(read(id), /expired or changed/);
    await read('start');
    return { calls, names, frames };
  } finally {
    await sessions.release();
  }
}
const ordinary = await capturePair(false), traced = await capturePair(true);
assert.deepEqual(traced.calls, ordinary.calls);
assert.ok(ordinary.frames.every(frame => !('frameImportOrdinal' in frame)));
assert.deepEqual(traced.frames.map(frame => frame.frameImportOrdinal), [1, 2, 3, 4, 1]);
assert.ok(traced.frames.every(frame => !('nativeScopeRequest' in frame)));
assert.notEqual(traced.names[0], traced.names[1]);
assert.equal(ordinary.names[0], ordinary.names[1]);
console.log('ok: restricted frame ordinals follow the grant without changing the browser call sequence');

// All snapshots intentionally have the same canonical pixels and markup.
function digest(frame) {
  const raw = { url: 'https://fixture.test', html: '', elements: [],
    regions: [{ selector: '#canvas', count: 1, html: '<canvas/>', values: [], frame,
      bitmap: { width: 16, height: 8, tileSize: 64, hashes: ['same'] } }] };
  return buildSnapshot(raw, 1).regionDigests['#canvas'];
}
const [first, polled, rescaled, moved, reopened] = ordinary.frames;
assert.equal(first.binding.pageId, 'main');
assert.equal(first.binding.frameId, 'frame');
assert.equal(first.binding.loaderId, 'doc');
assert.equal(first.binding.selector, '#canvas');
assert.equal(first.binding.surfaceId, first.sessionId);
assert.deepEqual(first.binding.viewportRect, { x: 0, y: 0, width: 16, height: 8 });
assert.equal(first.binding.deviceScaleFactor, 1);
assert.notEqual(first.mediaTimestampUs, polled.mediaTimestampUs);
assert.equal(digest(first), digest(polled));
assert.notEqual(digest(polled), digest(rescaled));
assert.notEqual(digest(rescaled), digest(moved));
assert.notEqual(digest(moved), digest(reopened));
assert.equal(digest({ sessionId: first.sessionId }), null);
console.log('ok: identical pixels bind to the captured surface, scale and viewport; missing bindings cannot authorize a region');
