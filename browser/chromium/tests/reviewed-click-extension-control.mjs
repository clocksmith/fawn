// Can an ordinary extension give the reviewed-click guarantee? This runs one
// adversarial page against three implementations in the same Chromium binary:
//   fawn               the browser-owned review (Input.captureVisualEvidence)
//   extension-trusted  ./extension-control with a trusted chrome.debugger click
//   extension-fenced   the same trusted click behind a content-script fence that
//                      stops it unless it lands on the bound element
//   extension-atomic   ./extension-control with an in-page check-and-click
// After each review the page changes at a random moment: either the reviewed
// pixels (the amount shown) or the element under the click point (an invisible
// look-alike). A delivery is unsafe when the look-alike received it, or when it
// arrived more than two frames after the reviewed pixels changed, so the
// changed frame was on screen before the click was sent. The same trials
// without any change measure unnecessary refusals and latency.
// The flicker attack keeps alternating the amount every two frames once it
// starts. A delivery is unsafe there when the page was showing the changed
// amount as the click arrived, more than two frames after the first change.
// Deliveries that arrive while the reviewed amount is showing again are counted
// apart: the pixels had changed since review and changed back.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { once } from 'node:events';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { setTimeout as wait } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { chromium } from 'playwright-core';
import { reviewedClicks } from '../scripts/reviewed-click.mjs';

const { values } = parseArgs({ options: {
  browser: { type: 'string' }, out: { type: 'string' }, arm: { type: 'string' },
  trials: { type: 'string', default: '150' }, 'window-ms': { type: 'string', default: '150' },
  attacks: { type: 'string', default: 'none,pixels,target' },
  'forced-trials': { type: 'string', default: '0' },
} });
const attacks = values.attacks.split(',').filter(Boolean);
assert(attacks.every(a => ['none', 'pixels', 'target', 'flicker'].includes(a)), 'attacks are none, pixels, target, flicker');
assert(values.browser && values.out && ['fawn', 'extension-trusted', 'extension-fenced', 'extension-atomic'].includes(values.arm),
  'usage: --browser CHROME --out NEW.json --arm fawn|extension-trusted|extension-fenced|extension-atomic [--trials N]');
const forcedTrials = Number(values['forced-trials']);
const trials = Number(values.trials), windowMs = Number(values['window-ms']);
const binary = resolve(values.browser);
const extension = fileURLToPath(new URL('./extension-control/', import.meta.url));
const rect = { x: 16, y: 72, width: 320, height: 64 };
const point = { x: 176, y: 104 };
// A second approve control: an ordinary button inside a closed shadow root.
const shadowRect = { x: 16, y: 160, width: 320, height: 64 };
const shadowPoint = { x: 176, y: 192 };
const TWO_FRAMES_MS = 34;

const html = `<!doctype html><title>Adversarial approvals</title>
<style>body{margin:0;font:14px sans-serif}@keyframes spin{to{transform:rotate(360deg)}}
#approve{position:absolute;left:16px;top:72px;display:block}
#busy{position:absolute;left:420px;top:8px;width:40px;height:40px;background:#c2410c;animation:spin 1.2s linear infinite}</style>
<div id="busy"></div><canvas id="approve" width="320" height="64"></canvas>
<div id="widget" style="position:absolute;left:16px;top:160px;width:320px;height:64px"></div>
<script>
const canvas = document.getElementById('approve'), ink = canvas.getContext('2d');
let shown;
const draw = amount => { shown = amount; ink.fillStyle = '#fff'; ink.fillRect(0, 0, 320, 64); ink.strokeRect(1, 1, 318, 62);
  ink.fillStyle = '#1d1d1b'; ink.font = 'bold 15px sans-serif'; ink.fillText('Approve payment', 12, 26);
  ink.font = '14px sans-serif'; ink.fillText('Acme Tools · ' + amount + ' · INV-1042', 12, 48); };
window.trial = null;
// The widget keeps its shadow root closed, as a component library would. Only
// this script can reach inside; the document and extensions see the host.
const widget = document.getElementById('widget'), inside = widget.attachShadow({ mode: 'closed' });
inside.innerHTML = '<button id="inner-approve" type="button" style="position:absolute;left:0;top:0;width:320px;height:64px;margin:0;'
  + 'border:2px solid #1d1d1b;background:#fff;font:14px sans-serif;text-align:left">Approve payment · Acme Tools · $1,280.00</button>';
inside.getElementById('inner-approve').onclick = event => window.trial?.commits.push({ record: 'INV-1042', amount: '$1,280.00', trusted: event.isTrusted, at: performance.now() });
inside.addEventListener('click', event => window.trial?.clicks.push({ at: performance.now(), target: event.target.id, trusted: event.isTrusted, shown: '$1,280.00' }), true);
// The application: a click on the approve control commits the amount it shows.
canvas.onclick = event => window.trial?.commits.push({ record: 'INV-1042', amount: shown, trusted: event.isTrusted, at: performance.now() });
window.begin = () => { inside.getElementById('lookalike')?.remove(); if (window.trial) window.trial.stopped = true;
  document.getElementById('lookalike')?.remove(); draw('$1,280.00'); window.trial = { changedAt: null, clicks: [], commits: [] };
  return new Promise(r => requestAnimationFrame(() => requestAnimationFrame(() => r(true)))); };
// The page changes at a moment of its own choosing after the review.
window.arm = (kind, delay) => { const mine = window.trial; setTimeout(() => {
  if (mine.stopped) return;
  if (kind === 'flicker') { let n = 0; const step = () => { if (mine.stopped) return;
      if (n++ % 2 === 0) draw(shown === '$1,280.00' ? '$9,280.00' : '$1,280.00'); requestAnimationFrame(step); };
    draw('$9,280.00'); requestAnimationFrame(step); }
  else if (kind === 'pixels') draw('$9,280.00');
  else { const d = document.createElement('div'); d.id = 'lookalike';
    d.style = 'position:absolute;left:16px;top:72px;width:320px;height:64px'; document.body.append(d); }
  mine.changedAt = performance.now(); }, delay); return true; };
// Forced orderings: the harness applies one change while an implementation is
// paused, and continues only after the change has been on screen.
const afterFrames = n => new Promise(resolve => { const step = () => (n-- > 0 ? requestAnimationFrame(step) : resolve(true)); step(); });
const lookalike = () => { const d = document.createElement('div'); d.id = 'lookalike';
  d.style = 'position:absolute;left:16px;top:72px;width:320px;height:64px';
  d.onclick = event => window.trial?.commits.push({ record: 'lookalike', amount: null, trusted: event.isTrusted, at: performance.now() });
  document.body.append(d); return d; };
window.change = async kind => {
  if (kind === 'pixels') draw('$9,280.00');
  if (kind === 'transient-pixels') { draw('$9,280.00'); await afterFrames(1); draw('$1,280.00'); }
  if (kind === 'target') lookalike();
  if (kind === 'shadow-target') { const d = document.createElement('div'); d.id = 'lookalike';
    d.style = 'position:absolute;left:0;top:0;width:320px;height:64px';
    d.onclick = event => window.trial?.commits.push({ record: 'lookalike', amount: null, trusted: event.isTrusted, at: performance.now() });
    inside.append(d); }
  if (kind === 'transient-target') { const d = lookalike(); await afterFrames(1); d.remove(); }
  window.trial.changedAt = performance.now();
  return afterFrames(3);
};
// Clicks inside the widget are logged by the listener in its shadow root, with
// the real recipient; here they would all appear as the host.
document.addEventListener('click', event => { if (event.target !== widget) window.trial?.clicks.push({ at: performance.now(),
  target: event.target.id || event.target.nodeName, trusted: event.isTrusted, shown }); }, true);
</script>`;
const server = createServer((req, res) => { res.setHeader('content-type', 'text/html'); res.end(html); });
server.listen(0, '127.0.0.1');
await once(server, 'listening');
const origin = `http://127.0.0.1:${server.address().port}`;

const quantile = (xs, q) => { const s = [...xs].sort((a, b) => a - b); return s.length ? s[Math.min(s.length - 1, Math.ceil(q * s.length) - 1)] : null; };
const sha256 = async path => createHash('sha256').update(await readFile(path)).digest('hex');
const base = ['--use-gl=angle', '--use-angle=swiftshader', '--disable-accelerated-2d-canvas', '--disable-gpu-compositing', '--force-device-scale-factor=1'];
const report = { schema: 'fawn.reviewed-click-extension-control/v1', arm: values.arm, chromeSha256: await sha256(binary),
  harnessSha256: await sha256(new URL(import.meta.url)), extensionSha256: await sha256(join(extension, 'background.js')),
  trialsPerAttack: trials, changeWindowMs: windowMs, twoFramesMs: TWO_FRAMES_MS, attacks: {} };
let closeBrowser = async () => {}, profile, barrierDir;
try {
  let page, implementation;
  if (values.arm === 'fawn') {
    barrierDir = await mkdtemp(join(tmpdir(), 'fawn-control-barriers-'));
    const browser = await chromium.launch({ executablePath: binary, headless: true, chromiumSandbox: true,
      args: [...base, '--ozone-platform=headless', `--fawn-reviewed-input-barriers=${barrierDir}`] });
    closeBrowser = () => browser.close();
    report.browserVersion = browser.version();
    const context = await browser.newContext({ viewport: null });
    page = await context.newPage();
    await page.goto(origin);
    const client = reviewedClicks(await context.newCDPSession(page));
    // The pause after Fawn's final pixel check is its delivery barrier: after
    // admission and Viz's verdict, before the page receives the click.
    const file = name => join(barrierDir, name);
    const exists = name => readFile(file(name)).then(() => true, () => false);
    const until = async name => { for (const end = Date.now() + 10000; Date.now() < end; await wait(2)) if (await exists(name)) return true; throw Error('timed out: ' + name); };
    implementation = { review: (r = rect, p = point) => client.capture(r, p), click: review => client.click(review),
      status: review => client.status(review),
      async pauseArm() { for (const name of ['delivery.ready', 'delivery.release', 'delivery.continued', 'hold-delivery']) await rm(file(name), { force: true });
        await writeFile(file('hold-delivery'), ''); },
      pauseReached: () => until('delivery.ready'),
      async pauseRelease() { await writeFile(file('delivery.release'), ''); await until('delivery.continued'); },
      async pauseClear() { await rm(file('hold-delivery'), { force: true }); } };
  } else {
    profile = await mkdtemp(join(tmpdir(), 'extension-control-'));
    const context = await chromium.launchPersistentContext(profile, { executablePath: binary, headless: false, chromiumSandbox: true,
      viewport: null, ignoreDefaultArgs: ['--disable-extensions'],
      args: [...base, `--disable-extensions-except=${extension}`, `--load-extension=${extension}`,
        '--disable-features=DisableLoadExtensionCommandLineSwitch'] });
    closeBrowser = () => context.close();
    report.browserVersion = context.browser()?.version() ?? null;
    const worker = context.serviceWorkers()[0] ?? await context.waitForEvent('serviceworker');
    page = context.pages()[0] ?? await context.newPage();
    await page.goto(origin);
    await page.bringToFront();
    const tabId = await worker.evaluate(async url => (await chrome.tabs.query({})).find(tab => tab.url.startsWith(url)).id, origin);
    // Attaching shows the debugging bar, which resizes the page; let it settle.
    await worker.evaluate(id => self.prepare(id), tabId);
    await wait(1500);
    const mode = values.arm.slice('extension-'.length);
    implementation = { review: (r = rect, p = point) => worker.evaluate(([id, r, p]) => self.review(id, r, p), [tabId, r, p]),
      // What the extension bound at the point: the element its content world sees.
      bound: p => worker.evaluate(([id, x, y]) => chrome.scripting.executeScript({ target: { tabId: id },
        func: (x, y) => { const e = document.elementFromPoint(x, y); return e && `${e.nodeName.toLowerCase()}#${e.id}`; }, args: [x, y] })
        .then(([r]) => r.result), [tabId, p.x, p.y]),
      click: review => worker.evaluate(([id, m]) => self.click(id, m), [review.id, mode]),
      pauseArm: () => worker.evaluate(() => self.armPause()),
      pauseReached: () => worker.evaluate(() => self.pauseReached()),
      pauseRelease: () => worker.evaluate(() => self.pauseRelease()),
      pauseClear: () => worker.evaluate(() => { self.__pause = null; }) };
  }

  for (const attack of attacks) {
    const tally = { trials, delivered: 0, refused: {}, unsafe: 0, deliveredToLookalike: 0, deliveredAfterChangedFrame: 0,
      deliveredAfterTransientChange: 0,
      deliveredWithinTwoFramesOfChange: 0, deliveredBeforeChange: 0, untrustedDeliveries: 0, clickMs: [] };
    for (let i = 0; i < trials; i++) {
      await page.evaluate(() => window.begin());
      let review;
      try { review = await implementation.review(); }
      catch (error) { const key = 'capture-refused'; tally.refused[key] = (tally.refused[key] ?? 0) + 1; continue; }
      if (attack !== 'none') await page.evaluate(([kind, delay]) => window.arm(kind, delay), [attack, Math.random() * windowMs]);
      const start = performance.now();
      const outcome = await implementation.click(review);
      const elapsed = performance.now() - start;
      await wait(windowMs + 100);
      const trial = await page.evaluate(() => window.trial);
      const click = trial.clicks[0];
      if (!click) {
        const key = outcome.reason ?? 'no-delivery';
        tally.refused[key] = (tally.refused[key] ?? 0) + 1;
        continue;
      }
      tally.delivered++;
      tally.clickMs.push(elapsed);
      if (!click.trusted) tally.untrustedDeliveries++;
      if (click.target === 'lookalike') { tally.deliveredToLookalike++; tally.unsafe++; continue; }
      if (trial.changedAt === null || click.at < trial.changedAt) { tally.deliveredBeforeChange++; continue; }
      const late = click.at - trial.changedAt > TWO_FRAMES_MS;
      if (attack === 'flicker' && late && click.shown === '$1,280.00') tally.deliveredAfterTransientChange++;
      else if ((attack === 'pixels' || attack === 'flicker') && late) { tally.deliveredAfterChangedFrame++; tally.unsafe++; }
      else tally.deliveredWithinTwoFramesOfChange++;
    }
    const times = tally.clickMs;
    tally.clickMs = { n: times.length, p50: quantile(times, 0.5), p99: quantile(times, 0.99) };
    if (attack === 'none') tally.unnecessaryRefusalRate = (trials - tally.delivered) / trials;
    report.attacks[attack] = tally;
    console.log(JSON.stringify({ arm: values.arm, attack, delivered: tally.delivered, unsafe: tally.unsafe, refused: tally.refused }));
  }
  // Forced orderings. Each implementation is paused after its final pixel check
  // (or not at all, for a change made before the click), one change is applied
  // and shown for three frames, and the click continues. Outcomes do not depend
  // on timing luck.
  const FORCED = [
    // Controls: nothing changes, so every trial must deliver and commit.
    ['unchanged-control', null, false],
    ['unchanged-control-paused-after-final-check', null, true],
    ['pixels-changed-and-still-showing-before-click', 'pixels', false],
    ['one-frame-pixel-change-reverted-before-click', 'transient-pixels', false],
    ['pixels-changed-after-final-check', 'pixels', true],
    ['target-swapped-after-final-check', 'target', true],
    ['one-frame-target-swap-removed-after-final-check', 'transient-target', true],
    // The recipient is a button inside a closed shadow root.
    ['closed-shadow-unchanged-control-paused-after-final-check', null, true, 'shadow'],
    ['closed-shadow-target-swapped-after-final-check', 'shadow-target', true, 'shadow'],
  ];
  if (implementation.bound) report.extensionBinds = { canvasControl: await implementation.bound(point), closedShadowControl: await implementation.bound(shadowPoint) };
  report.forced = {};
  for (const [name, kind, paused, where] of forcedTrials > 0 ? FORCED : []) {
    const at = where === 'shadow' ? [shadowRect, shadowPoint] : [rect, point];
    // observedAs: what the implementation itself reported for the trial, which
    // shows whether and at which step it noticed the change. Application commits
    // are counted apart from the input the page received.
    const tally = { trials: forcedTrials, delivered: 0, deliveredToReviewedTarget: 0, deliveredToLookalike: 0,
      deliveredWhileChangedPixelsShowing: 0, untrustedDeliveries: 0, refused: {}, statusBeforeClick: {}, observedAs: {},
      applicationCommits: 0, commitsOfReviewedAmount: 0, commitsOfChangedAmount: 0, commitsByLookalike: 0 };
    for (let i = 0; i < forcedTrials; i++) {
      await page.evaluate(() => window.begin());
      const review = await implementation.review(...at);
      let outcome;
      if (!paused) {
        if (kind) await page.evaluate(k => window.change(k), kind);
        if (implementation.status) { const reason = (await implementation.status(review)).reason; tally.statusBeforeClick[reason] = (tally.statusBeforeClick[reason] ?? 0) + 1; }
        outcome = await implementation.click(review);
      } else {
        await implementation.pauseArm();
        const pending = implementation.click(review);
        const reached = await Promise.race([implementation.pauseReached().then(() => true), pending.then(() => false)]);
        if (reached) { if (kind) await page.evaluate(k => window.change(k), kind); await implementation.pauseRelease(); }
        else tally.neverReachedPause = (tally.neverReachedPause ?? 0) + 1;
        outcome = await pending;
        await implementation.pauseClear();
      }
      await wait(150);
      const trial = await page.evaluate(() => window.trial);
      const click = trial.clicks[0];
      const said = 'admitted' in outcome ? `${outcome.admitted ? 'admitted' : 'not-admitted'}:${outcome.reason}` : outcome.reason;
      tally.observedAs[said] = (tally.observedAs[said] ?? 0) + 1;
      for (const commit of trial.commits) {
        tally.applicationCommits++;
        if (commit.record === 'lookalike') tally.commitsByLookalike++;
        else if (commit.amount === '$1,280.00') tally.commitsOfReviewedAmount++;
        else tally.commitsOfChangedAmount++;
      }
      if (!click) { const key = outcome.reason ?? 'no-delivery'; tally.refused[key] = (tally.refused[key] ?? 0) + 1; continue; }
      tally.delivered++;
      if (!click.trusted) tally.untrustedDeliveries++;
      if (click.target === 'lookalike') tally.deliveredToLookalike++; else tally.deliveredToReviewedTarget++;
      if (click.shown !== '$1,280.00') tally.deliveredWhileChangedPixelsShowing++;
    }
    report.forced[name] = tally;
    console.log(JSON.stringify({ arm: values.arm, forced: name, delivered: tally.delivered, toLookalike: tally.deliveredToLookalike,
      whileChanged: tally.deliveredWhileChangedPixelsShowing, commits: tally.applicationCommits, observedAs: tally.observedAs }));
  }
  report.unsafeDeliveries = Object.values(report.attacks).reduce((n, t) => n + t.unsafe, 0);
  report.completed = true;
} catch (error) {
  report.error = error.stack;
} finally {
  await closeBrowser().catch(() => {});
  server.close();
  if (profile) await rm(profile, { recursive: true, force: true });
  if (barrierDir) await rm(barrierDir, { recursive: true, force: true });
  await writeFile(values.out, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
}
