// Qualify reviewed input on a newly launched, disposable Linux browser built
// from chromium-reviewed-input.patch. One local drawing serves every case. No
// external site, existing browser endpoint, or hardware GPU is used: Viz and
// the 2D drawing composite on SwiftShader in both runtime arms.
//
// The browser exposes qualification barriers only under
// --fawn-reviewed-input-barriers. A barrier pauses a dispatch after browser
// validation and before the admission compare-and-exchange ("admission"), or
// after admission and before renderer delivery ("delivery"). While paused, the
// browser reports when Viz has activated a replacement frame on the reviewed
// surface. Every acknowledgment is recorded in the report.
import assert from 'node:assert/strict';
import { createHash, randomUUID } from 'node:crypto';
import { once } from 'node:events';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { platform } from 'node:os';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { setTimeout as wait } from 'node:timers/promises';
import { parseArgs } from 'node:util';
import { chromium } from 'playwright-core';
import { reviewedClicks } from '../scripts/reviewed-click.mjs';

const { values } = parseArgs({ options: {
  browser: { type: 'string' }, out: { type: 'string' }, arm: { type: 'string' },
  'doe-library': { type: 'string' }, patch: { type: 'string' },
  'animation-trials': { type: 'string', default: '20' },
} });
assert(values.browser && values.out && ['dawn', 'doe'].includes(values.arm),
  'usage: --browser CHROME --out NEW.json --arm dawn|doe [--doe-library LIB] [--patch PATCH]');
assert(values.arm === 'dawn' || values.arm === 'doe' && values['doe-library'], 'doe needs --doe-library');
const animationTrials = Number(values['animation-trials']);
assert(Number.isSafeInteger(animationTrials) && animationTrials > 0);

// Same flags as forcedRuntimeArgs in ../scripts/browser-runtime-selector.mjs,
// without that module's package lookups, so a disposable host needs only this file.
const forcedRuntimeArgs = (mode, library) => mode === 'dawn' ? ['--use-webgpu-runtime=dawn']
  : ['--use-webgpu-runtime=doe', `--doe-webgpu-library-path=${library}`, '--gpu-sandbox-start-early'];
const sha256 = async path => createHash('sha256').update(await readFile(path)).digest('hex');
const binary = resolve(values.browser);
const rect = { x: 0, y: 0, width: 48, height: 48 };
const point = { x: 16, y: 16 };
const SEQUENCE = ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click'];

// The drawing. The application owns record identity, version and duplicate
// suppression; the page reports every input event it receives to the server.
async function openDrawing() {
  const records = new Map();
  const receipts = new Map();
  const attempts = [];
  const inputs = [];
  const record = name => {
    if (!records.has(name)) records.set(name, { revision: 1 });
    return records.get(name);
  };
  const animations = {
    none: '',
    compositor: '<div id="spinner" style="position:absolute;left:120px;top:8px;width:24px;height:24px;background:#555;animation:spin 1s linear infinite"></div><style>@keyframes spin{to{transform:rotate(360deg)}}</style>',
    'main-thread': '<div id="pulse" style="position:absolute;left:120px;top:8px;width:24px;height:24px;animation:pulse 1s linear infinite"></div><style>@keyframes pulse{50%{background:#0a0}to{background:#555}}</style>',
    'raf-canvas': '<canvas id="ticker" width="24" height="24" style="position:absolute;left:120px;top:8px"></canvas><script>{const t=document.getElementById("ticker").getContext("2d");let n=0;const f=()=>{t.fillStyle="hsl("+(n++%360)+",60%,50%)";t.fillRect(0,0,24,24);requestAnimationFrame(f)};f()}</script>',
  };
  const server = createServer(async (req, res) => {
    const url = new URL(req.url, 'http://localhost');
    const path = url.pathname.split('/');
    res.setHeader('cache-control', 'no-store');
    const reply = (status, body) => {
      res.writeHead(status, { 'content-type': 'application/json' });
      res.end(JSON.stringify(body));
    };
    const body = async () => {
      const chunks = [];
      for await (const chunk of req) chunks.push(chunk);
      return JSON.parse(Buffer.concat(chunks).toString('utf8'));
    };
    if (req.method === 'GET' && path[1] === 'draw') {
      const name = path[2];
      const request = { record: name, revision: record(name).revision, key: randomUUID() };
      res.setHeader('content-type', 'text/html; charset=utf-8');
      res.end(`<!doctype html><title>Review drawing</title>
        <style>body{margin:0}canvas#drawing{display:block;isolation:isolate}</style>
        <canvas id="drawing" role="button" tabindex="0" aria-label="Repair drawing" width="48" height="48"></canvas>
        ${animations[url.searchParams.get('animate') ?? 'none'] ?? ''}
        <script>
        const doc = crypto.randomUUID();
        const canvas = document.getElementById('drawing'), ctx = canvas.getContext('2d');
        ctx.fillStyle = '#c80000'; ctx.fillRect(0,0,48,48);
        const request = ${JSON.stringify(request)};
        window.inputs = [];
        for (const type of ${JSON.stringify(SEQUENCE)}) document.addEventListener(type, e => {
          const entry = {type, trusted: e.isTrusted, target: e.target.id || e.target.nodeName, doc,
            path: location.pathname, x: e.clientX, y: e.clientY};
          inputs.push(entry);
          fetch('/input', {method: 'POST', keepalive: true, body: JSON.stringify(entry)});
        }, true);
        canvas.onclick = async () => {
          const response = await fetch('/commit/' + request.record, {
            method: 'POST', headers: {'content-type':'application/json'}, body: JSON.stringify(request)
          });
          window.lastReply = {status: response.status, receipt: await response.json()};
        };
        </script>`);
      return;
    }
    if (req.method === 'POST' && path[1] === 'input') {
      inputs.push(await body());
      return reply(204, {});
    }
    if (req.method !== 'POST' || path[1] !== 'commit') return reply(404, {});
    let input;
    try { input = await body(); } catch { return reply(400, { error: 'invalid-request' }); }
    attempts.push({ record: path[2], revision: input?.revision });
    if (!input || input.record !== path[2] || typeof input.key !== 'string'
        || !Number.isSafeInteger(input.revision)) return reply(403, { error: 'wrong-record' });
    const prior = receipts.get(input.key);
    if (prior) {
      if (prior.record !== input.record || prior.revision !== input.revision) return reply(409, { error: 'key-conflict' });
      return reply(200, prior);
    }
    if (record(input.record).revision !== input.revision) return reply(409, { error: 'stale-record' });
    const receipt = { record: input.record, revision: input.revision, writeId: randomUUID() };
    record(input.record).revision++;
    receipts.set(input.key, receipt);
    return reply(200, receipt);
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  return {
    origin: `http://127.0.0.1:${server.address().port}`, attempts, receipts, inputs,
    async close() { const closed = once(server, 'close'); server.close(); server.closeAllConnections(); await closed; },
  };
}

async function until(check, what, timeout = 10000) {
  const deadline = Date.now() + timeout;
  for (;;) {
    const value = await check();
    if (value) return value;
    if (Date.now() > deadline) throw Error(`timed out: ${what}`);
    await wait(5);
  }
}

function barriers(dir) {
  const file = name => join(dir, name);
  const json = async name => readFile(file(name), 'utf8').then(JSON.parse, () => null);
  return {
    hold: phase => writeFile(file(`hold-${phase}`), ''),
    reached: phase => until(() => json(`${phase}.ready`), `${phase} barrier`),
    async activation(phase, timeout = 5000) {
      await writeFile(file(`${phase}.await-activation`), '');
      return until(() => json(`${phase}.viz-activation`), `Viz activation during ${phase}`, timeout);
    },
    async release(phase) {
      await writeFile(file(`${phase}.release`), '');
      return until(() => json(`${phase}.continued`), `${phase} continuation`);
    },
    async clear() {
      for (const phase of ['admission', 'delivery']) {
        for (const suffix of ['.ready', '.await-activation', '.viz-activation', '.continued', '.release']) {
          await rm(file(phase + suffix), { force: true });
        }
        await rm(file(`hold-${phase}`), { force: true });
      }
    },
  };
}

const report = {
  schema: 'fawn.reviewed-input-qualification/v1', arm: values.arm, diagnostic: false,
  chromeSha256: await sha256(binary),
  libcontentSha256: await sha256(join(dirname(binary), 'libcontent.so')),
  harnessSha256: await sha256(new URL(import.meta.url)),
  patchSha256: values.patch ? await sha256(values.patch) : null,
  doeLibrarySha256: values['doe-library'] ? await sha256(values['doe-library']) : null,
  cases: [], animation: {}, application: {},
};
const site = await openDrawing();
const barrierDir = await mkdtemp(join(tmpdir(), 'fawn-reviewed-input-'));
const gate = barriers(barrierDir);
const runtime = values.arm === 'dawn' ? forcedRuntimeArgs('dawn') : forcedRuntimeArgs('doe', resolve(values['doe-library']));
report.flags = [...runtime, '--use-gl=angle', '--use-angle=swiftshader', '--disable-accelerated-2d-canvas',
  '--ozone-platform=headless', '--force-device-scale-factor=1', `--fawn-reviewed-input-barriers=${barrierDir}`];
let browser, control;
try {
  browser = await chromium.launch({ executablePath: binary, headless: true, chromiumSandbox: true, args: report.flags });
  report.browserVersion = browser.version();
  control = await browser.newBrowserCDPSession();
  // Browser-wide trace of the reviewed-input instants (validation, admission,
  // Viz replacement, delivery). Trace timestamps use the browser's monotonic
  // clock; barrier acknowledgments use Unix milliseconds.
  report.trace = [];
  control.on('Tracing.dataCollected', ({ value }) => report.trace.push(...value
    .filter(e => e.name?.startsWith('FawnReview.'))
    .map(e => ({ name: e.name, ts: e.ts, pid: e.pid, args: e.args }))));
  await control.send('Tracing.start', { categories: 'viz', transferMode: 'ReportEvents' });
  // Which WebGPU runtime the GPU process actually mapped. A sandboxed process
  // may refuse /proc reads; that is recorded as unknown, not as a pass.
  {
    const gpu = (await control.send('SystemInfo.getProcessInfo')).processInfo.find(p => p.type === 'GPU')?.id;
    let maps = null;
    if (platform() === 'linux' && gpu) maps = await readFile(`/proc/${gpu}/maps`, 'utf8').catch(() => null);
    report.runtimeMapped = { gpuPid: gpu ?? null,
      doeLibrary: maps === null ? 'unknown' : maps.includes('libwebgpu_doe_c') };
    if (values.arm === 'doe') assert.notEqual(report.runtimeMapped.doeLibrary, false, 'Doe arm GPU process did not map Doe');
    if (values.arm === 'dawn') assert.notEqual(report.runtimeMapped.doeLibrary, true, 'Dawn arm GPU process mapped Doe');
  }
  const context = await browser.newContext({ viewport: null });
  let page, session, client, serial = 0;

  async function open(path) {
    page ??= await context.newPage();
    await page.goto(site.origin + path);
    return settle();
  }
  async function settle() {
    await page.evaluate(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))));
    await page.screenshot();
    session = await context.newCDPSession(page);
    client = reviewedClicks(session);
  }
  async function quiesce() {
    await wait(300);
    return { inputs: site.inputs.length, attempts: site.attempts.length, writes: site.receipts.size };
  }
  const delta = (after, before) => ({ siteInputEvents: after.inputs - before.inputs,
    applicationAttempts: after.attempts - before.attempts, applicationWrites: after.writes - before.writes });
  const sequence = start => site.inputs.slice(start);
  async function runCase(name, body) {
    const row = { name };
    report.cases.push(row);
    await gate.clear();
    const before = { inputs: site.inputs.length, attempts: site.attempts.length, writes: site.receipts.size };
    try {
      await body(row, before);
      row.counts = delta(await quiesce(), before);
      row.passed = true;
    } catch (error) {
      row.counts ??= delta(await quiesce(), before);
      row.error = error.stack;
      row.passed = false;
    }
    console.log(JSON.stringify({ name, passed: row.passed, outcome: row.outcome, counts: row.counts, error: row.error?.split('\n')[0] }));
  }
  const draw1BluePixel = () => page.evaluate(() => { ctx.fillStyle = '#0000ff'; ctx.fillRect(0, 0, 1, 1); });
  const expectDelivered = (row, start) => {
    assert.deepEqual(row.outcome, { admitted: true, delivered: true, reason: 'delivered' });
    const events = sequence(start);
    row.events = events;
    assert.deepEqual(events.map(e => e.type), SEQUENCE);
    assert(events.every(e => e.trusted && e.target === 'drawing' && e.x === point.x && e.y === point.y
      && e.doc === events[0].doc));
  };
  const expectNothingDelivered = row => {
    assert.equal(row.outcome.delivered, false);
    assert.equal(row.counts.siteInputEvents, 0);
    assert.equal(row.counts.applicationAttempts, 0);
    assert.equal(row.counts.applicationWrites, 0);
  };

  // Ordering 1: approve red, pause before admission, paint one pixel blue, wait
  // for Viz to confirm the replacement frame is active on that surface, release.
  await runCase('ordering-1-replacement-active-before-admission', async (row, before) => {
    await open(`/draw/ordering-1-${++serial}`);
    const review = await client.capture(rect, point);
    row.review = { surface: review.surfaceId, document: review.documentId, revision: review.frameRevision };
    assert.deepEqual([...Buffer.from(review.rgba, 'base64').subarray(0, 4)], [200, 0, 0, 255]);
    await gate.hold('admission');
    const pending = client.click(review);
    row.barrier = { reached: await gate.reached('admission') };
    assert.equal(row.barrier.reached.gate, 'open');
    await draw1BluePixel();
    row.barrier.vizActivation = await gate.activation('admission');
    assert.equal(row.barrier.vizActivation.surface, review.surfaceId);
    assert(row.barrier.vizActivation.replacementRevision > Number(review.frameRevision));
    assert.equal(row.barrier.vizActivation.gate, 'stale');
    assert.equal(site.inputs.length, before.inputs);
    row.barrier.continued = await gate.release('admission');
    row.outcome = await pending;
    row.counts = delta(await quiesce(), before);
    assert.deepEqual(row.outcome, { admitted: false, delivered: false, reason: 'stale-compositor-revision' });
    expectNothingDelivered(row);
  });

  // Ordering 2: admit on red, then activate blue before renderer delivery. A
  // repaint after admission is not a stale admission: the click reaches the
  // bound recipient because the document and node are unchanged.
  await runCase('ordering-2-replacement-active-after-admission', async (row, before) => {
    await open(`/draw/ordering-2-${++serial}`);
    const review = await client.capture(rect, point);
    row.review = { surface: review.surfaceId, document: review.documentId, revision: review.frameRevision };
    await gate.hold('delivery');
    const pending = client.click(review);
    row.barrier = { reached: await gate.reached('delivery') };
    assert.equal(row.barrier.reached.gate, 'admitted');
    await draw1BluePixel();
    row.barrier.vizActivation = await gate.activation('delivery');
    assert.equal(row.barrier.vizActivation.surface, review.surfaceId);
    assert(row.barrier.vizActivation.replacementRevision > Number(review.frameRevision));
    assert.equal(row.barrier.vizActivation.gate, 'admitted');
    assert.equal(site.inputs.length, before.inputs);
    row.barrier.continued = await gate.release('delivery');
    row.outcome = await pending;
    await page.waitForFunction(() => window.lastReply, undefined, { timeout: 3000 });
    row.reply = await page.evaluate(() => window.lastReply);
    expectDelivered(row, before.inputs);
    assert.equal(row.reply.status, 200);
  });

  // Ordering 2, recipient changed while queued: same document, a pixel-identical
  // overlay now owns the point. The admitted click is cancelled, not retargeted.
  await runCase('ordering-2-overlay-after-admission', async (row, before) => {
    await open(`/draw/ordering-2-overlay-${++serial}`);
    const review = await client.capture(rect, point);
    await gate.hold('delivery');
    const pending = client.click(review);
    row.barrier = { reached: await gate.reached('delivery') };
    assert.equal(row.barrier.reached.gate, 'admitted');
    await page.evaluate(() => {
      const overlay = document.createElement('div');
      overlay.id = 'overlay';
      overlay.style = 'position:absolute;left:0;top:0;width:48px;height:48px';
      overlay.onclick = () => fetch('/commit/overlay-' + Math.random(), { method: 'POST', body: '{}' });
      document.body.append(overlay);
      document.body.offsetTop;
    });
    row.barrier.continued = await gate.release('delivery');
    row.outcome = await pending;
    row.counts = delta(await quiesce(), before);
    assert.deepEqual(row.outcome, { admitted: true, delivered: false, reason: 'target-changed' });
    expectNothingDelivered(row);
  });

  // Ordering 2, recipient changed while queued: an identical-looking document
  // replaced the reviewed one. The admitted click is cancelled.
  await runCase('ordering-2-navigation-after-admission', async (row, before) => {
    await open(`/draw/ordering-2-navigation-${++serial}`);
    const review = await client.capture(rect, point);
    await gate.hold('delivery');
    const pending = client.click(review);
    row.barrier = { reached: await gate.reached('delivery') };
    await page.goto(`${site.origin}/draw/lookalike-${serial}`);
    await page.evaluate(() => new Promise(r => requestAnimationFrame(r)));
    row.barrier.continued = await gate.release('delivery');
    row.outcome = await pending;
    row.counts = delta(await quiesce(), before);
    assert.equal(row.outcome.admitted, true);
    assert(['recipient-changed', 'document-changed', 'recipient-gone'].includes(row.outcome.reason), row.outcome.reason);
    expectNothingDelivered(row);
    await settle();
  });

  // Pixel-identical changes to the input target between review and dispatch.
  for (const [variant, change] of Object.entries({
    'transparent-overlay': () => { const d = document.createElement('div'); d.id = 'overlay'; d.style = 'position:absolute;left:0;top:0;width:48px;height:48px'; document.body.append(d); },
    'opacity-zero-overlay': () => { const d = document.createElement('div'); d.id = 'overlay'; d.style = 'position:absolute;left:0;top:0;width:48px;height:48px;background:#0f0;opacity:0'; document.body.append(d); },
    'raised-existing-element': () => { document.getElementById('under').style.zIndex = '5'; },
  })) {
    await runCase(`overlay-before-admission-${variant}`, async (row, before) => {
      await open(`/draw/overlay-${variant}-${++serial}`);
      await page.evaluate(() => { const d = document.createElement('div'); d.id = 'under'; d.style = 'position:absolute;left:0;top:0;width:48px;height:48px;z-index:-1'; document.body.append(d); });
      await page.screenshot();
      const review = await client.capture(rect, point);
      await page.evaluate(change);
      await page.evaluate(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))));
      row.outcome = await client.click(review);
      row.counts = delta(await quiesce(), before);
      expectNothingDelivered(row);
      row.refusedBy = row.outcome.admitted ? 'renderer-bound-target' : 'browser-admission';
    });
  }

  let consumed;
  await runCase('unchanged-target-delivers', async (row, before) => {
    await open(`/draw/unchanged-${++serial}`);
    consumed = await client.capture(rect, point);
    row.outcome = await client.click(consumed);
    await page.waitForFunction(() => window.lastReply, undefined, { timeout: 3000 });
    row.reply = await page.evaluate(() => window.lastReply);
    expectDelivered(row, before.inputs);
    assert.equal(row.reply.status, 200);
    report.application.firstReceipt = row.reply.receipt;
  });

  // The client refuses to spend a ticket twice; prove the browser does too by
  // spending the browser's own evidence ID directly.
  await runCase('consumed-approval-replay-refused', async (row, before) => {
    await open(`/draw/replay-${++serial}`);
    const raw = await session.send('Input.captureVisualEvidence', { ...rect, targetX: point.x, targetY: point.y });
    const first = await session.send('Input.dispatchReviewedClick', { evidenceId: raw.evidenceId, ...point });
    await page.waitForFunction(() => window.lastReply, undefined, { timeout: 3000 });
    const afterFirst = await quiesce();
    row.outcome = await session.send('Input.dispatchReviewedClick', { evidenceId: raw.evidenceId, ...point });
    row.first = first;
    assert.deepEqual(first, { admitted: true, delivered: true, reason: 'delivered' });
    assert.deepEqual(row.outcome, { admitted: false, delivered: false, reason: 'unknown-review' });
    const afterReplay = await quiesce();
    row.replayCounts = delta(afterReplay, afterFirst);
    assert.deepEqual(row.replayCounts, { siteInputEvents: 0, applicationAttempts: 0, applicationWrites: 0 });
    assert.equal(afterFirst.inputs - before.inputs, SEQUENCE.length);
  });

  // Application guarantees, kept distinct from the browser input guarantee: the
  // second click above carried the same record version and was refused by the
  // site; a duplicate commit of the first write returns the original receipt.
  await runCase('application-duplicate-commit-returns-original-receipt', async row => {
    const replay = await page.evaluate(async () => {
      const res = await fetch('/commit/' + request.record, { method: 'POST', body: JSON.stringify(request) });
      return { status: res.status, receipt: await res.json() };
    });
    row.replay = replay;
    const original = [...site.receipts.values()].find(r => r.writeId === replay.receipt.writeId);
    assert.equal(replay.status, 200);
    assert(original, 'duplicate must return an existing receipt');
    report.application.duplicateReturnedOriginalReceipt = true;
  });

  await runCase('same-document-navigation-invalidates-then-fresh-review', async (row, before) => {
    await open(`/draw/hash-${++serial}`);
    const review = await client.capture(rect, point);
    await page.evaluate(() => { location.hash = 'different'; });
    row.outcome = await client.click(review);
    assert.equal(row.outcome.admitted, false);
    assert.equal((await quiesce()).inputs, before.inputs);
    await settle();
    row.fresh = await client.click(await client.capture(rect, point));
    assert.equal(row.fresh.delivered, true);
    await page.waitForFunction(() => window.lastReply, undefined, { timeout: 3000 });
  });

  await runCase('cross-document-navigation-invalidates-then-fresh-review', async (row, before) => {
    await open(`/draw/navigate-${++serial}`);
    const review = await client.capture(rect, point);
    await page.goto(`${site.origin}/draw/navigate-target-${serial}`);
    await page.evaluate(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))));
    row.outcome = await client.click(review);
    const afterRefusal = await quiesce();
    assert.equal(row.outcome.admitted, false);
    assert.equal(afterRefusal.inputs, before.inputs);
    await settle();
    row.fresh = await client.click(await client.capture(rect, point));
    assert.equal(row.fresh.delivered, true);
    await page.waitForFunction(() => window.lastReply, undefined, { timeout: 3000 });
  });

  await runCase('renderer-restart-invalidates-then-fresh-review', async (row, before) => {
    await open(`/draw/renderer-${++serial}`);
    const raw = await session.send('Input.captureVisualEvidence', { ...rect, targetX: point.x, targetY: point.y });
    const crashed = page.waitForEvent('crash', { timeout: 5000 });
    void session.send('Page.crash').catch(() => {});
    await crashed;
    await page.close().catch(() => {});
    page = null;
    await open(`/draw/renderer-${serial}`);
    row.outcome = await session.send('Input.dispatchReviewedClick', { evidenceId: raw.evidenceId, ...point });
    assert.deepEqual(row.outcome, { admitted: false, delivered: false, reason: 'unknown-review' });
    assert.equal((await quiesce()).inputs, before.inputs);
    row.fresh = await client.click(await client.capture(rect, point));
    assert.equal(row.fresh.delivered, true);
    await page.waitForFunction(() => window.lastReply, undefined, { timeout: 3000 });
  });

  await runCase('gpu-loss-invalidates-then-fresh-review', async (row, before) => {
    await open(`/draw/gpu-${++serial}`);
    const review = await client.capture(rect, point);
    const gpuProcess = async () => (await control.send('SystemInfo.getProcessInfo')).processInfo.find(p => p.type === 'GPU')?.id;
    const oldPid = await gpuProcess();
    assert(oldPid > 0, 'a dedicated Viz GPU process must own the review');
    await control.send('Browser.crashGpuProcess');
    const newPid = await until(async () => {
      await page.screenshot({ timeout: 2000 }).catch(() => {});
      const pid = await gpuProcess();
      return pid && pid !== oldPid ? pid : null;
    }, 'replacement GPU process');
    row.gpu = { oldPid, newPid };
    row.outcome = await client.click(review);
    assert.deepEqual(row.outcome, { admitted: false, delivered: false, reason: 'stale-compositor-revision' });
    assert.equal(site.inputs.length, before.inputs);
    row.fresh = await client.click(await client.capture(rect, point));
    assert.equal(row.fresh.delivered, true);
    await page.waitForFunction(() => window.lastReply, undefined, { timeout: 3000 });
  });

  // Unnecessary refusals: an unrelated animation outside the reviewed region.
  for (const kind of ['none', 'compositor', 'main-thread', 'raf-canvas']) {
    const tally = { trials: animationTrials, delivered: 0, refused: {} };
    for (let i = 0; i < animationTrials; i++) {
      await gate.clear();
      await open(`/draw/animate-${kind}-${++serial}?animate=${kind}`);
      const start = site.inputs.length;
      let reason;
      try {
        const outcome = await client.click(await client.capture(rect, point));
        reason = outcome.delivered ? null : `${outcome.admitted ? 'admitted-' : ''}${outcome.reason}`;
        if (outcome.delivered) {
          await page.waitForFunction(() => window.lastReply, undefined, { timeout: 3000 });
          assert.deepEqual(sequence(start).map(e => e.type), SEQUENCE);
        } else {
          await wait(200);
          assert.equal(site.inputs.length, start, 'refused click reached the page');
        }
      } catch (error) {
        reason = `capture-refused: ${error.message.split('\n')[0]}`;
      }
      if (reason === null) tally.delivered++;
      else tally.refused[reason] = (tally.refused[reason] ?? 0) + 1;
    }
    tally.unnecessaryRefusalRate = (animationTrials - tally.delivered) / animationTrials;
    report.animation[kind] = tally;
    console.log(JSON.stringify({ animation: kind, ...tally }));
  }
  report.animationPassed = report.animation.none.delivered === animationTrials;
} catch (error) {
  report.error = error.stack;
} finally {
  if (browser?.isConnected() && control) {
    const done = once(control, 'Tracing.tracingComplete');
    await control.send('Tracing.end').then(() => done, () => {});
  }
  await browser?.close();
  await site.close();
  await rm(barrierDir, { recursive: true, force: true });
  report.passed = !report.error && report.cases.length > 0 && report.cases.every(c => c.passed) && report.animationPassed;
  report.summary = {
    browserInput: Object.fromEntries(report.cases.filter(c => !c.name.startsWith('application'))
      .map(c => [c.name, { passed: c.passed, outcome: c.outcome, counts: c.counts }])),
    application: report.application,
    unnecessaryRefusalRate: Object.fromEntries(Object.entries(report.animation).map(([k, v]) => [k, v.unnecessaryRefusalRate])),
  };
  await writeFile(values.out, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
  if (!report.passed) process.exitCode = 1;
}
