// The review inbox extension on a real application, driven through its own
// page the way a person would use it: pick a button, approve it, finish it,
// reconcile it. Nothing here calls into the extension except one Finish sent as
// a message, to show that an item whose result is unknown refuses it.
//
// Each item is a supplier payment: a pull request in a local Forgejo, opened by
// Ana. Ben picks its merge button in his own tab and approves it. Then, per case:
//   control       Ben finishes it                         -> merged once, "Merge confirmed"
//   stale         Ana pushes to its branch first          -> "Click blocked", stale, not merged
//   reload        Ben's tab is reloaded first             -> "Click blocked" with no Finish pressed
//   refused       Ana lands a conflicting file on main    -> "App refused", not merged
//   after-reset   the merge request reaches Forgejo, and  -> unknown; Reconcile reads Forgejo:
//   after-502       the page gets a connection reset or a     merged once, "Merge confirmed"
//                   502 instead of Forgejo's answer
//   before-reset  the merge request never reaches         -> unknown; Reconcile reads Forgejo:
//   before-502      Forgejo; the page gets the same reset     still unknown at once, "Not applied"
//                   or 502 after the same delay               after 30 s, and never merged
// The two "after" and "before" forms look the same from the browser: the request
// is held for the same time from when it leaves the page and then fails the same
// way. Only Forgejo's record differs. Every result is scored from Forgejo's
// records (the pull request's merged state, its merge commit and the number of
// merge commits for it on main); a second merge of any pull request fails the run.
//   FAWN_EXTENSION_BROWSER (chrome binary), FORGEJO_ORIGIN, FORGEJO_PASSWORD_FILE (Ben's),
//   FORGEJO_TOKEN_FILE (Ana's API token); FORGEJO_CASES (comma list, default control,stale,reload,refused),
//   FAWN_REPEATS (default 1), FAWN_INBOX_SCREENSHOT (optional PNG path)
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as wait } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';

const binary = process.env.FAWN_EXTENSION_BROWSER, origin = process.env.FORGEJO_ORIGIN;
if (!binary || !origin || !process.env.FORGEJO_PASSWORD_FILE || !process.env.FORGEJO_TOKEN_FILE) {
  console.log('skip: requires a Chrome binary and a local Forgejo with the two test accounts');
  process.exit(0);
}
// Loaded after the checks above, so a machine without the browser or without playwright-core skips instead of failing.
const { chromium } = await import('playwright-core').catch(error => { if (error.code !== 'ERR_MODULE_NOT_FOUND') throw error; return {}; });
if (!chromium) { console.log('skip: requires playwright-core'); process.exit(0); }
const { REASON, SETTLE_MS } = await import('../lifecycle.js');
const extension = process.env.FAWN_EXTENSION_DIR ?? fileURLToPath(new URL('..', import.meta.url));
const password = readFileSync(process.env.FORGEJO_PASSWORD_FILE, 'utf8').trim();
const token = readFileSync(process.env.FORGEJO_TOKEN_FILE, 'utf8').trim();
const repo = process.env.FORGEJO_REPO ?? 'ana/payables';
const CASES = (process.env.FORGEJO_CASES ?? 'control,stale,reload,refused').split(',');
const REPEATS = Number(process.env.FAWN_REPEATS ?? 1);
// How long an interrupted merge request is held, from when it leaves the page to when it fails, in both forms.
const HOLD_MS = 2500;
const known = ['control', 'stale', 'reload', 'refused', 'after-reset', 'after-502', 'before-reset', 'before-502'];
assert(CASES.every(name => known.includes(name)), `cases are ${known.join(', ')}`);

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
  return { id, vendor, number: pull.number, path: `invoices/${id}.csv`, head: pull.head.sha };
}
const pullState = async number => { const p = await api('GET', '/pulls/' + number);
  return { merged: p.merged, state: p.state, head: p.head.sha, mergeable: p.mergeable ?? null, mergeCommit: p.merge_commit_sha ?? null }; };
// Forgejo's own count of merges of a pull request: merge commits on main that name it. Forgejo serves at most
// 50 commits a page; the newest 200 cover every merge a run of this test can make.
async function mergesOnMain(number) {
  const found = [];
  for (let page = 1; page <= 4; page++) {
    const commits = await api('GET', `/commits?sha=main&limit=50&page=${page}&stat=false&verification=false&files=false`);
    found.push(...commits.filter(commit => commit.parents.length > 1 && commit.commit.message.includes(`/pulls/${number}\n`)).map(commit => commit.sha));
    if (commits.length < 50) break;
  }
  return found;
}
// Forgejo refuses to merge a pull request it has not finished examining; a person waits for the page to say it can merge.
async function readyToMerge(number) {
  const start = Date.now();
  while (!(await pullState(number)).mergeable) { assert(Date.now() - start < 30000, 'Forgejo never reported the pull request mergeable'); await wait(250); }
}
const sha256 = path => createHash('sha256').update(readFileSync(path)).digest('hex');

const profile = mkdtempSync(join(tmpdir(), 'fawn-inbox-extension-'));
const report = { reviewInboxExtension: true, chromeSha256: sha256(binary), backgroundSha256: sha256(join(extension, 'background.js')),
  cases: CASES, repeats: REPEATS, holdMs: HOLD_MS, items: [] };
let context, failed = false;
try {
  context = await chromium.launchPersistentContext(profile, { executablePath: binary, headless: false, chromiumSandbox: true,
    viewport: null, ignoreDefaultArgs: ['--disable-extensions'],
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--disable-gpu-compositing', '--force-device-scale-factor=1',
      `--disable-extensions-except=${extension}`, `--load-extension=${extension}`, '--disable-features=DisableLoadExtensionCommandLineSwitch'] });
  report.browserVersion = context.browser()?.version() ?? null;
  const worker = context.serviceWorkers()[0] ?? await context.waitForEvent('serviceworker');
  const extensionId = new URL(worker.url()).host;

  // Ben signs in.
  const first = context.pages()[0] ?? await context.newPage();
  await first.goto(origin + '/user/login');
  await first.fill('#user_name', 'ben'); await first.fill('#password', password);
  await Promise.all([first.waitForNavigation(), first.evaluate(() => document.querySelector('#user_name').form.submit())]);
  assert.notEqual(new URL(first.url()).pathname, '/user/login', 'Ben must be signed in');
  // The inbox, opened as a page. In Chrome it is the side panel; it is the same page.
  const inbox = first;
  await inbox.goto(`chrome-extension://${extensionId}/inbox.html`);
  const cardFor = pull => inbox.locator('[data-item]', { hasText: `Merge pull request #${pull.number} ` });
  const cardOf = pull => cardFor(pull).evaluate(card => ({ state: card.dataset.state,
    text: card.querySelector('[data-outcome] strong')?.textContent ?? null, detail: card.querySelector('[data-outcome] span')?.textContent ?? null,
    offers: [...card.querySelectorAll('[data-action]')].map(button => button.dataset.action) }));
  const waitForState = async (pull, states, timeout = 30000) => {
    const selector = states.map(state => `[data-item][data-state=${state}]`).join(', ');
    await inbox.locator(selector, { hasText: `Merge pull request #${pull.number} ` }).waitFor({ timeout }).catch(async error => {
      throw new Error(`#${pull.number} never became ${states.join('/')}: ${JSON.stringify(await cardOf(pull).catch(() => null))} notice=${JSON.stringify(await inbox.locator('[data-notice]').textContent())} (${error.message.split('\n')[0]})`);
    });
    return cardOf(pull);
  };
  const confirm = (page, pull) => page.locator(`form[action$="/pulls/${pull.number}/merge"] button[type=submit]`, { hasText: /^\s*Create merge commit\s*$/ });

  // Ben opens the pull request's merge form in its own tab, picks the button through the inbox, and approves it.
  async function pickAndApprove(pull) {
    const page = await context.newPage();
    await page.goto(`${origin}/${repo}/pulls/${pull.number}`);
    await page.locator('.merge-section button', { hasText: 'Create merge commit' }).first().click();
    await confirm(page, pull).waitFor({ state: 'visible' });
    await inbox.bringToFront();
    await inbox.locator('[data-tabs]').selectOption({ label: await page.title() });
    await inbox.locator('[data-pick]').click();
    await page.getByText('Click the button you want approved').waitFor({ state: 'visible', timeout: 15000 });
    await confirm(page, pull).scrollIntoViewIfNeeded();
    const box = await confirm(page, pull).boundingBox();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
    await inbox.bringToFront();
    await waitForState(pull, ['pending'], 15000);
    assert.equal((await pullState(pull.number)).merged, false, 'picking a button must not press it');
    await page.bringToFront(); await wait(200);
    await inbox.bringToFront();
    await cardFor(pull).locator('[data-action=approve]').click();
    await waitForState(pull, ['approved'], 15000);
    return page;
  }
  // The merge request is held from when it leaves the page, then fails as `form` says: before it reaches
  // Forgejo, or after Forgejo has answered, in which case Forgejo's answer is thrown away. Through the
  // browser's own request interception on Ben's tab, which the page and the extension cannot tell from a
  // dropped connection or a gateway.
  async function interrupt(page, pull, when, form) {
    const cdp = await context.newCDPSession(page), seen = { when, form };
    await cdp.send('Fetch.enable', { patterns: [{ urlPattern: `*/pulls/${pull.number}/merge`, requestStage: 'Request' }] });
    const fail = requestId => form === 'reset'
      ? cdp.send('Fetch.failRequest', { requestId, errorReason: 'ConnectionReset' })
      : cdp.send('Fetch.fulfillRequest', { requestId, responseCode: 502, responseHeaders: [{ name: 'content-type', value: 'text/html' }],
        body: Buffer.from('<html><body><h1>502 Bad Gateway</h1></body></html>').toString('base64') });
    cdp.on('Fetch.requestPaused', async event => {
      try {
        if (event.request.method !== 'POST') return await cdp.send('Fetch.continueRequest', { requestId: event.requestId });
        const atResponse = event.responseStatusCode !== undefined || event.responseErrorReason !== undefined;
        if (!atResponse) {
          seen.leftPageAt = Date.now(); seen.requests = (seen.requests ?? 0) + 1;
          if (when === 'after') return await cdp.send('Fetch.continueRequest', { requestId: event.requestId, interceptResponse: true });
        } else seen.forgejoAnswered = event.responseStatusCode ?? event.responseErrorReason;
        await wait(Math.max(0, seen.leftPageAt + HOLD_MS - Date.now()));
        seen.heldMs = Date.now() - seen.leftPageAt;
        await fail(event.requestId);
      } catch (error) { seen.error = error.message; }
    });
    return { seen, stop: () => cdp.detach().catch(() => null) };
  }
  const forgejo = async pull => { const state = await pullState(pull.number), merges = await mergesOnMain(pull.number);
    return { merged: state.merged, state: state.state, mergeCommit: state.mergeCommit, merges: merges.length, mergeIsTheRecorded: !state.mergeCommit || merges.includes(state.mergeCommit) }; };
  const finishMessage = id => inbox.evaluate(item => chrome.runtime.sendMessage({ inbox: 'finish', id: item }), id);
  // The inbox's own record of an item, to know when a press has been answered; the result is read from the card.
  const stored = id => inbox.evaluate(async item => (await chrome.storage.local.get('items')).items[item], id);
  const reconcile = async (pull, id) => {
    const readsBefore = (await stored(id)).earlier?.length ?? 0;
    await cardFor(pull).locator('[data-action=reconcile]').click();
    for (const end = Date.now() + 20000; ;) {
      const item = await stored(id);
      if (item.state !== 'reconciling' && (item.earlier?.length ?? 0) > readsBefore) return waitForState(pull, [item.state]);
      assert(Date.now() < end, `reconcile of #${pull.number} never answered`);
      await wait(100);
    }
  };

  const later = []; // before-commit items, reconciled again once enough time has passed
  for (let round = 0; round < REPEATS; round++) for (const name of CASES) {
    const pull = await openPull(`Vendor ${name} ${round}`, (100 + round).toFixed(2));
    const row = { case: name, round, pull: pull.number };
    report.items.push(row);
    const page = await pickAndApprove(pull);
    const id = await cardFor(pull).getAttribute('data-item');
    const posts = []; page.on('request', request => { if (request.method() === 'POST' && request.url().endsWith(`/pulls/${pull.number}/merge`)) posts.push(Date.now()); });
    await readyToMerge(pull.number);
    let cut = null;
    if (name === 'stale') {
      const file = await api('GET', `/contents/${pull.path}?ref=${pull.id}`);
      await api('PUT', `/contents/${pull.path}`, { content: csv(pull.vendor, '9100.00'), message: 'Revise the amount', branch: pull.id, sha: file.sha });
    }
    if (name === 'refused') {
      await api('POST', `/contents/${pull.path}`, { content: csv(pull.vendor, '1.00'), message: 'Conflicting invoice', branch: 'main' });
    }
    if (name === 'reload') { await page.reload(); await wait(500); }
    if (name.includes('-')) { const [when, form] = name.split('-'); cut = await interrupt(page, pull, when, form); }
    await inbox.bringToFront();
    if (name !== 'reload') await cardFor(pull).locator('[data-action=finish]').click();
    row.inbox = await waitForState(pull, ['confirmed', 'refused', 'blocked', 'unknown']);
    row.mergeRequestsFromPage = posts.length;
    if (cut) {
      row.interruption = cut.seen;
      // An unknown result offers no Finish, and one sent anyway is refused.
      row.secondFinish = await finishMessage(id);
      // Reconcile at once: Forgejo's record decides, whatever the page saw.
      row.firstReconcile = await reconcile(pull, id);
      if (row.firstReconcile.state === 'unknown') later.push({ pull, row, id, readyAt: Date.now() });
      await cut.stop();
    }
    row.forgejo = await forgejo(pull);
    await page.close();
  }
  if (later.length) {
    // The record is believed to show no merge only SETTLE_MS after the click; wait that long, then reconcile again.
    await wait(Math.max(0, later.at(-1).readyAt + SETTLE_MS + 1000 - Date.now()));
    for (const { pull, row, id } of later) {
      row.secondReconcile = await reconcile(pull, id);
      await wait(1000);
      // Nothing moves it on by itself.
      row.afterwards = await cardOf(pull);
      row.forgejo = await forgejo(pull);
    }
  }
  if (process.env.FAWN_INBOX_SCREENSHOT) await inbox.screenshot({ path: process.env.FAWN_INBOX_SCREENSHOT, fullPage: true });

  for (const row of report.items) {
    const at = `${row.case} #${row.pull}: ${JSON.stringify(row)}`;
    // Hard limits first: one approval is at most one merge, and the merge Forgejo records is the one on main.
    assert.ok(row.forgejo.merges <= 1, `duplicate merge, ${at}`);
    assert.ok(row.forgejo.mergeIsTheRecorded, at);
    const merged = row.forgejo.merged && row.forgejo.merges === 1;
    if (row.case === 'control') { assert.ok(merged, at); assert.deepEqual([row.inbox.state, row.inbox.text], ['confirmed', 'Merge confirmed'], at); }
    if (row.case === 'stale') {
      assert.equal(row.forgejo.merged, false, at); assert.equal(row.forgejo.merges, 0, at);
      assert.equal(row.inbox.state, 'blocked', at); assert.match(row.inbox.detail, /^Stale approval: pull request #\d+ now has head commit/, at);
      assert.equal(row.mergeRequestsFromPage, 0, `no merge request left the page, ${at}`);
    }
    if (row.case === 'reload') {
      assert.equal(row.forgejo.merges, 0, at); assert.equal(row.inbox.state, 'blocked', at); assert.match(row.inbox.detail, /reloaded or navigated/, at);
    }
    if (row.case === 'refused') {
      assert.equal(row.forgejo.merges, 0, at); assert.equal(row.inbox.state, 'refused', at);
      assert.match(row.inbox.detail ?? '', /conflict|not ready to be merged|head was updated/i, at);
    }
    if (row.case.includes('-')) {
      assert.equal(row.interruption.requests, 1, `exactly one merge request left the page, ${at}`);
      assert.ok(Math.abs(row.interruption.heldMs - HOLD_MS) < 500, `held for the agreed time, ${at}`);
      assert.equal(row.inbox.state, 'unknown', `a lost answer is not a result, ${at}`);
      assert.ok(!row.inbox.offers.includes('finish') && row.inbox.offers.includes('reconcile'), at);
      assert.deepEqual(row.secondFinish, { ok: false, error: REASON.unknownNoFinish }, at);
    }
    if (row.case.startsWith('after-')) {
      assert.equal(typeof row.interruption.forgejoAnswered, 'number', `the request reached Forgejo, ${at}`);
      assert.ok(merged, at);
      assert.deepEqual([row.firstReconcile.state, row.firstReconcile.text], ['confirmed', 'Merge confirmed'], at);
    }
    if (row.case.startsWith('before-')) {
      assert.equal(row.interruption.forgejoAnswered, undefined, `the request never reached Forgejo, ${at}`);
      assert.equal(row.forgejo.merged, false, at); assert.equal(row.forgejo.merges, 0, at);
      assert.equal(row.firstReconcile.state, 'unknown', `nothing found at once is not proof, ${at}`);
      assert.match(row.firstReconcile.detail, /may still be in progress/, at);
      assert.equal(row.secondReconcile.state, 'unapplied', at);
      assert.equal(row.afterwards.state, 'unapplied', `not applied stays so until a person approves again, ${at}`);
    }
  }
  report.passed = true;
} catch (error) {
  failed = true; report.passed = false; report.error = error.stack ?? String(error);
} finally {
  try { await context?.close(); } finally { rmSync(profile, { recursive: true, force: true }); }
}
// One line per item: the case, Forgejo's record and the inbox's final state, side by side.
for (const row of report.items) {
  const last = row.secondReconcile ?? row.firstReconcile ?? row.inbox;
  console.log(`ROW ${row.case} round=${row.round} pull=#${row.pull} forgejo.merged=${row.forgejo?.merged} forgejo.merges=${row.forgejo?.merges} inbox=${last?.state}` +
    (row.interruption ? ` held=${row.interruption.heldMs}ms forgejoAnswered=${row.interruption.forgejoAnswered ?? 'never'}` : ''));
}
console.log(JSON.stringify(report));
process.exit(failed ? 1 : 0);
