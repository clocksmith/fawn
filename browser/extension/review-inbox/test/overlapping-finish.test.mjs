// One approval is one click, however many Finish requests arrive for it and
// whatever happens to the extension while it is clicking.
//
// A local page shows a grid of buttons; each press is counted by the test's
// own server. Every button is picked and approved through the inbox page the
// way a person does it. Then:
//   1. Each item gets three Finish requests at the same moment, two from one
//      inbox page and one from a second (a side panel and a tab). These are
//      sent as messages, because overlap has to be forced, not hoped for.
//      Exactly one request may win and the server must count one press.
//   2. One more button keeps its page busy for 2.5 s while it is being
//      pressed. The extension's worker is stopped during that time. The card
//      must end as "Click may have been sent; result not confirmed", a later
//      Finish must be refused, and the server must count at most one press.
//   FAWN_EXTENSION_BROWSER (chrome binary); FAWN_OVERLAP_ITEMS (default 100)
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as wait } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';

const binary = process.env.FAWN_EXTENSION_BROWSER;
if (!binary) { console.log('skip: requires a Chrome binary'); process.exit(0); }
const extension = process.env.FAWN_EXTENSION_DIR ?? fileURLToPath(new URL('..', import.meta.url));
const count = Number(process.env.FAWN_OVERLAP_ITEMS ?? 100);
const sha256 = path => createHash('sha256').update(readFileSync(path)).digest('hex');

const presses = new Map();
const grid = `<!doctype html><title>Payments to release</title><style>body{margin:8px;font:13px sans-serif}
button{width:84px;height:26px;margin:4px}</style>${Array.from({ length: count }, (_, i) => `<button data-n="${i}">Pay ${i}</button>`).join('')}
<script>for (const b of document.querySelectorAll('button')) b.onclick = () => fetch('/press?item=' + b.dataset.n, { method: 'POST', keepalive: true });</script>`;
const slow = `<!doctype html><title>A slow payment</title><button style="margin:40px;width:160px;height:40px">Pay slowly</button>
<script>const b = document.querySelector('button');
b.onmousedown = () => { navigator.sendBeacon('/press?item=pressing'); for (const end = performance.now() + 2500; performance.now() < end;); };
b.onclick = () => fetch('/press?item=slow', { method: 'POST', keepalive: true });</script>`;
const server = createServer((request, response) => {
  const url = new URL(request.url, 'http://127.0.0.1');
  if (url.pathname === '/press') { const item = url.searchParams.get('item'); presses.set(item, (presses.get(item) ?? 0) + 1); response.end('ok'); return; }
  response.setHeader('content-type', 'text/html'); response.end(url.pathname === '/slow' ? slow : grid);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;

const profile = mkdtempSync(join(tmpdir(), 'fawn-inbox-overlap-'));
const report = { overlappingFinish: true, chromeSha256: sha256(binary), backgroundSha256: sha256(join(extension, 'background.js')), items: count };
let context;
try {
  context = await chromium.launchPersistentContext(profile, { executablePath: binary, headless: false, chromiumSandbox: true,
    viewport: null, ignoreDefaultArgs: ['--disable-extensions'],
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--disable-gpu-compositing', '--force-device-scale-factor=1',
      `--disable-extensions-except=${extension}`, `--load-extension=${extension}`, '--disable-features=DisableLoadExtensionCommandLineSwitch'] });
  report.browserVersion = context.browser()?.version() ?? null;
  const worker = context.serviceWorkers()[0] ?? await context.waitForEvent('serviceworker');
  const extensionId = new URL(worker.url()).host;
  const page = context.pages()[0] ?? await context.newPage();
  await page.goto(origin + '/grid');
  const slowPage = await context.newPage();
  await slowPage.goto(origin + '/slow');
  const inbox = await context.newPage(), second = await context.newPage();
  await inbox.goto(`chrome-extension://${extensionId}/inbox.html`);
  await second.goto(`chrome-extension://${extensionId}/inbox.html`);
  const card = label => inbox.locator('[data-item]', { has: inbox.locator('h2', { hasText: new RegExp(`^Click "${label}"$`) }) });

  async function pickAndApprove(target, button, label) {
    await inbox.bringToFront();
    await inbox.locator('[data-tabs]').selectOption({ label: await target.title() });
    await inbox.locator('[data-pick]').click();
    await target.getByText('Click the button you want approved').waitFor({ state: 'visible', timeout: 15000 });
    const box = await button.boundingBox();
    await target.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
    await inbox.bringToFront();
    await card(label).waitFor({ state: 'visible', timeout: 15000 });
    await card(label).locator('[data-action=approve]').click();
    await inbox.locator('[data-item][data-state=approved]', { has: inbox.locator('h2', { hasText: new RegExp(`^Click "${label}"$`) }) }).waitFor({ timeout: 15000 })
      .catch(async error => { throw new Error(`${label} was not approved: ${JSON.stringify(await inbox.locator('[data-notice]').textContent())} (${error.message.split('\n')[0]})`); });
    return card(label).getAttribute('data-item');
  }

  const ids = [];
  for (let i = 0; i < count; i++) ids.push(await pickAndApprove(page, page.locator(`button[data-n="${i}"]`), `Pay ${i}`));
  assert.equal(presses.size, 0, 'picking and approving must not press anything');

  // 1. Three overlapping Finish requests per item.
  const finish = (from, id, times) => from.evaluate(([item, n]) => Promise.all(Array.from({ length: n }, () =>
    chrome.runtime.sendMessage({ inbox: 'finish', id: item }).catch(error => ({ ok: false, error: String(error) })))), [id, times]);
  const overlap = { winners: [], pressesPerItem: [], refusals: new Set() };
  for (let i = 0; i < count; i++) {
    const replies = (await Promise.all([finish(inbox, ids[i], 2), finish(second, ids[i], 1)])).flat();
    overlap.winners.push(replies.filter(reply => reply.ok).length);
    for (const reply of replies) if (!reply.ok) overlap.refusals.add(reply.error);
  }
  await wait(1000);
  for (let i = 0; i < count; i++) overlap.pressesPerItem.push(presses.get(String(i)) ?? 0);
  report.overlap = { requestsPerItem: 3, itemsWithOneWinner: overlap.winners.filter(n => n === 1).length,
    itemsPressedOnce: overlap.pressesPerItem.filter(n => n === 1).length, itemsPressedMoreThanOnce: overlap.pressesPerItem.filter(n => n > 1).length,
    itemsNeverPressed: overlap.pressesPerItem.filter(n => n === 0).length, totalPresses: overlap.pressesPerItem.reduce((a, b) => a + b, 0),
    refusalTexts: [...overlap.refusals] };

  // 2. The worker stops in the middle of a click.
  const slowId = await pickAndApprove(slowPage, slowPage.locator('button'), 'Pay slowly');
  inbox.evaluate(id => { chrome.runtime.sendMessage({ inbox: 'finish', id }).catch(() => null); }, slowId).catch(() => null);
  // The page says when the press has reached it; it then stays busy handling it for 2.5 s.
  for (const end = Date.now() + 15000; !presses.has('pressing');) { assert(Date.now() < end, 'the press never reached the page'); await wait(10); }
  const control = await context.newCDPSession(inbox);
  await control.send('ServiceWorker.enable');
  await control.send('ServiceWorker.stopAllWorkers');
  report.stopped = { whileThePageWasHandlingThePress: true };
  await wait(3500); // the page finishes handling whatever it was sent
  await inbox.reload();
  const slowCard = inbox.locator(`[data-item="${slowId}"]`);
  await slowCard.locator('[data-outcome] strong').waitFor({ timeout: 15000 });
  report.stopped.card = await slowCard.evaluate(element => ({ state: element.dataset.state,
    text: element.querySelector('[data-outcome] strong')?.textContent ?? null, detail: element.querySelector('[data-outcome] span')?.textContent ?? null }));
  report.stopped.secondFinish = (await finish(inbox, slowId, 1))[0];
  await wait(1000);
  report.stopped.presses = presses.get('slow') ?? 0;

  assert.equal(report.overlap.itemsWithOneWinner, count, 'exactly one Finish request may win each item');
  assert.equal(report.overlap.itemsPressedOnce, count, 'every approved button is pressed exactly once');
  assert.equal(report.stopped.card.state, 'unconfirmed');
  assert.match(report.stopped.card.text, /^Click may have been sent; result not confirmed$/);
  // Only a worker other than the one that started the click writes this, so it also shows the worker really stopped.
  assert.match(report.stopped.card.detail, /stopped while this click was in progress/);
  assert.equal(report.stopped.secondFinish.ok, false, 'a spent approval cannot be finished again');
  assert.ok(report.stopped.presses <= 1, 'a click interrupted by a stopped worker is never sent twice');
  report.passed = true;
} catch (error) {
  report.passed = false; report.error = error.stack ?? String(error);
} finally {
  await context?.close().catch(() => null);
  server.close();
  rmSync(profile, { recursive: true, force: true });
}
console.log(JSON.stringify(report, null, 2));
process.exit(report.passed ? 0 : 1);
