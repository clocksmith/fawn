// Disposable software-rendered tab. This exercises the capture adapter, independent of product timing.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { chromium } from 'playwright-core';
import { CompositorSessions } from '../../src/adapters/compositor-capture.js';
import { buildSnapshot } from '../../src/adapters/page-query.js';

if (process.env.BLITZEN_TEST_CAPTURE_BINDING !== '1') {
  console.log('skip: set BLITZEN_TEST_CAPTURE_BINDING=1 for disposable current-tab capture');
  process.exit(0);
}
const server = createServer((_req, res) => {
  res.setHeader('content-type', 'text/html');
  res.end(`<!doctype html><title>Isolated capture fixture</title>
    <style>body{margin:0}canvas{isolation:isolate;position:absolute;left:10px;top:10px}</style>
    <canvas width="64" height="64"></canvas>
    <script>const ctx=document.querySelector('canvas').getContext('2d');
      ctx.fillStyle='red';ctx.fillRect(0,0,64,64)</script>`);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = 'http://127.0.0.1:' + server.address().port;
const sessions = new CompositorSessions();
let browser;
try {
  browser = await chromium.launch({ channel: 'chrome', headless: true, chromiumSandbox: true,
    args: ['--auto-accept-this-tab-capture', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const context = await browser.newContext({ viewport: { width: 320, height: 260 }, deviceScaleFactor: 1 });
  const page = await context.newPage();
  await page.goto(origin);
  await page.bringToFront();
  const cdp = await context.newCDPSession(page);
  const capture = { origin, selector: 'canvas', readRoots: ['canvas'], grantId: 'fixture', grantRevision: 1,
    ownerGeneration: 1, transport: 'stock-i420', stockPlaneSource: false, ttlMs: null, timeoutMs: 10000 };
  let session = 'start';
  async function snapshot() {
    const raw = await sessions.snapshot('fixture', 'main', (method, args) => cdp.send(method, args),
      { mode: 'snapshot', maxElements: 50, maxTextChars: 1000, elementTextChars: 80,
        regions: ['canvas'], readRoots: ['canvas'] }, { ...capture, session });
    const { frameEvidence, regionDigests, regionBitmaps } = buildSnapshot(raw, 1);
    session = frameEvidence.canvas.sessionId;
    return { binding: frameEvidence.canvas.binding, digest: regionDigests.canvas, pixels: regionBitmaps.canvas.hashes };
  }

  // Chromium can resize the first frames while tab capture starts.
  let first = await snapshot(), stable;
  for (let attempt = 0; attempt < 8; attempt++) {
    const next = await snapshot();
    if (first.digest === next.digest) { stable = next; break; }
    first = next;
  }
  assert.ok(stable, 'capture settles on the unchanged evidence');
  await page.evaluate(() => { document.body.style.transform = 'translateX(8px)'; });
  const moved = await snapshot();
  assert.equal(moved.binding.viewportRect.x, stable.binding.viewportRect.x + 8);
  assert.deepEqual(moved.pixels, stable.pixels);
  assert.notEqual(moved.digest, stable.digest);
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 320, height: 260, deviceScaleFactor: 2, mobile: false });
  const scaled = await snapshot();
  assert.equal(moved.binding.deviceScaleFactor, 1);
  assert.equal(scaled.binding.deviceScaleFactor, 2);
  assert.notEqual(scaled.digest, moved.digest);
  await page.evaluate(() => {
    const old = document.querySelector('canvas'), replacement = old.cloneNode();
    replacement.getContext('2d').drawImage(old, 0, 0);
    old.replaceWith(replacement);
  });
  await assert.rejects(snapshot(), /expired|changed|ended|replaced/);
  session = 'start';
  const fresh = await snapshot();
  assert.notEqual(fresh.binding.surfaceId, scaled.binding.surfaceId);
  console.log('ok:', browser.version(), 'restricted capture binds equal pixels to scale, position and surface identity');
} finally {
  await sessions.release();
  await browser?.close();
  server.closeAllConnections();
  await new Promise(resolve => server.close(resolve));
}
