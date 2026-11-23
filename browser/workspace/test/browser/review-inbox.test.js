// Two people finish approvals on a busy, changing page through the review
// inbox, without sharing a screen. Ana may review; Ben may review and finish.
// Each signs in with their own invitation and sees only inbox data: the exact
// pixels the browser captured, who approved them, and whether the browser says
// that image still holds. Browser deliveries (trusted clicks the page received)
// are logged apart from application commits.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as wait } from 'node:timers/promises';
import { openApprovalsSite } from '../../qualification/messy-approvals-site.js';
import { createCdpAdapter } from '../../src/adapters/cdp-adapter.js';
import { loadClientServiceConfig } from '../../src/client/browser-selection.js';
import { WorkspaceController } from '../../src/client/controller.js';
import { startWorkspaceServer } from '../../src/client/server.js';
import { WorkspaceService } from '../../src/service.js';

const profile = process.env.FAWN_REVIEWED_INPUT_PROFILE;
if (!profile) {
  console.log('skip: requires the native reviewed-input browser and a controlled capture chooser');
  process.exit(0);
}
const root = mkdtempSync(join(tmpdir(), 'review-inbox-'));
const clientConfig = JSON.parse(readFileSync(new URL('../../../../config/fawn-workspace-client.json', import.meta.url)));
const browserConfig = loadClientServiceConfig({ browserChannel: 'fawn', browserConfig: profile,
  headless: false, compositorTransport: 'stock-i420' });
// FAWN_INBOX_CONTROL=dom runs the same flows against an ordinary button element.
const control = process.env.FAWN_INBOX_CONTROL === 'dom' ? 'dom' : 'canvas';
const site = await openApprovalsSite({ control });
let server, controller, service, adapter;
const report = { flows: [], counts: { failedCaptures: 0, staleApprovals: 0, refusedFinishes: 0, unsafeDeliveries: 0, declinedNavigations: 0 } };
try {
  service = await WorkspaceService.open({ stateDir: join(root, 'service'), config: browserConfig,
    createAdapter(opts) { adapter = createCdpAdapter(opts); return adapter; } });
  controller = new WorkspaceController({ service, stateDir: join(root, 'client'), config: clientConfig });
  server = await startWorkspaceServer({ controller, config: clientConfig, port: 0 });
  const client = () => {
    let cookie;
    return async (path, body, method = 'POST') => {
      const response = await fetch(server.origin + path, { method, headers: { origin: server.origin,
        'content-type': 'application/json', ...(cookie ? { cookie } : {}) }, ...(method === 'POST' ? { body: JSON.stringify(body) } : {}) });
      cookie ??= response.headers.get('set-cookie')?.split(';')[0];
      const data = await response.json();
      if (!response.ok) throw Object.assign(Error(data.error), { status: response.status, code: data.code });
      return data;
    };
  };
  // The operator owns the browser, invites both people and files what the agent prepared.
  const operator = client();
  let projectId;
  const command = (operation, input) => operator('/api/command', { operation, projectId, input });
  await operator('/api/session', { token: new URL(server.loginUrl).hash.slice(1) });
  ({ id: projectId } = await command('create', { goal: 'Approve supplier payments' }));
  const person = async (name, permissions) => {
    const invited = await command('invitePerson', { name, permissions });
    const send = client();
    await send('/api/inbox/session', { token: new URL(invited.link).hash.slice(1) });
    return { ...invited, inbox: async () => (await send('/api/inbox', null, 'GET')).items,
      act: (op, input) => send('/api/inbox', { op, ...input }) };
  };
  const ana = await person('Ana', ['review']), ben = await person('Ben', ['review', 'finish']);
  const evaluate = expression => adapter._evaluate({ workspaceId: projectId, pageId: 'main' }, expression)
    .catch(error => { throw Error(`${error.message} in page step: ${expression.slice(0, 90)}`); });
  const settle = () => evaluate('new Promise(r => requestAnimationFrame(() => requestAnimationFrame(() => r(true))))');

  // The scripted stand-in for the agent opens the invoice and files one item for
  // review. No model is called.
  // The workspace declines a navigation as stale when the page changed after it
  // was observed (a receipt appearing, for one), and says so in the returned
  // record. Repeat it then; never continue on the old page.
  async function open(invoiceId) {
    for (let attempt = 0; attempt < 5; attempt++) {
      const result = await command('navigate', { pageId: 'main', url: site.login(invoiceId) });
      if (result.outcome === 'completed') {
        assert.equal(await evaluate('location.pathname'), '/approvals/' + invoiceId);
        return;
      }
      assert.equal(result.neverDispatched?.code, 'stale-observation', JSON.stringify(result).slice(0, 400));
      report.counts.declinedNavigations++;
      await wait(100);
    }
    throw Error('navigation to ' + invoiceId + ' was declined five times');
  }
  async function prepare(invoiceId, title) {
    await open(invoiceId);
    await command('focusPage', { pageId: 'main' });
    const { cdp, sessionId } = adapter._entry({ workspaceId: projectId, pageId: 'main' });
    const { frameTree: { frame } } = await cdp.send('Page.getFrameTree', {}, sessionId);
    const rect = await evaluate(`(() => { const r = document.querySelector('#approve').getBoundingClientRect();
      return { x: r.x, y: r.y, width: r.width, height: r.height }; })()`);
    // The stand-in files exactly what it prepared: the invoice it was asked for.
    assert.match(await evaluate(`document.querySelector('#approve').dataset.summary`), new RegExp(invoiceId + '$'));
    try {
      return await command('proposeReview', { pageId: 'main', selector: '#approve', title,
        completion: { completedWhen: [{ kind: 'element-present', selector: '#approve #receipt' }] },
        change: `Pay ${(await evaluate(`document.querySelector('#approve').dataset.summary`))}`, assignee: ana.personId,
        visibleRectangle: { url: frame.url, loaderId: frame.loaderId, selector: '#approve', ...rect } });
    } catch (error) {
      if (error.code === 'review-capture-refused') report.counts.failedCaptures++;
      throw error;
    }
  }
  const only = async (who, itemId) => (await who.inbox()).find(item => item.itemId === itemId);
  const flow = async (name, body) => {
    const before = { inputs: site.inputs.length, commits: site.commits.length };
    const row = { name, reviewedAgain: 0 };
    const start = performance.now();
    await body(row);
    row.msToAuthorizedOutcome = Math.round(performance.now() - start);
    await wait(300);
    row.browserDeliveries = site.inputs.length - before.inputs;
    row.applicationCommits = site.commits.length - before.commits;
    report.flows.push(row);
  };
  const digestOf = rgba => createHash('sha256').update(Buffer.from(rgba, 'base64')).digest('hex');

  // 1. The page keeps moving outside the reviewed button. Ana's approval still
  // holds when Ben picks it up, and Ben acts on exactly what Ana saw.
  // An ordinary element may be reviewed only as a visible rectangle a person
  // chose. Without one its capture would be the tab cropped to the element's
  // box, which is more than an element-only grant covers, so it is refused.
  if (control === 'dom') {
    await open('INV-1039');
    await command('focusPage', { pageId: 'main' });
    await assert.rejects(command('proposeReview', { pageId: 'main', selector: '#approve', title: 'No rectangle',
      change: 'Pay', assignee: ana.personId }), /surface unavailable/i);
    report.elementOnlyGrantRefusedForOrdinaryElement = true;
  }

  await flow('unrelated-motion-holds-across-handoff', async row => {
    const item = await prepare('INV-1042', 'Approve the Acme payment');
    assert.equal(item.status, 'still-holds');
    const seen = await only(ana, item.itemId);
    assert.equal(digestOf(seen.review.rgba), seen.review.digest, 'Ana sees the browser capture itself');
    assert.equal(seen.proposedBy, 'agent');
    await assert.rejects(ana.act('finish', { itemId: item.itemId }), /does not have finish permission/);
    await assert.rejects(ben.act('finish', { itemId: item.itemId }), /No one has approved/);
    await ana.act('approve', { itemId: item.itemId, reviewId: seen.review.reviewId });
    await ana.act('handoff', { itemId: item.itemId, to: ben.personId, note: 'Amount matches the PO. Yours to release.' });
    assert.equal((await ana.inbox()).filter(i => i.state === 'open' && i.assignee?.personId === ana.personId).length, 0);
    const verifiedBefore = BigInt((await controller.service.reviewedClickStatus({ ...controller.admin(projectId), reviewId: seen.review.reviewId })).verifiedRevision);
    await wait(1500);
    const picked = await only(ben, item.itemId);
    const verifiedAfter = BigInt((await controller.service.reviewedClickStatus({ ...controller.admin(projectId), reviewId: seen.review.reviewId })).verifiedRevision);
    row.framesVerifiedWhileWaiting = Number(verifiedAfter - verifiedBefore);
    assert(row.framesVerifiedWhileWaiting > 10, 'the page must have kept drawing frames');
    assert.equal(picked.status, 'still-holds', picked.browserReason);
    assert.equal(picked.review.digest, seen.review.digest, 'Ben sees the image Ana approved');
    assert.deepEqual(picked.approvals.map(a => [a.by, a.appliesToCurrentPixels]), [['Ana', true]]);
    assert.equal(picked.handoffs[0].note, 'Amount matches the PO. Yours to release.');
    const done = await ben.act('finish', { itemId: item.itemId });
    assert.equal(done.attempt.delivered, true, JSON.stringify(done.attempt));
    assert.equal(done.item.state, 'finished');
    await wait(300);
    assert.deepEqual(site.commits.at(-1) && [site.commits.at(-1).invoice, site.commits.at(-1).cents], ['INV-1042', 128000]);
    assert.equal(site.inputs.at(-1).target, 'approve');
  });

  // 2. The invoice is revised after Ana approves. Ben is told the image no
  // longer holds, the old approval cannot be acted on, and one fresh look by
  // Ben restores progress on the new amount.
  await flow('revised-amount-needs-fresh-look', async row => {
    const item = await prepare('INV-2077', 'Approve the Acme payment');
    const seen = await only(ana, item.itemId);
    await ana.act('approve', { itemId: item.itemId, reviewId: seen.review.reviewId });
    await ana.act('handoff', { itemId: item.itemId, to: ben.personId, note: 'Approved at $1,280.00.' });
    site.revise('INV-2077', 138000);
    for (const deadline = Date.now() + 8000; ;) {
      const shown = await evaluate(`[document.querySelector('#approve').dataset.summary, document.visibilityState, state.revision]`);
      if (shown[0].includes('1,380')) break;
      if (Date.now() > deadline) throw Error('the page never showed the revised amount: ' + JSON.stringify(shown));
      await wait(50);
    }
    await settle();
    await wait(200);
    const picked = await only(ben, item.itemId);
    assert.equal(picked.status, 'needs-fresh-look', picked.browserReason);
    assert.match(picked.statusReason, /changed after it was reviewed/);
    report.counts.staleApprovals++;
    const before = { inputs: site.inputs.length, attempts: site.attempts.length };
    const refused = await ben.act('finish', { itemId: item.itemId });
    assert.equal(refused.attempt.delivered, false);
    row.staleAttemptRefusedAs = refused.attempt.outcomeBasis;
    report.counts.refusedFinishes++;
    await wait(300);
    assert.equal(site.inputs.length, before.inputs, 'a stale approval must deliver nothing');
    assert.equal(site.attempts.length, before.attempts);
    const fresh = await ben.act('lookAgain', { itemId: item.itemId });
    row.reviewedAgain++;
    assert.equal(fresh.status, 'still-holds');
    assert.notEqual(fresh.review.digest, seen.review.digest);
    assert.equal(fresh.earlierReview.digest, seen.review.digest, 'the earlier image stays for comparison');
    assert.deepEqual(fresh.approvals.map(a => [a.by, a.appliesToCurrentPixels]), [['Ana', false]]);
    await assert.rejects(ben.act('finish', { itemId: item.itemId }), /No one has approved the current image/);
    await ben.act('approve', { itemId: item.itemId, reviewId: fresh.review.reviewId });
    const done = await ben.act('finish', { itemId: item.itemId });
    assert.equal(done.attempt.delivered, true, JSON.stringify(done.attempt));
    await wait(300);
    assert.deepEqual([site.commits.at(-1).invoice, site.commits.at(-1).cents], ['INV-2077', 138000]);
  });

  // 3. A look-alike takes the click point after approval without changing a
  // pixel. The browser cancels the click at delivery; nothing reaches the page.
  await flow('lookalike-target-swap-cancelled', async row => {
    const item = await prepare('INV-3090', 'Approve the Acme payment');
    const seen = await only(ana, item.itemId);
    await ana.act('approve', { itemId: item.itemId, reviewId: seen.review.reviewId });
    await ana.act('handoff', { itemId: item.itemId, to: ben.personId });
    await evaluate(`(() => { const d = document.createElement('div'); d.id = 'lookalike';
      d.style = 'position:absolute;left:16px;top:72px;width:320px;height:64px';
      d.onclick = () => fetch('/commit/INV-9999', { method: 'POST', body: '{}' }); document.body.append(d); return true; })()`);
    await settle();
    const before = { inputs: site.inputs.length, attempts: site.attempts.length };
    const refused = await ben.act('finish', { itemId: item.itemId });
    assert.equal(refused.attempt.delivered, false, JSON.stringify(refused.attempt));
    assert.equal(refused.attempt.admitted, true, 'pixels were unchanged, so the browser admitted and then cancelled');
    row.staleAttemptRefusedAs = refused.attempt.outcomeBasis;
    report.counts.refusedFinishes++;
    await wait(300);
    assert.equal(site.inputs.length, before.inputs, 'the look-alike must receive nothing');
    assert.equal(site.attempts.length, before.attempts);
    assert.equal((await only(ben, item.itemId)).status, 'needs-fresh-look');
    await evaluate(`document.querySelector('#lookalike').remove(), true`);
    await settle();
    const fresh = await ben.act('lookAgain', { itemId: item.itemId });
    row.reviewedAgain++;
    await ben.act('approve', { itemId: item.itemId, reviewId: fresh.review.reviewId });
    const done = await ben.act('finish', { itemId: item.itemId });
    assert.equal(done.attempt.delivered, true, JSON.stringify(done.attempt));
    await wait(300);
    assert.equal(site.commits.at(-1).invoice, 'INV-3090');
  });

  // Every trusted click the page received was on the approve button, showing
  // the amount that was approved for it, and led to exactly one commit.
  report.counts.unsafeDeliveries = site.inputs.filter(input => input.target !== 'approve').length
    + site.commits.filter((commit, i) => !site.inputs[i]?.shown.includes(site.money(commit.cents))).length;
  assert.equal(report.counts.unsafeDeliveries, 0);
  assert.equal(site.inputs.length, 3);
  assert.equal(site.commits.length, 3);
  assert.equal(site.attempts.length, 3);
  console.log(JSON.stringify({ reviewInbox: true, people: ['Ana: review', 'Ben: review + finish'], ...report,
    browserDeliveries: site.inputs.length, applicationCommits: site.commits.length, applicationAttempts: site.attempts.length }));
} finally {
  try { await server?.close(); await controller?.close(); await service?.close(); }
  finally { await site.close(); rmSync(root, { recursive: true, force: true }); }
}
