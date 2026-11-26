// The review inbox extension on a real application, driven through its own
// page the way a person would use it: pick a button, approve it, finish it.
// Nothing here calls into the extension; it clicks what a person clicks.
//
// Three supplier payments are three pull requests in a local Forgejo, each in
// its own tab of one browser profile. All three are picked and approved. Then:
//   B is finished first and merges                     -> "Merge confirmed"
//   A's branch was changed after approval; Forgejo
//     refuses the merge of the page Ben still has      -> "App refused"
//   C's tab was reloaded after approval; the inbox
//     closes it at once, with no Finish left to press  -> "Click blocked"
// Each card must show its own result, B must be on main exactly once, and
// neither A nor C may reach main.
//   FAWN_EXTENSION_BROWSER (chrome binary), FORGEJO_ORIGIN, FORGEJO_PASSWORD_FILE (Ben's),
//   FORGEJO_TOKEN_FILE (Ana's API token); FAWN_INBOX_SCREENSHOT (optional PNG path)
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
const extension = process.env.FAWN_EXTENSION_DIR ?? fileURLToPath(new URL('..', import.meta.url));
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
const commitsOnMain = async path => {
  const response = await fetch(`${origin}/api/v1/repos/${repo}/commits?sha=main&path=${encodeURIComponent(path)}&limit=10`,
    { headers: { authorization: 'token ' + token } });
  if (response.status === 404) return 0;
  assert(response.ok, `commits for ${path}: ${response.status}`);
  return (await response.json()).length;
};
// Forgejo refuses to merge a pull request it has not finished examining; a person waits for the page to say it can merge.
async function readyToMerge(number) {
  const start = Date.now();
  while (!(await pullState(number)).mergeable) { assert(Date.now() - start < 30000, 'Forgejo never reported the pull request mergeable'); await wait(250); }
}
const sha256 = path => createHash('sha256').update(readFileSync(path)).digest('hex');

const profile = mkdtempSync(join(tmpdir(), 'fawn-inbox-extension-'));
const report = { reviewInboxExtension: true, chromeSha256: sha256(binary), backgroundSha256: sha256(join(extension, 'background.js')), items: [] };
let context;
try {
  context = await chromium.launchPersistentContext(profile, { executablePath: binary, headless: false, chromiumSandbox: true,
    viewport: null, ignoreDefaultArgs: ['--disable-extensions'],
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--disable-gpu-compositing', '--force-device-scale-factor=1',
      `--disable-extensions-except=${extension}`, `--load-extension=${extension}`, '--disable-features=DisableLoadExtensionCommandLineSwitch'] });
  report.browserVersion = context.browser()?.version() ?? null;
  const worker = context.serviceWorkers()[0] ?? await context.waitForEvent('serviceworker');
  const extensionId = new URL(worker.url()).host;

  // Ben signs in and opens each pull request's merge form in its own tab.
  const first = context.pages()[0] ?? await context.newPage();
  await first.goto(origin + '/user/login');
  await first.fill('#user_name', 'ben'); await first.fill('#password', password);
  await Promise.all([first.waitForNavigation(), first.evaluate(() => document.querySelector('#user_name').form.submit())]);
  assert.notEqual(new URL(first.url()).pathname, '/user/login', 'Ben must be signed in');
  const pulls = { A: await openPull('Acme Tools', '1280.00'), B: await openPull('Birch Supply', '640.00'), C: await openPull('Cedar Freight', '310.00') };
  const tabs = { A: first, B: await context.newPage(), C: await context.newPage() };
  const confirm = (page, pull) => page.locator(`form[action$="/pulls/${pull.number}/merge"] button[type=submit]`, { hasText: /^\s*Create merge commit\s*$/ });
  for (const key of ['A', 'B', 'C']) {
    await tabs[key].goto(`${origin}/${repo}/pulls/${pulls[key].number}`);
    await tabs[key].locator('.merge-section button', { hasText: 'Create merge commit' }).first().click();
    await confirm(tabs[key], pulls[key]).waitFor({ state: 'visible' });
  }
  // The inbox, opened as a page. In Chrome it is the side panel; it is the same page.
  const inbox = await context.newPage();
  await inbox.goto(`chrome-extension://${extensionId}/inbox.html`);
  const cardFor = pull => inbox.locator('[data-item]', { hasText: `Merge pull request #${pull.number} ` });
  const outcomeOf = pull => cardFor(pull).evaluate(card => ({ state: card.dataset.state,
    text: card.querySelector('[data-outcome] strong')?.textContent ?? null, detail: card.querySelector('[data-outcome] span')?.textContent ?? null }));

  // Pick: choose the page in the inbox, press Pick, then click the button on that page. The click chooses
  // the button and must not press it.
  for (const key of ['A', 'B', 'C']) {
    const page = tabs[key], pull = pulls[key];
    await inbox.bringToFront();
    await inbox.locator('[data-tabs]').selectOption({ label: await page.title() });
    await inbox.locator('[data-pick]').click();
    await page.getByText('Click the button you want approved').waitFor({ state: 'visible', timeout: 15000 });
    // A click beside the button chooses nothing and the picker stays open.
    await page.mouse.click(5, 5);
    assert.equal(await inbox.locator('[data-item]', { hasText: `#${pull.number} ` }).count(), 0, 'a click on the page background is not a target');
    await confirm(page, pull).scrollIntoViewIfNeeded();
    const box = await confirm(page, pull).boundingBox();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
    await inbox.bringToFront();
    await cardFor(pull).waitFor({ state: 'visible', timeout: 15000 }).catch(async error => {
      throw new Error(`no card for #${pull.number}: notice=${JSON.stringify(await inbox.locator('[data-notice]').textContent())} cards=${JSON.stringify(await inbox.locator('[data-item] h2').allTextContents())} (${error.message.split('\n')[0]})`);
    });
    assert.equal((await pullState(pull.number)).merged, false, 'picking a button must not press it');
    assert.equal(await cardFor(pull).getAttribute('data-state'), 'pending');
    // The card shows the button that was picked, not an empty rectangle.
    const colours = await cardFor(pull).locator('.seen img').first().evaluate(async image => {
      await image.decode();
      const canvas = new OffscreenCanvas(image.naturalWidth, image.naturalHeight), ink = canvas.getContext('2d');
      ink.drawImage(image, 0, 0);
      const data = ink.getImageData(0, 0, canvas.width, canvas.height).data, seen = new Set();
      for (let i = 0; i < data.length; i += 4) seen.add(data[i] << 16 | data[i + 1] << 8 | data[i + 2]);
      return { distinct: seen.size, width: canvas.width, height: canvas.height };
    });
    assert.ok(colours.distinct > 8 && colours.width > 40, `the reviewed image must show the button: ${JSON.stringify(colours)}`);
  }
  // Scrolling a page after picking is not a change to what was reviewed.
  await tabs.A.evaluate(() => window.scrollTo(0, 0));
  // Approve each, switching tabs in between as a person looking at each page would.
  for (const key of ['A', 'B', 'C']) {
    await tabs[key].bringToFront(); await wait(200);
    await inbox.bringToFront();
    await cardFor(pulls[key]).locator('[data-action=approve]').click();
    await inbox.locator(`[data-item][data-state=approved]`, { hasText: `#${pulls[key].number} ` }).waitFor({ timeout: 15000 }).catch(async error => {
      throw new Error(`#${pulls[key].number} was not approved: notice=${JSON.stringify(await inbox.locator('[data-notice]').textContent())} (${error.message.split('\n')[0]})`);
    });
  }
  await inbox.screenshot({ path: process.env.FAWN_INBOX_SCREENSHOT ? process.env.FAWN_INBOX_SCREENSHOT.replace(/\.png$/, '-approved.png') : join(profile, 'approved.png'), fullPage: true });

  // After approval: A's amount is changed on its branch, and C's tab is reloaded.
  const file = await api('GET', `/contents/${pulls.A.path}?ref=${pulls.A.id}`);
  await api('PUT', `/contents/${pulls.A.path}`, { content: csv('Acme Tools', '9280.00'), message: 'Revise the amount', branch: pulls.A.id, sha: file.sha });
  await tabs.C.reload();

  // Finish out of order, from the inbox, without going back to any of the three tabs.
  // C was closed when its tab reloaded: its card already has its result and offers no Finish to press.
  const finish = async (key, press = true) => {
    const start = Date.now();
    if (press) await cardFor(pulls[key]).locator('[data-action=finish]').click();
    const clickReturnedMs = Date.now() - start;
    await cardFor(pulls[key]).locator('[data-outcome] strong').waitFor({ timeout: 30000 });
    // When the inbox recorded the result, by its own clock.
    const recordedMs = await inbox.evaluate(async number => { const item = Object.values((await chrome.storage.session.get('items')).items)
      .find(entry => entry.summary.title.includes('#' + number + ' ')); return Date.parse(item.outcome.at); }, pulls[key].number) - start;
    return { ...(await outcomeOf(pulls[key])), ms: Date.now() - start, clickReturnedMs, recordedMs };
  };
  await inbox.bringToFront();
  // Count how often the inbox loses the front while items are finished.
  await inbox.evaluate(() => { globalThis.__hiddenCount = 0; document.addEventListener('visibilitychange', () => { if (document.hidden) globalThis.__hiddenCount++; }); });
  await readyToMerge(pulls.B.number);
  const doneB = await finish('B');
  const doneA = await finish('A');
  const doneC = await finish('C', false);
  await wait(1000);
  const visible = await inbox.evaluate(() => document.visibilityState);
  report.tabSwitchesDuringFinish = await inbox.evaluate(() => globalThis.__hiddenCount ?? 0);
  for (const [key, done] of [['A', doneA], ['B', doneB], ['C', doneC]]) {
    const after = await pullState(pulls[key].number);
    report.items.push({ item: key, pull: pulls[key].number, ...done, merged: after.merged, commitsOnMain: await commitsOnMain(pulls[key].path),
      finishOffered: await cardFor(pulls[key]).locator('[data-action=finish]').count() });
  }
  report.inboxStayedInFront = visible === 'visible';
  if (process.env.FAWN_INBOX_SCREENSHOT) await inbox.screenshot({ path: process.env.FAWN_INBOX_SCREENSHOT, fullPage: true });
  process.stderr.write('ROWS ' + JSON.stringify(report) + '\n');

  const [a, b, c] = report.items;
  assert.equal(b.text, 'Merge confirmed', JSON.stringify(b)); assert.equal(b.merged, true); assert.equal(b.commitsOnMain, 1, 'B is on main exactly once');
  assert.equal(a.text, 'App refused', JSON.stringify(a)); assert.equal(a.merged, false); assert.equal(a.commitsOnMain, 0);
  assert.match(a.detail ?? '', /head was updated|not ready to be merged/, 'the card carries what Forgejo said');
  assert.equal(c.text, 'Click blocked', JSON.stringify(c)); assert.equal(c.merged, false); assert.equal(c.commitsOnMain, 0);
  assert.match(c.detail ?? '', /reloaded or navigated/);
  assert.ok(report.items.every(item => item.finishOffered === 0), 'a finished item is never offered again');
  console.log(JSON.stringify(report));
} finally {
  try { await context?.close(); } finally { rmSync(profile, { recursive: true, force: true }); }
}
