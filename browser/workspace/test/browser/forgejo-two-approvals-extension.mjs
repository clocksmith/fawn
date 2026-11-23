// The two-approvals demo (forgejo-two-approvals.test.js) with an ordinary Chrome
// extension in place of the browser-owned review: the fenced reviewed click from
// browser/chromium/tests/extension-control, the strongest an MV3 extension can
// offer. Same application, same browser binary, same order of events: two
// pull requests are reviewed, the first one's branch is changed, the first is
// finished and then the second.
//
// The extension reviews by screenshotting the rectangle through the debugger
// API and binding the element under the click point; it clicks after comparing
// a fresh screenshot and fencing the click to that element. Reading the result
// from the page is done here with the same conditions and the same rule that
// only what is new counts. No inbox is involved: "approval" is the stored
// review, and both reviews wait in one browser profile, each in its own tab.
//   FAWN_EXTENSION_BROWSER (chrome binary), FAWN_EXTENSION_DIR (unpacked extension),
//   FORGEJO_ORIGIN, FORGEJO_PASSWORD_FILE (Ben's), FORGEJO_TOKEN_FILE (Ana's API token)
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { setTimeout as wait } from 'node:timers/promises';
import { chromium } from 'playwright-core';

const binary = process.env.FAWN_EXTENSION_BROWSER, extensionDir = process.env.FAWN_EXTENSION_DIR, origin = process.env.FORGEJO_ORIGIN;
if (!binary || !extensionDir || !origin || !process.env.FORGEJO_PASSWORD_FILE || !process.env.FORGEJO_TOKEN_FILE) {
  console.log('skip: requires a Chrome binary, the unpacked control extension and a local Forgejo with the two test accounts');
  process.exit(0);
}
const extension = resolve(extensionDir);
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
const pullState = async number => { const p = await api('GET', '/pulls/' + number); return { merged: p.merged, state: p.state, head: p.head.sha, mergeable: p.mergeable ?? null }; };
// Forgejo examines every open pull request again after activity on the base branch's pull requests, and
// refuses to merge one it has not finished examining. The unchanged payment is finished once Forgejo says
// it can be merged, as a person would wait for the page to say so.
async function readyToMerge(number) {
  const start = Date.now();
  while (!(await pullState(number)).mergeable) { assert(Date.now() - start < 30000, 'Forgejo never reported the pull request mergeable'); await wait(250); }
  return Date.now() - start;
}
const commitsOnMain = async path => {
  const response = await fetch(`${origin}/api/v1/repos/${repo}/commits?sha=main&path=${encodeURIComponent(path)}&limit=10`,
    { headers: { authorization: 'token ' + token } });
  if (response.status === 404) return 0;
  assert(response.ok, `commits for ${path}: ${response.status}`);
  return (await response.json()).length;
};
const sha256 = path => createHash('sha256').update(readFileSync(path)).digest('hex');

const DONE = '.issue-state-label.purple', REFUSED = '.flash-error, .toastify.on';
const RESULT_WINDOW_MS = 10000, POLL_MS = 100;
const profile = mkdtempSync(join(tmpdir(), 'forgejo-extension-'));
const report = { forgejoTwoApprovalsExtension: true, implementation: 'extension-fenced', chromeSha256: sha256(binary),
  extensionSha256: sha256(join(extension, 'background.js')), items: [] };
let context;
try {
  context = await chromium.launchPersistentContext(profile, { executablePath: binary, headless: false, chromiumSandbox: true,
    viewport: null, ignoreDefaultArgs: ['--disable-extensions'],
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--disable-accelerated-2d-canvas', '--disable-gpu-compositing', '--force-device-scale-factor=1',
      `--disable-extensions-except=${extension}`, `--load-extension=${extension}`, '--disable-features=DisableLoadExtensionCommandLineSwitch'] });
  report.browserVersion = context.browser()?.version() ?? null;
  const worker = context.serviceWorkers()[0] ?? await context.waitForEvent('serviceworker');
  const tabOf = page => worker.evaluate(async url => (await chrome.tabs.query({})).find(tab => tab.url === url)?.id, page.url());
  // Ben signs in to Forgejo. This stands in for him typing.
  const first = context.pages()[0] ?? await context.newPage();
  await first.goto(origin + '/user/login');
  await first.fill('#user_name', 'ben'); await first.fill('#password', password);
  await Promise.all([first.waitForNavigation(), first.evaluate(() => document.querySelector('#user_name').form.submit())]);
  assert.notEqual(new URL(first.url()).pathname, '/user/login', 'Ben must be signed in');
  const second = await context.newPage();

  // The scripted stand-in for the agent opens a pull request in a tab, opens its
  // merge form and has the extension review the confirm button.
  async function prepare(page, pull) {
    await page.goto(`${origin}/${repo}/pulls/${pull.number}`);
    await page.bringToFront();
    const tabId = await tabOf(page);
    // Attaching the debugger shows its bar, which moves the page down; attach before measuring.
    await worker.evaluate(id => self.prepare(id), tabId);
    await wait(1500);
    await page.evaluate(() => [...document.querySelectorAll('.merge-section button')].find(b => /Create merge commit/.test(b.textContent)).click());
    await wait(600);
    const box = await page.evaluate(number => {
      const button = [...document.querySelectorAll('button')].find(b => b.form?.action.endsWith(`/pulls/${number}/merge`) && b.type === 'submit'
        && /^\s*Create merge commit\s*$/.test(b.textContent));
      button.scrollIntoView({ block: 'center' });
      const r = button.getBoundingClientRect();
      return { x: r.x, y: r.y, width: r.width, height: r.height, head: document.querySelector('input[name=head_commit_id]').value };
    }, pull.number);
    await page.evaluate(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(() => r(true)))));
    const { head, ...r } = box;
    // Whole pixels that enclose the button, as the browser-owned review uses.
    const rect = { x: Math.floor(r.x), y: Math.floor(r.y), width: Math.ceil(r.x + r.width) - Math.floor(r.x), height: Math.ceil(r.y + r.height) - Math.floor(r.y) };
    const point = { x: rect.x + Math.floor(rect.width / 2), y: rect.y + Math.floor(rect.height / 2) };
    const review = await worker.evaluate(([id, r, p]) => self.review(id, r, p), [tabId, rect, point]);
    return { page, pull, tabId, rect, review, formHead: head, imageDigest: createHash('sha256').update(review.image).digest('hex') };
  }
  const state = page => page.evaluate(([done, refused]) => ({ document: String(performance.timeOrigin),
    done: document.querySelectorAll(done).length, refused: document.querySelectorAll(refused).length,
    said: [...document.querySelectorAll(refused)].filter(e => e.getClientRects().length).map(e => (e.classList.contains('toastify') ? 'message' : 'banner') + ': '
      + e.textContent.trim().replace(/\s+/g, ' ').slice(0, 120)).join(' | ') }), [DONE, REFUSED]).catch(() => null);
  // Click through the extension, then read what the application did. Only what is new counts.
  async function finish(item) {
    await item.page.bringToFront();
    const before = await state(item.page), start = performance.now();
    const click = await worker.evaluate(([id]) => self.click(id, 'fenced'), [item.review.id]);
    if (!click.delivered) return { delivered: false, outcome: 'failed', outcomeBasis: click.reason, finishMs: Math.round(performance.now() - start) };
    for (const deadline = Date.now() + RESULT_WINDOW_MS; ;) {
      const now = await state(item.page); // null while the page is navigating
      if (now) {
        const fresh = key => now[key] > 0 && (before[key] === 0 || now.document !== before.document || now[key] > before[key]);
        if (fresh('refused')) return { delivered: true, outcome: 'failed', outcomeBasis: 'failure-condition', said: now.said, finishMs: Math.round(performance.now() - start) };
        if (fresh('done')) return { delivered: true, outcome: 'completed', outcomeBasis: 'completion-condition', finishMs: Math.round(performance.now() - start) };
      }
      if (Date.now() >= deadline) return { delivered: true, outcome: 'unknown', outcomeBasis: 'completion-unestablished', finishMs: Math.round(performance.now() - start) };
      await wait(POLL_MS);
    }
  }

  const pullA = await openPull('Acme Tools', '1280.00'), pullB = await openPull('Birch Supply', '640.00');
  const itemA = await prepare(first, pullA), itemB = await prepare(second, pullB);
  // After both are reviewed, the first payment's amount is changed on its branch.
  const file = await api('GET', `/contents/${pullA.path}?ref=${pullA.id}`);
  await api('PUT', `/contents/${pullA.path}`, { content: csv('Acme Tools', '9280.00'), message: 'Revise the amount', branch: pullA.id, sha: file.sha });
  assert.notEqual((await pullState(pullA.number)).head.slice(0, 10), itemA.formHead.slice(0, 10), 'the first branch head moved');

  const doneA = await finish(itemA);
  await wait(1000);
  const afterA = await pullState(pullA.number), untouchedB = await pullState(pullB.number);
  report.waitedForForgejoMs = await readyToMerge(pullB.number);
  const doneB = await finish(itemB);
  await wait(1500);
  const afterB = await pullState(pullB.number);
  // A second click on either review: the extension spent them.
  const again = [];
  for (const item of [itemA, itemB]) again.push((await worker.evaluate(([id]) => self.click(id, 'fenced'), [item.review.id])).reason);
  report.items = [
    { item: 'A: changed after approval', pull: pullA.number, ...doneA, merged: afterA.merged, pullState: afterA.state, commitsOnMain: await commitsOnMain(pullA.path) },
    { item: 'B: unchanged', pull: pullB.number, ...doneB, merged: afterB.merged, pullState: afterB.state, mergedBeforeItsOwnFinish: untouchedB.merged,
      commitsOnMain: await commitsOnMain(pullB.path) },
  ];
  report.secondClick = again;
  report.bothReviewsWaitedInOneBrowser = true;
  process.stderr.write('ROWS ' + JSON.stringify(report) + '\n');

  assert.equal(doneA.delivered, true, JSON.stringify(doneA));
  assert.equal(afterA.merged, false, 'the changed payment must not merge');
  assert.equal(doneA.outcome, 'failed', JSON.stringify(doneA));
  assert.equal(untouchedB.merged, false, 'finishing A did not merge B');
  assert.equal(doneB.delivered, true, JSON.stringify(doneB));
  assert.equal(afterB.merged, true, 'the unchanged payment merged');
  assert.equal(doneB.outcome, 'completed', JSON.stringify(doneB));
  assert.equal(report.items[1].commitsOnMain, 1, 'the unchanged payment is on main exactly once');
  assert.equal(report.items[0].commitsOnMain, 0, 'the changed payment never reached main');
  assert.ok(again.every(reason => reason === 'unknown-review'), 'neither review can be used twice: ' + JSON.stringify(again));
  console.log(JSON.stringify(report));
} finally {
  try { await context?.close(); } finally { rmSync(profile, { recursive: true, force: true }); }
}
