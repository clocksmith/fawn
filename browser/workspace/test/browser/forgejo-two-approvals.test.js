// Two approvals waiting in one inbox, on a real application. Ana approves two
// supplier payments (two pull requests in a local Forgejo, each open in its own
// tab of Ben's browser) and hands both to Ben. Before Ben acts, the first
// payment's branch is changed. Ben finishes the first, then the second.
//
// What must hold: the changed payment is not merged and its inbox item says the
// application refused; the unchanged payment merges exactly once and its item
// says so; each item reports its own result and never the other's. Needs a
// running Forgejo with the two test accounts:
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
const csv = (vendor, amount) => Buffer.from(`vendor,amount\n${vendor},${amount}\n`).toString('base64');
async function openPull(vendor, amount) {
  const id = 'inv-' + Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
  await api('POST', `/contents/invoices/${id}.csv`, { content: csv(vendor, amount), message: 'Add invoice ' + id, new_branch: id, branch: 'main' });
  const pull = await api('POST', '/pulls', { title: `Pay ${vendor} ${amount} (${id})`, head: id, base: 'main' });
  return { id, vendor, number: pull.number, path: `invoices/${id}.csv` };
}
const pullState = async number => { const p = await api('GET', '/pulls/' + number);
  return { merged: p.merged, state: p.state, head: p.head.sha, mergeCommit: p.merge_commit_sha ?? null, mergeable: p.mergeable ?? null }; };
// Forgejo examines every open pull request again after activity on the base branch's pull requests, and
// refuses to merge one it has not finished examining. The unchanged payment is finished once Forgejo says
// it can be merged, as a person would wait for the page to say so.
async function readyToMerge(number) {
  const start = Date.now();
  while (!(await pullState(number)).mergeable) { assert(Date.now() - start < 30000, 'Forgejo never reported the pull request mergeable'); await wait(250); }
  return Date.now() - start;
}
// How many commits on main touch this invoice: one when its pull request merged once.
const commitsOnMain = async path => {
  const response = await fetch(`${origin}/api/v1/repos/${repo}/commits?sha=main&path=${encodeURIComponent(path)}&limit=10`,
    { headers: { authorization: 'token ' + token } });
  if (response.status === 404) return 0; // the file never reached main
  assert(response.ok, `commits for ${path}: ${response.status}`);
  return (await response.json()).length;
};

const root = mkdtempSync(join(tmpdir(), 'forgejo-two-approvals-'));
const clientConfig = JSON.parse(readFileSync(new URL('../../../../config/fawn-workspace-client.json', import.meta.url)));
const browserConfig = loadClientServiceConfig({ browserChannel: 'fawn', browserConfig: profile, headless: false, compositorTransport: 'stock-i420' });
let server, controller, service, adapter;
const report = { forgejoTwoApprovals: true, people: ['Ana: review', 'Ben: review + finish'], items: [], counts: { failedCaptures: 0, declinedNavigations: 0 } };
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
  const operator = client();
  let projectId;
  const commandIn = (project, operation, input) => operator('/api/command', { operation, projectId: project, input });
  const command = (operation, input) => commandIn(projectId, operation, input);
  await operator('/api/session', { token: new URL(server.loginUrl).hash.slice(1) });
  ({ id: projectId } = await command('create', { goal: 'Approve two supplier payments by merging their pull requests' }));
  const person = async (name, permissions) => {
    const invited = await command('invitePerson', { name, permissions });
    const send = client();
    await send('/api/inbox/session', { token: new URL(invited.link).hash.slice(1) });
    return { ...invited, inbox: async () => (await send('/api/inbox', null, 'GET')).items,
      act: (op, input) => send('/api/inbox', { op, ...input }) };
  };
  const ana = await person('Ana', ['review']), ben = await person('Ben', ['review', 'finish']);
  // One page of one of Ben's browsers. Each pending payment gets a browser of its own: within one browser,
  // starting or ending the capture behind a review re-lays-out the other pages, and the browser closes a
  // review whose surface changed, so two approvals could not both wait there.
  const tab = (project, pageId) => {
    const evaluate = expression => adapter._evaluate({ workspaceId: project, pageId }, expression)
      .catch(error => { throw Error(`${error.message} in page step: ${expression.slice(0, 90)}`); });
    return { project, pageId, evaluate,
      settle: () => evaluate('new Promise(r => requestAnimationFrame(() => requestAnimationFrame(() => r(true))))'),
      focus: () => commandIn(project, 'focusPage', { pageId }),
      async go(url) {
        for (let attempt = 0; attempt < 5; attempt++) {
          const result = await commandIn(project, 'navigate', { pageId, url });
          if (result.outcome === 'completed') return;
          assert.equal(result.neverDispatched?.code, 'stale-observation', JSON.stringify(result).slice(0, 400));
          report.counts.declinedNavigations++;
          await wait(100);
        }
        throw Error('navigation was declined five times: ' + url);
      } };
  };
  // Ben signs in to Forgejo in each browser. This stands in for him typing.
  const signedIn = async project => {
    const page = tab(project, 'main');
    await page.go(origin + '/user/login');
    await page.evaluate(`(() => { const user = document.querySelector('#user_name'); user.value = 'ben';
      document.querySelector('#password').value = ${JSON.stringify(password)}; user.form.submit(); return true; })()`);
    await wait(1500);
    assert.notEqual(await page.evaluate('location.pathname'), '/user/login', 'Ben must be signed in');
    return page;
  };
  const first = await signedIn(projectId);
  const { id: secondProject } = await operator('/api/command', { operation: 'create', input: { goal: 'Second supplier payment' } });
  const second = await signedIn(secondProject);

  // The scripted stand-in for the agent opens a pull request in a tab, opens its
  // merge form and files the confirm button for review. No model is called.
  async function prepare(page, pull) {
    await page.go(`${origin}/${repo}/pulls/${pull.number}`);
    await page.focus();
    await page.evaluate(`[...document.querySelectorAll('.merge-section button')].find(b => /Create merge commit/.test(b.textContent)).click(), true`);
    await wait(600);
    const seenPage = await controller.service.observe({ ...await controller.humanPage(page.project, page.pageId), pageId: page.pageId });
    const buttons = seenPage.elements.filter(element => element.formAction?.endsWith(`/pulls/${pull.number}/merge`)
      && /^Create merge commit$/.test(element.label ?? ''));
    assert.equal(buttons.length, 1, JSON.stringify(buttons.map(b => [b.selector, b.label])));
    const selector = buttons[0].selector;
    await page.evaluate(`document.querySelector(${JSON.stringify(selector)}).scrollIntoView({ block: 'center' }), true`);
    await page.settle();
    const { cdp, sessionId } = adapter._entry({ workspaceId: page.project, pageId: page.pageId });
    const { frameTree: { frame } } = await cdp.send('Page.getFrameTree', {}, sessionId);
    const rect = await page.evaluate(`(() => { const r = document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect();
      return { x: r.x, y: r.y, width: r.width, height: r.height, head: document.querySelector('input[name=head_commit_id]').value }; })()`);
    const { head, ...box } = rect;
    try {
      const item = await commandIn(page.project, 'proposeReview', { pageId: page.pageId, selector, title: `Pay ${pull.vendor}: merge pull request ${pull.number}`,
        completion: { completedWhen: [{ kind: 'element-present', selector: '.issue-state-label.purple' }],
          failedWhen: [{ kind: 'element-present', selector: '.flash-error, .toastify.on' }] },
        completionRegions: ['.page-content', '.toastify.on'],
        change: `Merge ${pull.id} at ${head.slice(0, 10)}`, assignee: ana.personId,
        visibleRectangle: { url: frame.url, loaderId: frame.loaderId, selector, ...box } });
      return { ...item, formHead: head };
    } catch (error) {
      if (error.code === 'review-capture-refused') report.counts.failedCaptures++;
      throw error;
    }
  }
  const only = async (who, itemId) => (await who.inbox()).find(item => item.itemId === itemId);
  const digestOf = rgba => createHash('sha256').update(Buffer.from(rgba, 'base64')).digest('hex');
  const lastAttempt = async itemId => (await only(ben, itemId)).attempts.at(-1) ?? null;

  // Two payments are filed, each in its own tab.
  const pullA = await openPull('Acme Tools', '1280.00'), pullB = await openPull('Birch Supply', '640.00');
  const itemA = await prepare(first, pullA), itemB = await prepare(second, pullB);
  // Ana reviews both from her inbox, approves each exact image and hands both to Ben.
  // Ana reviews both from her inbox, approves each exact image and hands both to Ben. Neither item needed a
  // second look: filing one did not disturb the other.
  report.counts.lookedAgain = 0;
  for (const item of [itemA, itemB]) {
    let seen = await only(ana, item.itemId);
    if (seen.status !== 'still-holds') {
      report.counts.lookedAgain++;
      report.firstItemNeededFreshLookBecause = seen.browserReason;
      await ana.act('lookAgain', { itemId: item.itemId });
      seen = await only(ana, item.itemId);
    }
    assert.equal(seen.status, 'still-holds', `${seen.title}: ${seen.browserReason}`);
    assert.equal(digestOf(seen.review.rgba), seen.review.digest, 'Ana sees the browser capture itself');
    await ana.act('approve', { itemId: item.itemId, reviewId: seen.review.reviewId });
    await ana.act('handoff', { itemId: item.itemId, to: ben.personId, note: 'Amount matches the PO.' });
  }
  // Both approvals now wait for Ben, and both still hold.
  for (const item of [itemA, itemB]) {
    const waiting = await only(ben, item.itemId);
    assert.equal(waiting.status, 'still-holds', `${waiting.title} waiting for Ben: ${waiting.browserReason}`);
  }
  // After approving, the first payment's amount is changed on its branch.
  const file = await api('GET', `/contents/${pullA.path}?ref=${pullA.id}`);
  await api('PUT', `/contents/${pullA.path}`, { content: csv('Acme Tools', '9280.00'), message: 'Revise the amount', branch: pullA.id, sha: file.sha });
  assert.notEqual((await pullState(pullA.number)).head.slice(0, 10), itemA.formHead.slice(0, 10), 'the first branch head moved');

  // Ben finishes the changed payment first. Its tab is behind the other one; the
  // workspace brings it forward. The tab still shows what Ana approved, so the
  // click is delivered; Forgejo refuses to merge a branch that moved.
  const startA = performance.now();
  const doneA = await ben.act('finish', { itemId: itemA.itemId });
  const msA = Math.round(performance.now() - startA);
  await wait(1000);
  const afterA = await pullState(pullA.number);
  const saidA = await first.evaluate(`[...document.querySelectorAll('.flash-error, .toastify.on')].filter(e => e.getClientRects().length).map(e => (e.classList.contains('toastify') ? 'message' : 'banner') + ': ' + e.textContent.trim().replace(/\\s+/g, ' ').slice(0, 120)).join(' | ')`);
  // Nothing about the second payment moved while the first was finished.
  const untouchedB = await pullState(pullB.number), waitingB = await only(ben, itemB.itemId);
  report.secondItemAfterFirstFinished = { status: waitingB.status, browserReason: waitingB.browserReason };

  // Then the unchanged payment, whose tab is now the one behind.
  report.waitedForForgejoMs = await readyToMerge(pullB.number);
  const startB = performance.now();
  const doneB = await ben.act('finish', { itemId: itemB.itemId });
  const msB = Math.round(performance.now() - startB);
  await wait(1500);
  const afterB = await pullState(pullB.number);
  const saidB = await second.evaluate(`[...document.querySelectorAll('.flash-error, .toastify.on')].filter(e => e.getClientRects().length).map(e => (e.classList.contains('toastify') ? 'message' : 'banner') + ': ' + e.textContent.trim().replace(/\\s+/g, ' ').slice(0, 160)).join(' | ')`);
  // A second finish of either item is not possible: both are closed.
  const again = [];
  for (const item of [itemA, itemB]) {
    again.push(await ben.act('finish', { itemId: item.itemId }).then(done => 'accepted: ' + JSON.stringify(done.attempt), error => error.message));
  }
  await wait(500);
  const finalA = await lastAttempt(itemA.itemId), finalB = await lastAttempt(itemB.itemId);
  report.items = [
    { item: 'A: changed after approval', pull: pullA.number, delivered: doneA.attempt.delivered, outcome: doneA.attempt.outcome, outcomeBasis: doneA.attempt.outcomeBasis,
      merged: afterA.merged, pullState: afterA.state, forgejoSaid: saidA, finishMs: msA, outcomeAfterBFinished: finalA?.outcome, commitsOnMain: await commitsOnMain(pullA.path) },
    { item: 'B: unchanged', pull: pullB.number, delivered: doneB.attempt.delivered, outcome: doneB.attempt.outcome, outcomeBasis: doneB.attempt.outcomeBasis,
      merged: afterB.merged, pullState: afterB.state, forgejoSaid: saidB, mergeableBeforeFinish: untouchedB.mergeable, finishMs: msB, mergedBeforeItsOwnFinish: untouchedB.merged, stateWhileAFinished: waitingB.state,
      commitsOnMain: await commitsOnMain(pullB.path) },
  ];
  report.secondFinish = again;
  process.stderr.write('ROWS ' + JSON.stringify(report) + '\n');

  assert.equal(doneA.attempt.delivered, true, JSON.stringify(doneA.attempt));
  assert.equal(afterA.merged, false, 'the changed payment must not merge');
  assert.equal(doneA.attempt.outcome, 'failed', JSON.stringify({ attempt: doneA.attempt, saidA }));
  assert.equal(doneA.attempt.outcomeBasis, 'failure-condition');
  assert.equal(untouchedB.merged, false, 'finishing A did not merge B');
  assert.equal(waitingB.state, 'open', 'B was still waiting for Ben after A finished');
  assert.equal(waitingB.attempts.length, 0, 'B has no result before its own finish');
  assert.equal(doneB.attempt.delivered, true, JSON.stringify(doneB.attempt));
  assert.equal(afterB.merged, true, 'the unchanged payment merged');
  assert.equal(doneB.attempt.outcome, 'completed', JSON.stringify(doneB.attempt));
  assert.equal(doneB.attempt.outcomeBasis, 'completion-condition');
  assert.equal(report.items[1].commitsOnMain, 1, 'the unchanged payment is on main exactly once');
  assert.equal(report.items[0].commitsOnMain, 0, 'the changed payment never reached main');
  assert.equal(finalA.outcome, 'failed', 'A keeps its own result after B finished');
  assert.equal(finalB.outcome, 'completed');
  assert.ok(again.every(result => /closed/.test(result)), 'neither item can be finished twice: ' + JSON.stringify(again));
  console.log(JSON.stringify(report));
} finally {
  try { await server?.close(); await controller?.close(); await service?.close(); }
  finally { rmSync(root, { recursive: true, force: true }); }
}
