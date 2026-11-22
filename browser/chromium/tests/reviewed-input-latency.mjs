// Profile reviewed clicks end to end on a disposable Linux browser, one arm per
// run, on the same drawing, the same page load and the same correctness bar as
// reviewed-input-qualification.mjs. Arms differ only in compositing and WebGPU
// runtime flags:
//   cpu   Viz software compositing (--disable-gpu-compositing), Dawn runtime
//   dawn  Viz on SwiftShader GL, native Dawn runtime
//   doe   Viz on SwiftShader GL, native Doe runtime
// No hardware GPU is used. The page keeps compositor, main-thread and canvas
// animation running outside the reviewed rectangle plus periodic main-thread
// work, so every trial runs with Viz activating frames.
//
// Per trial: capture round trip, click round trip, and the browser's own trace
// of the click (validation, admission, Viz pixel verdict, renderer delivery).
// Per run: Viz copies per open-review second, frame pacing with and without an
// open review, browser and GPU process RSS, and refusals.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { once } from 'node:events';
import { readFile, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { dirname, join, resolve } from 'node:path';
import { setTimeout as wait } from 'node:timers/promises';
import { parseArgs } from 'node:util';
import { chromium } from 'playwright-core';
import { reviewedClicks } from '../scripts/reviewed-click.mjs';

const { values } = parseArgs({ options: {
  browser: { type: 'string' }, out: { type: 'string' }, arm: { type: 'string' },
  'doe-library': { type: 'string' }, patch: { type: 'string' },
  trials: { type: 'string', default: '300' }, 'pacing-seconds': { type: 'string', default: '10' },
} });
assert(values.browser && values.out && ['cpu', 'dawn', 'doe'].includes(values.arm),
  'usage: --browser CHROME --out NEW.json --arm cpu|dawn|doe [--doe-library LIB] [--trials N]');
assert(values.arm !== 'doe' || values['doe-library'], 'doe needs --doe-library');
const trials = Number(values.trials);
const pacingSeconds = Number(values['pacing-seconds']);
const binary = resolve(values.browser);
const sha256 = async path => createHash('sha256').update(await readFile(path)).digest('hex');
const rect = { x: 0, y: 0, width: 48, height: 48 };
const point = { x: 16, y: 16 };

const runtime = values.arm === 'doe'
  ? ['--use-webgpu-runtime=doe', `--doe-webgpu-library-path=${resolve(values['doe-library'])}`, '--gpu-sandbox-start-early']
  : ['--use-webgpu-runtime=dawn'];
const flags = [...runtime, '--use-gl=angle', '--use-angle=swiftshader', '--disable-accelerated-2d-canvas',
  '--ozone-platform=headless', '--force-device-scale-factor=1',
  ...(values.arm === 'cpu' ? ['--disable-gpu-compositing'] : [])];

// The drawing plus load outside the reviewed rectangle. The page records every
// animation frame time and every trusted input it receives.
const page_html = `<!doctype html><title>Latency drawing</title>
<style>body{margin:0}canvas#drawing{display:block}@keyframes spin{to{transform:rotate(360deg)}}
@keyframes pulse{50%{background:#0a0}to{background:#555}}</style>
<canvas id="drawing" width="48" height="48"></canvas>
<div style="position:absolute;left:120px;top:8px;width:24px;height:24px;background:#555;animation:spin 1s linear infinite"></div>
<div style="position:absolute;left:160px;top:8px;width:24px;height:24px;animation:pulse 1s linear infinite"></div>
<canvas id="ticker" width="24" height="24" style="position:absolute;left:200px;top:8px"></canvas>
<script>
const ctx = document.getElementById('drawing').getContext('2d');
ctx.fillStyle = '#c80000'; ctx.fillRect(0, 0, 48, 48);
const t = document.getElementById('ticker').getContext('2d');
window.frames_ = []; window.clicks = 0;
let n = 0;
const tick = now => { frames_.push(now); t.fillStyle = 'hsl(' + (n++ % 360) + ',60%,50%)'; t.fillRect(0, 0, 24, 24); requestAnimationFrame(tick); };
requestAnimationFrame(tick);
// Periodic main-thread work: 4 ms busy every 16 ms.
setInterval(() => { const end = performance.now() + 4; while (performance.now() < end); }, 16);
document.getElementById('drawing').addEventListener('click', e => { if (e.isTrusted) clicks++; });
</script>`;
const server = createServer((req, res) => { res.setHeader('content-type', 'text/html'); res.end(page_html); });
server.listen(0, '127.0.0.1');
await once(server, 'listening');
const origin = `http://127.0.0.1:${server.address().port}`;

const quantile = (xs, q) => {
  if (!xs.length) return null;
  const s = [...xs].sort((a, b) => a - b);
  return s[Math.min(s.length - 1, Math.ceil(q * s.length) - 1)];
};
const stats = xs => ({ n: xs.length, p50: quantile(xs, 0.5), p90: quantile(xs, 0.9), p99: quantile(xs, 0.99),
  max: xs.length ? Math.max(...xs) : null, mean: xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : null });
const rss = async pid => {
  const status = await readFile(`/proc/${pid}/status`, 'utf8').catch(() => null);
  const kb = status?.match(/VmRSS:\s+(\d+)/)?.[1];
  return kb ? Number(kb) * 1024 : null;
};

const report = {
  schema: 'fawn.reviewed-input-latency/v1', arm: values.arm, flags,
  chromeSha256: await sha256(binary),
  libcontentSha256: await sha256(join(dirname(binary), 'libcontent.so')),
  libvizServiceSha256: await sha256(join(dirname(binary), 'libcomponents_viz_service.so')).catch(() => null),
  harnessSha256: await sha256(new URL(import.meta.url)),
  patchSha256: values.patch ? await sha256(values.patch) : null,
  doeLibrarySha256: values['doe-library'] ? await sha256(values['doe-library']) : null,
  reviewedPixels: rect.width * rect.height, bytesPerCopy: 4 * rect.width * rect.height,
};
let browser, control;
const events = [];
try {
  browser = await chromium.launch({ executablePath: binary, headless: true, chromiumSandbox: true, args: flags });
  report.browserVersion = browser.version();
  control = await browser.newBrowserCDPSession();
  control.on('Tracing.dataCollected', ({ value }) => events.push(...value
    .filter(e => e.name?.startsWith('FawnReview.'))
    .map(e => ({ name: e.name, ts: e.ts, args: e.args }))));
  const browserPid = browser.process?.()?.pid ?? null;
  // Which WebGPU runtime the GPU process mapped, read after the run: the
  // runtime library can load after launch.
  const mapped = async () => {
    const pid = (await control.send('SystemInfo.getProcessInfo')).processInfo.find(p => p.type === 'GPU')?.id;
    const maps = pid ? await readFile(`/proc/${pid}/maps`, 'utf8').catch(() => null) : null;
    return { gpuPid: pid ?? null, doeLibrary: maps === null ? 'unknown' : maps.includes('libwebgpu_doe_c') };
  };

  const context = await browser.newContext({ viewport: null });
  const page = await context.newPage();
  await page.goto(origin + '/');
  await page.evaluate(() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))));
  const session = await context.newCDPSession(page);
  const client = reviewedClicks(session);
  // Read after the page is up: a GPU process that fails initialization is
  // replaced at startup, and the replacement decides the compositing mode.
  const info = (await control.send('SystemInfo.getInfo')).gpu;
  report.gpu = { devices: info.devices?.map(d => ({ vendorId: d.vendorId, deviceId: d.deviceId, vendorString: d.vendorString, deviceString: d.deviceString })),
    compositing: info.featureStatus?.gpu_compositing, rasterization: info.featureStatus?.rasterization,
    webgpu: info.featureStatus?.webgpu };
  report.webgpuAdapter = await page.evaluate(async () => !!(await navigator.gpu?.requestAdapter().catch(() => null)));
  const gpuPid = (await control.send('SystemInfo.getProcessInfo')).processInfo.find(p => p.type === 'GPU')?.id;

  const pacing = async () => {
    const start = await page.evaluate(() => frames_.length);
    await wait(pacingSeconds * 1000);
    const frames = await page.evaluate(s => frames_.slice(s), start);
    const deltas = frames.slice(1).map((t, i) => t - frames[i]);
    return { frames: frames.length, fps: frames.length / pacingSeconds, frameMs: stats(deltas),
      longFrames: deltas.filter(d => d > 2 * 16.7).length };
  };
  report.memoryBefore = { browser: browserPid && await rss(browserPid), gpu: gpuPid && await rss(gpuPid) };
  // Frame pacing with no review open, then with one review held open.
  report.pacingIdle = await pacing();
  await control.send('Tracing.start', { categories: 'viz', transferMode: 'ReportEvents' });
  const held = await client.capture(rect, point);
  report.pacingReviewOpen = await pacing();
  report.memoryReviewOpen = { browser: browserPid && await rss(browserPid), gpu: gpuPid && await rss(gpuPid) };
  const heldOutcome = await client.click(held);
  assert.equal(heldOutcome.delivered, true, JSON.stringify(heldOutcome));

  const rows = [];
  const refused = {};
  for (let i = 0; i < trials; i++) {
    const row = {};
    let t = performance.now();
    try {
      const review = await client.capture(rect, point);
      row.captureMs = performance.now() - t;
      t = performance.now();
      const outcome = await client.click(review);
      row.clickMs = performance.now() - t;
      row.delivered = outcome.delivered;
      if (!outcome.delivered) refused[`${outcome.admitted ? 'admitted-' : ''}${outcome.reason}`] = (refused[`${outcome.admitted ? 'admitted-' : ''}${outcome.reason}`] ?? 0) + 1;
    } catch (error) {
      row.delivered = false;
      const key = `capture-refused: ${error.message.split('\n')[0]}`;
      refused[key] = (refused[key] ?? 0) + 1;
    }
    rows.push(row);
  }
  report.memoryAfter = { browser: browserPid && await rss(browserPid), gpu: gpuPid && await rss(gpuPid) };
  report.runtimeMapped = await mapped();
  assert.equal(report.runtimeMapped.gpuPid, gpuPid, 'GPU process restarted during the run');
  if (values.arm === 'doe') assert.notEqual(report.runtimeMapped.doeLibrary, false, 'Doe arm GPU process did not map Doe');
  else assert.notEqual(report.runtimeMapped.doeLibrary, true, 'non-Doe arm GPU process mapped Doe');
  await wait(300);
  report.trustedClicksAtPage = await page.evaluate(() => clicks);
  {
    const done = once(control, 'Tracing.tracingComplete');
    await control.send('Tracing.end');
    await done;
  }
  const delivered = rows.filter(r => r.delivered);
  report.trials = trials;
  report.delivered = delivered.length;
  report.refused = refused;
  report.unnecessaryRefusalRate = (trials - delivered.length) / trials;
  report.captureMs = stats(delivered.map(r => r.captureMs));
  report.clickMs = stats(delivered.map(r => r.clickMs));

  // Browser-side phases per click, from the trace (microseconds to ms).
  // Validated -> Admission: final checks to the admission compare-and-exchange.
  // Admission -> PixelVerdict: waiting for Viz to verify the frame active at
  // admission. PixelVerdict -> Delivery: renderer re-check and dispatch.
  events.sort((a, b) => a.ts - b.ts);
  const phases = { validatedToAdmission: [], admissionToVerdict: [], verdictToDelivery: [] };
  let open = null;
  for (const e of events) {
    if (e.name === 'FawnReview.Validated') open = { validated: e.ts };
    else if (open && e.name === 'FawnReview.Admission') open.admission = e.ts;
    else if (open?.admission && e.name === 'FawnReview.PixelVerdict') open.verdict = e.ts;
    else if (open?.verdict && e.name === 'FawnReview.Delivery') {
      phases.validatedToAdmission.push((open.admission - open.validated) / 1000);
      phases.admissionToVerdict.push((open.verdict - open.admission) / 1000);
      phases.verdictToDelivery.push((e.ts - open.verdict) / 1000);
      open = null;
    }
  }
  report.phasesMs = Object.fromEntries(Object.entries(phases).map(([k, v]) => [k, stats(v)]));
  // Copies Viz made: one reference per capture plus one verification per drawn
  // frame of the reviewed surface while a review was open. Readbacks only:
  // blits inside the GPU process and driver transfers are not visible here.
  const references = events.filter(e => e.name === 'FawnReview.Reference').length;
  const verdicts = events.filter(e => e.name === 'FawnReview.Verdict');
  report.vizCopies = { references, verifications: verdicts.length,
    mismatches: verdicts.filter(e => e.args?.matched === false).length,
    readbackBytes: (references + verdicts.length) * report.bytesPerCopy,
    gpuBlitsAndDriverTransfers: 'unknown' };
  report.correct = report.trustedClicksAtPage === delivered.length + 1;
  report.passed = report.correct;
} catch (error) {
  report.error = error.stack;
  report.passed = false;
} finally {
  await browser?.close();
  server.close();
  await writeFile(values.out, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
  console.log(JSON.stringify({ arm: report.arm, passed: report.passed, delivered: report.delivered,
    refusal: report.unnecessaryRefusalRate, clickP99: report.clickMs?.p99, captureP99: report.captureMs?.p99,
    verdictP99: report.phasesMs?.admissionToVerdict?.p99, error: report.error?.split('\n')[0] }));
}
