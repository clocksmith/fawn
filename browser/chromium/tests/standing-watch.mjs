// A standing watch on twelve regions of one visible, canvas-drawn page.
//
// Two watchers are measured on the same browser binary, the same page and the
// same scripted changes, with software rendering and no hardware GPU:
//   fork:  one Fawn visual review per region. The compositor copies each
//          region out of every frame it draws and compares it with the
//          reviewed copy; the first difference is latched. The test only asks
//          for the verdict.
//   poll:  what an extension can call through the debugger:
//          Page.captureScreenshot of the tile area, as fast as it returns,
//          compared region by region in the caller.
//   stream: the tab captured as video (the capture chrome.tabCapture gives an
//          extension), every delivered frame compared in a second page.
// Cases, each `--trials` times:
//   lasting    one tile's text changes and stays changed
//   one-frame  one tile's text changes for a single animation frame, then
//              returns to what it was
//   unchanged  frames keep being drawn (a moving bar below the tiles) while
//              no tile changes
// Also recorded: what each watcher says while the page is hidden, and the
// page's frame rate with and without the watch.
//   node standing-watch.mjs --browser <chrome> --out <report.json> [--trials 1000] [--predictions <file>]
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { setTimeout as wait } from 'node:timers/promises';
import { parseArgs } from 'node:util';
import { inflateSync } from 'node:zlib';
import { chromium } from 'playwright-core';

const { values } = parseArgs({ options: { browser: { type: 'string' }, out: { type: 'string' }, trials: { type: 'string' }, predictions: { type: 'string' }, 'cost-only': { type: 'boolean' } } });
assert(values.browser && values.out, '--browser and --out are required');
const TRIALS = Number(values.trials ?? 1000), TILES = 12;
const tile = i => ({ x: 10 + (i % 4) * 156, y: 10 + Math.floor(i / 4) * 120, w: 150, h: 110 });
const watched = i => { const t = tile(i); return { x: t.x + 2, y: t.y + 2, width: t.w - 4, height: t.h - 4 }; };
const AREA = { x: 0, y: 0, width: 640, height: 372 }; // every tile, not the moving bar
const fixture = `<!doctype html><title>Supplier terms</title><style>*{margin:0} canvas{display:block}</style>
<canvas width=640 height=420></canvas><script>
const c = document.querySelector('canvas'), ctx = c.getContext('2d'), tiles = [];
let frames = 0;
const rectOf = i => ({ x: 10 + (i % 4) * 156, y: 10 + Math.floor(i / 4) * 120, w: 150, h: 110 });
function draw(i, text) { const r = rectOf(i); ctx.fillStyle = '#f4f1e8'; ctx.fillRect(r.x, r.y, r.w, r.h); ctx.fillStyle = '#1a1a1a';
  ctx.font = '14px sans-serif'; ctx.fillText('Supplier ' + i, r.x + 10, r.y + 30); ctx.font = 'bold 18px sans-serif'; ctx.fillText(text, r.x + 10, r.y + 64); tiles[i] = text; }
ctx.fillStyle = '#fff'; ctx.fillRect(0, 0, 640, 420);
for (let i = 0; i < 12; i++) draw(i, 'Net 30  $' + (1000 + i * 37) + '.00');
function tick() { frames++; ctx.fillStyle = '#fff'; ctx.fillRect(0, 385, 640, 30); ctx.fillStyle = '#3b6'; ctx.fillRect((frames * 7) % 600, 390, 40, 20); requestAnimationFrame(tick); }
requestAnimationFrame(tick);
window.setTile = (i, text) => { draw(i, text); };
window.flashTile = i => new Promise(done => { const old = tiles[i];
  requestAnimationFrame(() => { draw(i, 'Net 90  $9,999.00'); requestAnimationFrame(() => { draw(i, old); requestAnimationFrame(() => requestAnimationFrame(() => done(frames))); }); }); });
window.waitFrames = n => new Promise(done => { const end = frames + n; (function next() { frames >= end ? done(frames) : requestAnimationFrame(next); })(); });
window.frameRate = ms => new Promise(done => { const start = performance.now(), first = frames; setTimeout(() => done((frames - first) * 1000 / (performance.now() - start)), ms); });
</script>`;

// PNG as the debugger returns it: 8-bit RGB or RGBA, not interlaced.
function decodePng(buffer) {
  let offset = 8, width = 0, height = 0, channels = 0; const data = [];
  while (offset < buffer.length) {
    const length = buffer.readUInt32BE(offset), type = buffer.toString('latin1', offset + 4, offset + 8), body = buffer.subarray(offset + 8, offset + 8 + length);
    if (type === 'IHDR') { width = body.readUInt32BE(0); height = body.readUInt32BE(4); assert.equal(body[8], 8); assert.equal(body[12], 0);
      channels = body[9] === 6 ? 4 : body[9] === 2 ? 3 : assert.fail('unexpected PNG colour type ' + body[9]); }
    if (type === 'IDAT') data.push(body);
    offset += 12 + length;
  }
  const raw = inflateSync(Buffer.concat(data)), stride = width * channels, pixels = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)], row = y * stride, source = y * (stride + 1) + 1;
    for (let x = 0; x < stride; x++) {
      const a = x >= channels ? pixels[row + x - channels] : 0, b = y ? pixels[row - stride + x] : 0, c = x >= channels && y ? pixels[row - stride + x - channels] : 0;
      let predicted = 0;
      if (filter === 1) predicted = a; else if (filter === 2) predicted = b; else if (filter === 3) predicted = (a + b) >> 1;
      else if (filter === 4) { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); predicted = pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      pixels[row + x] = (raw[source + x] + predicted) & 255;
    }
  }
  return { width, height, channels, pixels };
}
const regionDigest = (image, r) => { const hash = createHash('sha1');
  for (let y = r.y; y < r.y + r.height; y++) hash.update(image.pixels.subarray((y * image.width + r.x) * image.channels, (y * image.width + r.x + r.width) * image.channels));
  return hash.digest('hex'); };
const summary = list => { if (!list.length) return null; const s = [...list].sort((a, b) => a - b), at = q => +s[Math.min(s.length - 1, Math.floor(q * s.length))].toFixed(2);
  return { n: s.length, min: +s[0].toFixed(2), p50: at(0.5), p95: at(0.95), p99: at(0.99), max: +s[s.length - 1].toFixed(2) }; };

const watcher = `<!doctype html><title>Watcher</title><button id=go>watch</button><script>
const REG = ${JSON.stringify(Array.from({ length: TILES }, (_, i) => watched(i)))};
let baseline = null, wantBase = true, framesSeen = 0, size = null; const seen = new Map();
const canvas = new OffscreenCanvas(640, 372), ctx = canvas.getContext('2d', { willReadFrequently: true });
function differs(a, b, r) { let n = 0; for (let y = r.y; y < r.y + r.height; y++) { let o = (y * 640 + r.x) * 4;
  for (let x = 0; x < r.width; x++, o += 4) if (Math.abs(a[o] - b[o]) > 24 || Math.abs(a[o + 1] - b[o + 1]) > 24 || Math.abs(a[o + 2] - b[o + 2]) > 24) n++; } return n > 40; }
go.onclick = async () => { try {
  const stream = await navigator.mediaDevices.getDisplayMedia({ video: { frameRate: { ideal: 60 } }, audio: false });
  const track = stream.getVideoTracks()[0]; window.settings = track.getSettings();
  const reader = new MediaStreamTrackProcessor({ track }).readable.getReader(); window.started = true;
  for (;;) { const { value: frame, done } = await reader.read(); if (done) break;
    size = [frame.displayWidth, frame.displayHeight];
    const sx = frame.displayWidth / window.target.w, sy = frame.displayHeight / window.target.h;
    ctx.drawImage(frame, 0, 0, 640 * sx, 372 * sy, 0, 0, 640, 372); frame.close();
    const data = ctx.getImageData(0, 0, 640, 372).data; framesSeen++;
    if (wantBase) { baseline = data; wantBase = false; seen.clear(); continue; }
    for (let i = 0; i < REG.length; i++) if (!seen.has(i) && differs(data, baseline, REG[i])) seen.set(i, performance.timeOrigin + performance.now());
  } } catch (error) { window.failed = String(error); } };
window.rebase = () => new Promise(done => { wantBase = true; (function next() { wantBase ? setTimeout(next, 2) : done(framesSeen); })(); });
window.state = () => ({ seen: [...seen], framesSeen, size });
window.clearSeen = () => seen.clear();
</script>`;

// ---- stream: the tab captured as video, every delivered frame compared ----
// The capture an extension gets from chrome.tabCapture, taken here with getDisplayMedia in a second page.
async function streamPhase() {
  const out = report.stream = {};
  let second;
  try {
    second = await chromium.launch({ executablePath: resolve(values.browser), headless: true, chromiumSandbox: true,
      args: ['--use-gl=angle', '--use-angle=swiftshader', '--use-webgpu-runtime=dawn', '--disable-accelerated-2d-canvas', '--ozone-platform=headless', '--force-device-scale-factor=1',
        '--auto-select-tab-capture-source-by-title=Supplier terms'] });
    const context = await second.newContext({ viewport: null });
    await context.route('http://127.0.0.1/**', route => route.fulfill({ contentType: 'text/html', body: new URL(route.request().url()).pathname === '/watcher' ? watcher : fixture }));
    const page = await context.newPage(); await page.goto('http://127.0.0.1/terms'); await page.screenshot();
    out.frameRateWithoutWatch = +(await page.evaluate(() => frameRate(3000))).toFixed(1);
    const viewer = await context.newPage(); await viewer.goto('http://127.0.0.1/watcher');
    await viewer.evaluate(target => { window.target = target; }, await page.evaluate(() => ({ w: innerWidth, h: innerHeight })));
    await viewer.bringToFront();
    await viewer.click('#go');
    await viewer.waitForFunction(() => window.started || window.failed, null, { timeout: 15000 });
    const failed = await viewer.evaluate(() => window.failed ?? null);
    if (failed) { out.notRun = failed; return; }
    await wait(1000);
    out.capture = await viewer.evaluate(() => ({ settings: window.settings, ...window.state() }));
    out.watchedPage = { visibilityState: await page.evaluate(() => document.visibilityState), frameRateWithWatch: +(await page.evaluate(() => frameRate(3000))).toFixed(1) };
    const epoch = () => performance.timeOrigin + performance.now();
    const lastingText = n => `Net 45  $${2000 + n}.00`;
    await viewer.evaluate(() => rebase());
    {
      const first = (await viewer.evaluate(() => state())).framesSeen, start = performance.now(); let drawn = 0;
      while (drawn < TRIALS) drawn += await page.evaluate(() => { const begin = frames; return waitFrames(100).then(now => now - begin); });
      const end = await viewer.evaluate(() => state());
      out.unchanged = { framesDrawnByThePage: drawn, seconds: +((performance.now() - start) / 1000).toFixed(1), framesDelivered: end.framesSeen - first, falseAlarms: end.seen.length };
    }
    await viewer.evaluate(() => rebase());
    {
      const latency = []; let missed = 0, crossAlarms = 0;
      for (let n = 0; n < TRIALS; n++) {
        const i = n % TILES; await wait(Math.random() * 40);
        const start = epoch();
        await page.evaluate(([index, text]) => setTile(index, text), [i, lastingText(n)]);
        let at = null, seen = [];
        while (epoch() - start < 2000) { seen = (await viewer.evaluate(() => state())).seen; at = seen.find(([region]) => region === i)?.[1] ?? null; if (at !== null) break; }
        if (at !== null) latency.push(at - start); else missed++;
        crossAlarms += seen.filter(([region]) => region !== i).length;
        await viewer.evaluate(() => rebase());
      }
      out.lasting = { changes: TRIALS, caught: latency.length, missed, msFromChangeToVerdict: summary(latency), alarmsOnTheOtherElevenRegions: crossAlarms };
    }
    {
      let caught = 0, missed = 0, crossAlarms = 0; const before = (await viewer.evaluate(() => state())).framesSeen;
      for (let n = 0; n < TRIALS; n++) {
        const i = n % TILES; await viewer.evaluate(() => clearSeen());
        await wait(Math.random() * 40);
        await page.evaluate(index => flashTile(index), i);
        await wait(100);
        const seen = (await viewer.evaluate(() => state())).seen;
        if (seen.some(([region]) => region === i)) caught++; else missed++;
        crossAlarms += seen.filter(([region]) => region !== i).length;
      }
      out.oneFrame = { changes: TRIALS, caught, missed, alarmsOnTheOtherElevenRegions: crossAlarms, framesDelivered: (await viewer.evaluate(() => state())).framesSeen - before };
    }
  } catch (error) { out.error = error.stack ?? String(error); }
  finally { await second?.close().catch(() => null); }
}

// ---- cost: processor time the browser spends while each watcher runs ----
// Every process of the browser is counted (clock ticks from /proc), split into the GPU process, which
// holds the compositor, and everything else. The polling caller's own decoding runs outside the browser
// and is reported apart.
async function costPhase() {
  const out = report.cost = { seconds: 10, note: 'Processor seconds used per second of wall time, software rendering.' };
  const ticks = pid => { try { const f = readFileSync(`/proc/${pid}/stat`, 'utf8'); const rest = f.slice(f.lastIndexOf(')') + 2).split(' '); return { parent: Number(rest[1]), used: Number(rest[11]) + Number(rest[12]) }; } catch { return null; } };
  const family = root => { const all = new Map(); for (const name of readdirSync('/proc')) if (/^\d+$/.test(name)) { const t = ticks(name); if (t) all.set(Number(name), t); }
    const members = new Set([root]); for (let grew = true; grew;) { grew = false; for (const [pid, t] of all) if (!members.has(pid) && members.has(t.parent)) { members.add(pid); grew = true; } }
    const sum = { gpuProcess: 0, others: 0 };
    for (const pid of members) { let line = ''; try { line = readFileSync(`/proc/${pid}/cmdline`, 'utf8'); } catch {}
      if (line.includes('--type=gpu-process')) sum.gpuProcess += all.get(pid).used; else sum.others += all.get(pid).used; }
    return sum; };
  for (const mode of ['none', 'fork', 'poll', 'stream']) {
    const tag = '--fawn-cost-run=' + process.pid + '-' + mode; let instance;
    try {
      instance = await chromium.launch({ executablePath: resolve(values.browser), headless: true, chromiumSandbox: true,
        args: ['--use-gl=angle', '--use-angle=swiftshader', '--use-webgpu-runtime=dawn', '--disable-accelerated-2d-canvas', '--ozone-platform=headless', '--force-device-scale-factor=1',
          '--auto-select-tab-capture-source-by-title=Supplier terms', tag] });
      const root = Number(execFileSync('pgrep', ['-o', '-f', '--', tag]).toString().trim().split('\n')[0]);
      const context = await instance.newContext({ viewport: null });
      await context.route('http://127.0.0.1/**', route => route.fulfill({ contentType: 'text/html', body: new URL(route.request().url()).pathname === '/watcher' ? watcher : fixture }));
      const page = await context.newPage(); await page.goto('http://127.0.0.1/terms'); await page.screenshot();
      let stop = () => {}, callerMs = 0;
      if (mode === 'fork') for (let i = 0; i < TILES; i++) { const session = await context.newCDPSession(page), r = watched(i);
        for (let attempt = 0; ; attempt++) { try { await session.send('Input.captureVisualEvidence', { ...r, targetX: r.x + (r.width >> 1), targetY: r.y + (r.height >> 1) }); break; } catch (error) { if (attempt > 8) throw error; await wait(20); } } }
      if (mode === 'poll') { const session = await context.newCDPSession(page); let running = true;
        const loop = (async () => { while (running) { const data = (await session.send('Page.captureScreenshot', { format: 'png', clip: { ...AREA, scale: 1 } })).data;
          const start = performance.now(), image = decodePng(Buffer.from(data, 'base64')); for (let i = 0; i < TILES; i++) regionDigest(image, watched(i)); callerMs += performance.now() - start; } })();
        stop = async () => { running = false; await loop; }; }
      if (mode === 'stream') { const viewer = await context.newPage(); await viewer.goto('http://127.0.0.1/watcher');
        await viewer.evaluate(target => { window.target = target; }, await page.evaluate(() => ({ w: innerWidth, h: innerHeight })));
        await viewer.bringToFront(); await viewer.click('#go'); await viewer.waitForFunction(() => window.started || window.failed, null, { timeout: 15000 }); }
      await wait(2000); callerMs = 0;
      const before = family(root), start = performance.now(), firstFrame = await page.evaluate(() => frames);
      await wait(out.seconds * 1000);
      const after = family(root), seconds = (performance.now() - start) / 1000, drawn = (await page.evaluate(() => frames)) - firstFrame;
      await stop();
      out[mode] = { gpuProcess: +((after.gpuProcess - before.gpuProcess) / 100 / seconds).toFixed(3), restOfBrowser: +((after.others - before.others) / 100 / seconds).toFixed(3),
        pageFramesPerSecond: +(drawn / seconds).toFixed(1), ...(mode === 'poll' ? { callerOutsideBrowser: +(callerMs / 1000 / seconds).toFixed(3) } : {}) };
    } catch (error) { out[mode] = { error: error.message.split('\n')[0] }; }
    finally { await instance?.close().catch(() => null); }
  }
}

const report = { standingWatch: true, trials: TRIALS, regions: TILES, regionPixels: watched(0).width * watched(0).height,
  chromeSha256: createHash('sha256').update(readFileSync(resolve(values.browser))).digest('hex'),
  predictions: values.predictions ? JSON.parse(readFileSync(values.predictions, 'utf8')) : null, startedAt: new Date().toISOString() };
let browser;
try {
  browser = await chromium.launch({ executablePath: resolve(values.browser), headless: true, chromiumSandbox: true,
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--use-webgpu-runtime=dawn', '--disable-accelerated-2d-canvas', '--ozone-platform=headless', '--force-device-scale-factor=1'] });
  report.browserVersion = browser.version();
  const context = await browser.newContext({ viewport: null });
  await context.route('http://127.0.0.1/**', route => route.fulfill({ contentType: 'text/html', body: fixture }));
  const open = async () => { const page = await context.newPage(); await page.goto('http://127.0.0.1/terms'); await page.screenshot(); return page; };
  const lasting = n => `Net 45  $${2000 + n}.00`;

  // ---- fork: the compositor watches ----
  if (!values['cost-only']) {
    const page = await open(), out = report.fork = {};
    out.frameRateWithoutWatch = +(await page.evaluate(() => frameRate(3000))).toFixed(1);
    const sessions = [];
    for (let i = 0; i < TILES; i++) sessions.push(await context.newCDPSession(page));
    const evidence = [];
    const arm = async i => {
      const r = watched(i); let last;
      for (let attempt = 0; attempt < 8; attempt++) {
        try { evidence[i] = (await sessions[i].send('Input.captureVisualEvidence', { ...r, targetX: r.x + (r.width >> 1), targetY: r.y + (r.height >> 1) })).evidenceId; return attempt; }
        catch (error) { last = error; await wait(20); }
      }
      throw last;
    };
    const status = i => sessions[i].send('Input.getVisualEvidenceStatus', { evidenceId: evidence[i] });
    const others = async except => { let alarms = 0; const reasons = {};
      for (let i = 0; i < TILES; i++) if (i !== except) { const s = await status(i); if (!s.holds) { alarms++; reasons[s.reason] = (reasons[s.reason] ?? 0) + 1; } }
      return { alarms, reasons }; };
    let armRetries = 0;
    for (let i = 0; i < TILES; i++) armRetries += await arm(i);
    out.frameRateWithWatch = +(await page.evaluate(() => frameRate(3000))).toFixed(1);

    // unchanged: frames are drawn, no tile changes
    {
      const before = []; for (let i = 0; i < TILES; i++) before.push(await status(i));
      const start = performance.now(); let drawn = 0;
      while (drawn < TRIALS) drawn += await page.evaluate(() => { const first = frames; return waitFrames(100).then(now => now - first); });
      const after = []; for (let i = 0; i < TILES; i++) after.push(await status(i));
      out.unchanged = { framesDrawnByThePage: drawn, seconds: +((performance.now() - start) / 1000).toFixed(1), falseAlarms: after.filter(s => !s.holds).length,
        reasons: after.filter(s => !s.holds).map(s => s.reason),
        framesVerifiedPerRegion: summary(after.map((s, i) => Number(s.verifiedRevision) - Number(before[i].verifiedRevision))) };
      for (let i = 0; i < TILES; i++) if (!after[i].holds) armRetries += await arm(i);
    }
    // lasting change
    {
      const latency = [], ask = []; let missed = 0, crossAlarms = 0; const reasons = {}, crossReasons = {};
      for (let n = 0; n < TRIALS; n++) {
        const i = n % TILES, start = performance.now();
        await page.evaluate(([index, text]) => setTile(index, text), [i, lasting(n)]);
        let seen = null;
        while (performance.now() - start < 2000) { const asked = performance.now(), s = await status(i); ask.push(performance.now() - asked); if (!s.holds) { seen = s; break; } }
        if (seen) { latency.push(performance.now() - start); reasons[seen.reason] = (reasons[seen.reason] ?? 0) + 1; } else missed++;
        const rest = await others(i); crossAlarms += rest.alarms; for (const [k, v] of Object.entries(rest.reasons)) crossReasons[k] = (crossReasons[k] ?? 0) + v;
        for (let k = 0; k < TILES; k++) if (k === i || !(await status(k)).holds) armRetries += await arm(k);
      }
      out.lasting = { changes: TRIALS, caught: latency.length, missed, msFromChangeToVerdict: summary(latency), reasons,
        alarmsOnTheOtherElevenRegions: crossAlarms, otherRegionChecks: TRIALS * (TILES - 1), crossReasons, msPerStatusQuestion: summary(ask) };
    }
    // one-frame change
    {
      let caught = 0, missed = 0, crossAlarms = 0; const reasons = {};
      for (let n = 0; n < TRIALS; n++) {
        const i = n % TILES;
        await wait(Math.random() * 40);
        await page.evaluate(index => flashTile(index), i);
        let seen = null;
        for (const end = performance.now() + 500; performance.now() < end;) { const s = await status(i); if (!s.holds) { seen = s; break; } await wait(5); }
        if (seen) { caught++; reasons[seen.reason] = (reasons[seen.reason] ?? 0) + 1; } else missed++;
        crossAlarms += (await others(i)).alarms;
        for (let k = 0; k < TILES; k++) if (!(await status(k)).holds) armRetries += await arm(k);
      }
      out.oneFrame = { changes: TRIALS, caught, missed, reasons, alarmsOnTheOtherElevenRegions: crossAlarms };
    }
    // hidden: another page comes to the front
    {
      const front = await context.newPage(); await front.goto('http://127.0.0.1/other'); await front.bringToFront(); await wait(500);
      const hidden = []; for (let i = 0; i < TILES; i++) hidden.push(await status(i));
      const hiddenState = await page.evaluate(() => document.visibilityState);
      await page.evaluate(([index, text]) => setTile(index, text), [0, 'Net 60  $1.00']).catch(() => null);
      await wait(500);
      const changedWhileHidden = await status(0);
      await page.bringToFront(); await wait(500);
      const back = []; for (let i = 0; i < TILES; i++) back.push(await status(i));
      out.hidden = { visibilityStateWhileHidden: hiddenState, whileHidden: [...new Set(hidden.map(s => `${s.holds}:${s.reason}`))],
        tileZeroChangedWhileHidden: `${changedWhileHidden.holds}:${changedWhileHidden.reason}`, afterReturning: back.map(s => `${s.holds}:${s.reason}`) };
      await front.close();
    }
    out.rearmRetries = armRetries;
    for (const session of sessions) await session.detach().catch(() => null);
    await page.close();
  }

  // ---- poll: screenshots, as an extension would take them ----
  if (!values['cost-only']) {
    const page = await open(), session = await context.newCDPSession(page), out = report.poll = {};
    out.frameRateWithoutWatch = +(await page.evaluate(() => frameRate(3000))).toFixed(1);
    const regions = Array.from({ length: TILES }, (_, i) => watched(i));
    const look = async () => { const image = decodePng(Buffer.from((await session.send('Page.captureScreenshot', { format: 'png', clip: { ...AREA, scale: 1 } })).data, 'base64'));
      return regions.map(r => regionDigest(image, r)); };
    let baseline = await look(), polls = 0, running = false; const pollMs = [];
    const changed = { seen: new Map() }; // region -> time first seen different
    const watch = () => { running = true; return (async () => { while (running) { const start = performance.now(), now = await look(); pollMs.push(performance.now() - start); polls++;
      now.forEach((digest, i) => { if (digest !== baseline[i] && !changed.seen.has(i)) changed.seen.set(i, performance.now()); }); } })(); };
    let watching = watch();
    const settle = async () => { running = false; await watching; baseline = await look(); changed.seen.clear(); };
    out.frameRateWithWatch = +(await page.evaluate(() => frameRate(3000))).toFixed(1);
    // unchanged
    {
      const first = polls, start = performance.now(); let drawn = 0;
      while (drawn < TRIALS) drawn += await page.evaluate(() => { const begin = frames; return waitFrames(100).then(now => now - begin); });
      out.unchanged = { framesDrawnByThePage: drawn, seconds: +((performance.now() - start) / 1000).toFixed(1), looks: polls - first, falseAlarms: changed.seen.size };
    }
    await settle();
    // lasting change: one look loop per trial, so the baseline is always current
    {
      const latency = []; let missed = 0, crossAlarms = 0;
      for (let n = 0; n < TRIALS; n++) {
        const i = n % TILES, start = performance.now();
        await page.evaluate(([index, text]) => setTile(index, text), [i, lasting(n)]);
        let seen = false;
        while (performance.now() - start < 2000) { const now = await look(); polls++;
          if (now[i] !== baseline[i]) { seen = true; crossAlarms += now.filter((digest, k) => k !== i && digest !== baseline[k]).length; baseline = now; break; } }
        if (seen) latency.push(performance.now() - start); else { missed++; baseline = await look(); }
      }
      out.lasting = { changes: TRIALS, caught: latency.length, missed, msFromChangeToVerdict: summary(latency), alarmsOnTheOtherElevenRegions: crossAlarms };
    }
    // one-frame change: looks run continuously while the tile flashes
    {
      let caught = 0, missed = 0;
      for (let n = 0; n < TRIALS; n++) {
        const i = n % TILES; changed.seen.clear();
        watching = watch();
        await wait(Math.random() * 40);
        await page.evaluate(index => flashTile(index), i);
        await wait(60);
        running = false; await watching;
        if (changed.seen.has(i)) caught++; else missed++;
      }
      out.oneFrame = { changes: TRIALS, caught, missed };
    }
    out.msPerLook = summary(pollMs);
    // hidden
    {
      const front = await context.newPage(); await front.goto('http://127.0.0.1/other'); await front.bringToFront(); await wait(500);
      const hiddenState = await page.evaluate(() => document.visibilityState);
      await page.evaluate(([index, text]) => setTile(index, text), [0, 'Net 60  $1.00']).catch(() => null);
      await wait(500);
      const start = performance.now(); let result;
      try { const now = await Promise.race([look(), wait(5000, 'no answer in 5 s')]); result = typeof now === 'string' ? now : `answered in ${(performance.now() - start).toFixed(0)} ms, ${now.filter((d, i) => d !== baseline[i]).length} regions differ (tile 0 was changed while hidden)`; }
      catch (error) { result = 'error: ' + error.message; }
      out.hidden = { visibilityStateWhileHidden: hiddenState, look: result };
      await front.close();
    }
    await page.close();
  }
  await browser.close(); browser = null;
  if (!values['cost-only']) await streamPhase();
  await costPhase();
  report.passed = true;
} catch (error) {
  report.passed = false; report.error = error.stack ?? String(error);
} finally {
  await browser?.close().catch(() => null);
}
report.finishedAt = new Date().toISOString();
writeFileSync(resolve(values.out), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify({ passed: report.passed, error: report.error ?? null }));
process.exit(report.passed ? 0 : 1);
