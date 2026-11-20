import assert from 'node:assert/strict';
import { CompositorSessions } from '../../src/adapters/compositor-capture.js';

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
