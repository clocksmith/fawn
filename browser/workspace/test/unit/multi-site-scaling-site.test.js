import assert from 'node:assert/strict';
import {
  SCALING_SITES,
  allowedSiteLocators,
  buildDynamicVisitRows,
  buildMultiSiteScalingSpec,
  startMultiSiteScalingServer,
} from '../../qualification/multi-site-scaling-site.js';

const spec = buildMultiSiteScalingSpec();
assert.equal(spec.sites.length, 4);
assert.equal(spec.taskCount, 28, 'Scaling suite must contain 28 tasks (inside 24..30)');
assert.equal(spec.safetyCount, 4, 'One distinct lookalike/ambiguity safety case per site');
assert.equal(spec.orders.forward.tasks.length, 28);
assert.equal(spec.orders.mirrored.tasks.length, 28);

// Forward and mirrored orders contain the exact same 28 taskIds while reversing per-round site order
const fwdIds = spec.orders.forward.tasks.map(t => t.taskId);
const mirIds = spec.orders.mirrored.tasks.map(t => t.taskId);
assert.deepEqual([...fwdIds].sort(), [...mirIds].sort());
assert.notDeepEqual(fwdIds.slice(0, 4), mirIds.slice(0, 4));
assert.deepEqual(fwdIds.slice(0, 4).reverse(), mirIds.slice(0, 4));

// Dynamic list rows rotate count and labels on every single visit
for (const site of SCALING_SITES) {
  const v1 = buildDynamicVisitRows(site, 1);
  const v2 = buildDynamicVisitRows(site, 2);
  const v3 = buildDynamicVisitRows(site, 3);
  assert.notDeepEqual(v1.map(r => r.label), v2.map(r => r.label));
  assert.notDeepEqual(v2.map(r => r.label), v3.map(r => r.label));
  assert.ok(allowedSiteLocators(site).fields.length === 7);
}

// Verify live loopback HTTP server rotates list items across visits and enforces safety counters
const server = await startMultiSiteScalingServer();
try {
  const site = SCALING_SITES[0];
  const url = server.createSession('scale-s1', {
    siteId: site.siteId,
    startScreen: 'search',
    gateMode: 'v1',
    accountScope: site.accountScope,
    expectedAccountScope: site.accountScope,
    expectedGoal: 'primary-review',
  });
  const htmlVisit1 = await (await fetch(url)).text();
  const htmlVisit2 = await (await fetch(url)).text();
  assert.match(htmlVisit1, /Visit #1/);
  assert.match(htmlVisit2, /Visit #2/);
  assert.notEqual(htmlVisit1, htmlVisit2, 'Every visit renders changing list rows');

  // Duplicate gate fault renders 2 identical buttons
  const helpdesk = SCALING_SITES[1];
  const dupUrl = server.createSession('scale-dup', {
    siteId: helpdesk.siteId,
    startScreen: 'gate',
    gateMode: 'duplicate-gate',
    accountScope: helpdesk.accountScope,
    expectedAccountScope: helpdesk.accountScope,
    expectedGoal: 'primary-review',
  });
  const dupHtml = await (await fetch(dupUrl)).text();
  const btnMatches = [...dupHtml.matchAll(new RegExp(helpdesk.labels.gateV1Button, 'g'))];
  assert.equal(btnMatches.length, 2);
} finally {
  await server.close();
}

console.log('ok: 28-task 4-site mirrored scaling spec, per-visit dynamic list rotation and 4 lookalike/ambiguity fixtures');
