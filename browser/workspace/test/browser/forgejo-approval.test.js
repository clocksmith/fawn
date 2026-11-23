// A real application through the review inbox: two people approve a payment by
// merging its pull request in a local Forgejo. Ben's browser is signed in to
// Forgejo; Ana reviews the confirm button from her inbox and hands it to Ben,
// who finishes. The confirm button is an ordinary submit button in a form that
// carries the head commit, and Forgejo itself refuses a merge whose branch has
// moved. Needs a running Forgejo with the two test accounts:
//   FORGEJO_ORIGIN, FORGEJO_PASSWORD_FILE (Ben's), FORGEJO_TOKEN_FILE (Ana's API token)
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as wait } from 'node:timers/promises';
import { createCdpAdapter } from '../../src/adapters/cdp-adapter.js';
import { loadClientServiceConfig } from '../../src/client/browser-selection.js';
import { WorkspaceController } from '../../src/client/controller.js';
import { startWorkspaceServer } from '../../src/client/server.js';
import { WorkspaceService } from '../../src/service.js';

const profile = process.env.FAWN_REVIEWED_INPUT_PROFILE, origin = process.env.FORGEJO_ORIGIN;
if (!profile || !origin || !process.env.FORGEJO_PASSWORD_FILE || !process.env.FORGEJO_TOKEN_FILE) {
  console.log('skip: requires the native reviewed-input browser and a local Forgejo with the two test accounts');
  process.exit(0);
}
const password = readFileSync(process.env.FORGEJO_PASSWORD_FILE, 'utf8').trim();
const token = readFileSync(process.env.FORGEJO_TOKEN_FILE, 'utf8').trim();
const repo = process.env.FORGEJO_REPO ?? 'ana/payables';
const api = async (method, path, body) => {
  const response = await fetch(`${origin}/api/v1/repos/${repo}${path}`, { method,
    headers: { authorization: 'token ' + token, 'content-type': 'application/json' }, ...(body ? { body: JSON.stringify(body) } : {}) });
  const data = await response.json().catch(() => null);
  assert(response.ok, `${method} ${path}: ${response.status} ${JSON.stringify(data).slice(0, 200)}`);
  return data;
};
const csv = amount => Buffer.from(`vendor,amount\nAcme Tools,${amount}\n`).toString('base64');
// Ana opens a pull request that adds one invoice.
async function openPull(amount) {
  const id = 'inv-' + Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
  await api('POST', `/contents/invoices/${id}.csv`, { content: csv(amount), message: 'Add invoice ' + id, new_branch: id, branch: 'main' });
  const pull = await api('POST', '/pulls', { title: `Pay Acme Tools ${amount} (${id})`, head: id, base: 'main' });
  return { id, number: pull.number, path: `invoices/${id}.csv` };
}
const pullState = async number => { const p = await api('GET', '/pulls/' + number); return { merged: p.merged, state: p.state, head: p.head.sha }; };
const root = mkdtempSync(join(tmpdir(), 'forgejo-approval-'));
const clientConfig = JSON.parse(readFileSync(new URL('../../../../config/fawn-workspace-client.json', import.meta.url)));
const browserConfig = loadClientServiceConfig({ browserChannel: 'fawn', browserConfig: profile,
  headless: false, compositorTransport: 'stock-i420' });
let server, controller, service, adapter;
const report = { flows: [], counts: { failedCaptures: 0, refusedFinishes: 0, declinedNavigations: 0 } };
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
  ({ id: projectId } = await command('create', { goal: 'Approve supplier payments by merging their pull requests' }));
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

  const go = async url => {
    for (let attempt = 0; attempt < 5; attempt++) {
      const result = await command('navigate', { pageId: 'main', url });
      if (result.outcome === 'completed') return;
      assert.equal(result.neverDispatched?.code, 'stale-observation', JSON.stringify(result).slice(0, 400));
      report.counts.declinedNavigations++;
      await wait(100);
    }
    throw Error('navigation was declined five times: ' + url);
  };
  // Ben signs in to Forgejo in the shared browser. This stands in for him typing.
  await go(origin + '/user/login');
  await evaluate(`(() => { const user = document.querySelector('#user_name'); user.value = 'ben';
    document.querySelector('#password').value = ${JSON.stringify(password)}; user.form.submit(); return true; })()`);
  await wait(1500);
  assert.notEqual(await evaluate('location.pathname'), '/user/login', 'Ben must be signed in');

  // The scripted stand-in for the agent opens the pull request, opens its merge
  // form and files the confirm button for review. No model is called.
  async function prepare(pull) {
    await go(`${origin}/${repo}/pulls/${pull.number}`);
    await command('focusPage', { pageId: 'main' });
    await evaluate(`[...document.querySelectorAll('.merge-section button')].find(b => /Create merge commit/.test(b.textContent)).click(), true`);
    await wait(600);
    // The stand-in names the button the way an agent would: by the selector the
    // workspace's own observation gives the form's confirm button.
    const seenPage = await controller.service.observe({ ...await controller.humanPage(projectId, 'main'), pageId: 'main' });
    const buttons = seenPage.elements.filter(element => element.formAction?.endsWith(`/pulls/${pull.number}/merge`)
      && /^Create merge commit$/.test(element.label ?? ''));
    assert.equal(buttons.length, 1, JSON.stringify(buttons.map(b => [b.selector, b.label])));
    const selector = buttons[0].selector;
    await evaluate(`document.querySelector(${JSON.stringify(selector)}).scrollIntoView({ block: 'center' }), true`);
    await settle();
    const { cdp, sessionId } = adapter._entry({ workspaceId: projectId, pageId: 'main' });
    const { frameTree: { frame } } = await cdp.send('Page.getFrameTree', {}, sessionId);
    const rect = await evaluate(`(() => { const r = document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect();
      return { x: r.x, y: r.y, width: r.width, height: r.height, head: document.querySelector('input[name=head_commit_id]').value }; })()`);
    const { head, ...box } = rect;
    try {
      const item = await command('proposeReview', { pageId: 'main', selector, title: `Merge pull request ${pull.number}`,
        // Forgejo shows the result outside the button and reloads the page: a
        // purple state label when merged, an error banner when it refuses. The
        // stand-in asks to read the page's main content, which holds both.
        completion: { completedWhen: [{ kind: 'element-present', selector: '.issue-state-label.purple' }],
          failedWhen: [{ kind: 'element-present', selector: '.flash-error' }] },
        completionRegions: ['.page-content'],
        change: `Merge ${pull.id} at ${head.slice(0, 10)}`, assignee: ana.personId,
        visibleRectangle: { url: frame.url, loaderId: frame.loaderId, selector, ...box } });
      return { ...item, formHead: head, box };
    } catch (error) {
      if (error.code === 'review-capture-refused') report.counts.failedCaptures++;
      throw error;
    }
  }
  const only = async (who, itemId) => (await who.inbox()).find(item => item.itemId === itemId);
  const digestOf = rgba => createHash('sha256').update(Buffer.from(rgba, 'base64')).digest('hex');
  const flow = async (name, run) => {
    const row = { name }, start = performance.now();
    await run(row);
    row.ms = Math.round(performance.now() - start);
    report.flows.push(row);
  };

  // 1. Ana reviews the confirm button, approves and hands off. Ben sees the same
  // image and finishes; Forgejo merges the pull request.
  await flow('approved-handoff-merges', async row => {
    const pull = await openPull('1280.00');
    const item = await prepare(pull);
    row.buttonBox = item.box;
    assert.equal(item.status, 'still-holds');
    const seen = await only(ana, item.itemId);
    assert.equal(digestOf(seen.review.rgba), seen.review.digest, 'Ana sees the browser capture itself');
    await assert.rejects(ana.act('finish', { itemId: item.itemId }), /does not have finish permission/);
    await ana.act('approve', { itemId: item.itemId, reviewId: seen.review.reviewId });
    await ana.act('handoff', { itemId: item.itemId, to: ben.personId, note: 'Amount matches the PO.' });
    const picked = await only(ben, item.itemId);
    assert.equal(picked.status, 'still-holds', picked.browserReason);
    assert.equal(picked.review.digest, seen.review.digest, 'Ben sees the image Ana approved');
    const done = await ben.act('finish', { itemId: item.itemId });
    assert.equal(done.attempt.delivered, true, JSON.stringify(done.attempt));
    await wait(1500);
    const after = await pullState(pull.number);
    Object.assign(row, { delivered: true, merged: after.merged, itemState: done.item.state, outcome: done.attempt.outcome, outcomeBasis: done.attempt.outcomeBasis });
    assert.equal(after.merged, true, 'Forgejo merged the pull request');
    assert.equal(done.attempt.outcome, 'completed', JSON.stringify(done.attempt));
    assert.deepEqual((await only(ben, item.itemId))?.readsAfterClick ?? picked.readsAfterClick, ['.page-content']);
  });

  // 2. Ana pushes a correction to the branch after approving. The page Ben's
  // browser shows does not change, so the reviewed image still holds and the
  // click is delivered. Forgejo refuses the merge because the head moved.
  await flow('branch-moved-after-approval-refused-by-forgejo', async row => {
    const pull = await openPull('1280.00');
    const item = await prepare(pull);
    const seen = await only(ana, item.itemId);
    await ana.act('approve', { itemId: item.itemId, reviewId: seen.review.reviewId });
    await ana.act('handoff', { itemId: item.itemId, to: ben.personId });
    const file = await api('GET', `/contents/${pull.path}?ref=${pull.id}`);
    await api('PUT', `/contents/${pull.path}`, { content: csv('9280.00'), message: 'Revise the amount', branch: pull.id, sha: file.sha });
    const moved = await pullState(pull.number);
    assert.notEqual(moved.head.slice(0, 10), item.formHead.slice(0, 10), 'the branch head moved');
    const done = await ben.act('finish', { itemId: item.itemId });
    await wait(2000);
    const after = await pullState(pull.number);
    const message = await evaluate(`[...document.querySelectorAll('.flash-error, .ui.negative.message, .flash-message')].filter(e => e.getClientRects().length).map(e => e.className + ': ' + e.textContent).map(e => ({ textContent: e })).map(e => e.textContent.trim().replace(/\\s+/g, ' ').slice(0, 120)).join(' | ')`);
    Object.assign(row, { delivered: done.attempt.delivered, admitted: done.attempt.admitted, outcome: done.attempt.outcome,
      outcomeBasis: done.attempt.outcomeBasis, merged: after.merged, pullState: after.state, forgejoSaid: message });
    assert.equal(after.merged, false, 'Forgejo must not merge a pull request whose head moved after the form was opened');
    assert.equal(done.attempt.outcome, 'failed', JSON.stringify(done.attempt));
    assert.equal(done.attempt.outcomeBasis, 'failure-condition');
  });

  report.deliveries = report.flows.filter(row => row.delivered).length;
  report.merges = report.flows.filter(row => row.merged).length;
  console.log(JSON.stringify({ forgejoApproval: true, people: ['Ana: review', 'Ben: review + finish'], ...report }));
} finally {
  try { await server?.close(); await controller?.close(); await service?.close(); }
  finally { rmSync(root, { recursive: true, force: true }); }
}
