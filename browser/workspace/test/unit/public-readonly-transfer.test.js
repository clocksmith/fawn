import assert from 'node:assert/strict';
import {
  attachReadOnlyNetworkGuard,
  isReadOnlyHttpMethod,
  startReadOnlyReferenceMirrorServer,
  validateReadOnlyTransferSpec,
} from '../../qualification/public-readonly-transfer.js';

assert.equal(isReadOnlyHttpMethod('GET'), true);
assert.equal(isReadOnlyHttpMethod('HEAD'), true);
assert.equal(isReadOnlyHttpMethod('POST'), false);
assert.equal(isReadOnlyHttpMethod('DELETE'), false);

// Reject submit/password/file controls in read-only transfer specs
assert.throws(
  () => validateReadOnlyTransferSpec({
    entryUrl: 'https://example.org/docs',
    fields: [{ slot: 'pw', kind: 'fill', locator: ['textbox', 'Password', 'input', 'password'] }],
    intermediateClicks: [],
    queries: [{ inputs: {} }, { inputs: {} }],
  }),
  /Forbidden input type/,
);

// Verify network guard blocks POST and cross-origin requests while permitting same-origin GET
const routes = [];
const mockPage = { route: (_pattern, fn) => { routes.push(fn); } };
const guard = attachReadOnlyNetworkGuard(mockPage, 'https://docs.example.org');
await guard.install();
assert.equal(routes.length, 1);

let continued = 0;
let aborted = 0;
await routes[0]({
  request: () => ({ method: () => 'GET', url: () => 'https://docs.example.org/search?q=1', resourceType: () => 'document' }),
  continue: async () => { continued += 1; },
  abort: async () => { aborted += 1; },
});
await routes[0]({
  request: () => ({ method: () => 'POST', url: () => 'https://docs.example.org/submit', resourceType: () => 'xhr' }),
  continue: async () => { continued += 1; },
  abort: async () => { aborted += 1; },
});
await routes[0]({
  request: () => ({ method: () => 'GET', url: () => 'https://evil.example.net/pixel', resourceType: () => 'image' }),
  continue: async () => { continued += 1; },
  abort: async () => { aborted += 1; },
});
assert.equal(continued, 1);
assert.equal(aborted, 2);
assert.equal(guard.blockedRequests.length, 2);

const mirror = await startReadOnlyReferenceMirrorServer();
try {
  const getRes = await fetch(`${mirror.origin}/docs?q=adapter&section=Specs`);
  assert.equal(getRes.status, 200);
  assert.match(await getRes.text(), /WebGPU Adapter Limits/);
  const postRes = await fetch(`${mirror.origin}/newsletter`, { method: 'POST' });
  assert.equal(postRes.status, 405);
  assert.equal(mirror.getServerWriteHits(), 1);
} finally {
  await mirror.close();
}

console.log('ok: public read-only transfer spec validation, POST/cross-origin network guard and reference mirror');
