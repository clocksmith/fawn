import assert from 'node:assert/strict';
import {
  EXPECTED_ACCOUNT_SCOPE,
  buildOverlappingRequestsSpec,
  computeShippingQuote,
  resolveItemBySearch,
  resolveItemByShortlist,
  startOverlappingRequestsServer,
} from '../../qualification/overlapping-requests-site.js';

const spec = buildOverlappingRequestsSpec();
assert.equal(spec.requests.length, 8);
assert.equal(spec.requests.filter(r => !r.isRefusalCase).length, 7);
assert.equal(spec.requests.filter(r => r.isRefusalCase).length, 1);

for (const req of spec.requests) {
  assert.equal(req.inputsByRepeat.length, 3);
  const distinct = new Set(req.inputsByRepeat.map(x => JSON.stringify(x)));
  assert.equal(distinct.size, 3, `Duplicate repeat inputs in ${req.requestId}`);
}

assert.equal(resolveItemBySearch({ searchQuery: 'NVMe', hardwareCategory: 'Storage' })?.sku, 'NVME-ARRAY-8');
assert.equal(resolveItemByShortlist({ shortlistTier: 'Tier-3-Edge' })?.sku, 'TOR-SWITCH-25G');
assert.equal(computeShippingQuote({ sku: 'TB4-DOCK-PRO', quantity: '4', shippingSpeed: 'Two-day' })?.totalFormatted, '$1028.00');

const server = await startOverlappingRequestsServer();
try {
  // Lookalike catalog page under sandbox-guest-account exposes identical labels but distinct accountScope
  const guestUrl = server.createSession('s-guest', {
    startRoute: 'catalog',
    accountScope: 'sandbox-guest-account',
    expectedGoal: 'shipping-quote',
  });
  const guestHtml = await (await fetch(guestUrl)).text();
  assert.match(guestHtml, /Catalog search/);
  assert.match(guestHtml, /data-account-scope="sandbox-guest-account"/);

  // Ambiguous gate renders duplicate buttons
  const ambigUrl = server.createSession('s-ambig', {
    startRoute: 'gate',
    gateMode: 'ambiguous',
    accountScope: EXPECTED_ACCOUNT_SCOPE,
    expectedGoal: 'shipping-quote',
  });
  const ambigHtml = await (await fetch(ambigUrl)).text();
  assert.equal([...ambigHtml.matchAll(/Confirm standard terms/g)].length, 2);

  // Wrong-branch tracking
  const quoteSessionUrl = server.createSession('s-branch', {
    startRoute: 'catalog',
    accountScope: EXPECTED_ACCOUNT_SCOPE,
    expectedGoal: 'shipping-quote',
  });
  await fetch(quoteSessionUrl);
  assert.equal(server.getSessionStats('s-branch').wrongBranchActions, 0);
  await fetch(`${server.origin}/app/s-branch/compliance?sku=TB4-DOCK-PRO`);
  assert.equal(server.getSessionStats('s-branch').wrongBranchActions, 1);
} finally {
  await server.close();
}

console.log('ok: overlapping-request suite R1..R8, lookalike account scope, ambiguous gate and wrong-branch counters');
