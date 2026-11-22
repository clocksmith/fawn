// Exercise browser/Viz ownership in a newly launched, disposable Linux browser.
// No external site, existing browser endpoint, or hardware GPU is used.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { parseArgs } from 'node:util';
import { chromium } from 'playwright-core';

const { values } = parseArgs({ options: { browser: { type: 'string' }, out: { type: 'string' } } });
assert(values.browser && values.out, '--browser and --out are required');
const result = { passed: false, steps: [], unchangedTrials: [] };
const rect = { x: 4, y: 4, width: 16, height: 16 };
const point = { x: 12, y: 12 };
const fixture = `<!doctype html><style>*{margin:0} canvas{display:block}</style>
<canvas width=48 height=48></canvas><script>
let clicks=0; const c=document.querySelector('canvas'), ctx=c.getContext('2d');
ctx.fillStyle='rgb(200,0,0)';ctx.fillRect(0,0,48,48);c.onclick=()=>++clicks;
</script>`;
let browser;
try {
  browser = await chromium.launch({ executablePath: resolve(values.browser), headless: true,
    chromiumSandbox: true, args: ['--use-gl=angle', '--use-angle=swiftshader', '--use-webgpu-runtime=dawn',
      '--disable-accelerated-2d-canvas', '--ozone-platform=headless', '--force-device-scale-factor=1'] });
  result.browserVersion = browser.version();
  const control = await browser.newBrowserCDPSession();
  const context = await browser.newContext({ viewport: null });
  await context.route('http://127.0.0.1/**', route => route.fulfill({ contentType: 'text/html', body: fixture }));
  async function open() {
    const page = await context.newPage();
    await page.goto('http://127.0.0.1/fixture');
    await page.screenshot();
    return { page, session: await context.newCDPSession(page) };
  }
  async function capture(session) {
    const evidence = await session.send('Input.captureVisualEvidence', rect);
    assert.equal(evidence.pixelWidth, rect.width);
    assert.equal(evidence.pixelHeight, rect.height);
    const rgba = Buffer.from(evidence.rgba, 'base64');
    assert.equal(rgba.length, 4 * rect.width * rect.height);
    assert.equal(createHash('sha256').update(rgba).digest('hex'), evidence.baselineDigest);
    return evidence;
  }
  const click = (session, review, position = point) => session.send('Input.dispatchReviewedClick',
    { evidenceId: review.evidenceId, ...position });
  let { page, session } = await open();
  for (let i = 0; i < 3; i++) {
    const receipt = await capture(session);
    assert.deepEqual([...Buffer.from(receipt.rgba, 'base64').subarray(0, 4)], [200, 0, 0, 255]);
    const outcome = await click(session, receipt);
    result.unchangedTrials.push({ frame: receipt.frameRevision, ...outcome });
    assert.equal(outcome.admitted, true);
    await page.waitForFunction(n => clicks === n, i + 1, { timeout: 3000 });
    assert.equal((await click(session, receipt)).admitted, false);
  }
  result.steps.push('unchanged-and-one-shot');
  const outside = await capture(session);
  assert.equal((await click(session, outside, { x: 20, y: 16 })).admitted, false);
  assert.equal((await click(session, outside)).admitted, false);
  const foreign = await context.newCDPSession(page);
  const owned = await capture(session);
  assert.equal((await click(foreign, owned)).admitted, false);
  await foreign.detach();
  result.steps.push('scope-region-and-session');
  await page.evaluate(() => { location.hash = 'different'; });
  assert.equal((await click(session, owned)).admitted, false);
  result.steps.push('same-document-navigation');

  await page.evaluate(() => {
    const frame = document.createElement('iframe'); frame.srcdoc = 'child'; document.body.append(frame);
  });
  await page.waitForFunction(() => frames.length === 1);
  await assert.rejects(capture(session), /unsupported visual surface|surface changed or unsupported/);
  result.steps.push('child-rejected');
  await page.evaluate(() => document.querySelector('iframe').remove());
  await page.screenshot();
  const changedScale = await capture(session);
  await session.send('Emulation.setDeviceMetricsOverride',
    { width: 400, height: 240, deviceScaleFactor: 2, mobile: false });
  await page.screenshot();
  assert.equal((await click(session, changedScale)).admitted, false);
  await assert.rejects(capture(session), /unsupported visual surface|surface changed or unsupported/);
  await session.send('Emulation.clearDeviceMetricsOverride');
  await page.screenshot();
  result.steps.push('child-and-resize-rejected');

  const beforeGpuLoss = await capture(session);
  async function gpuProcess() {
    return (await control.send('SystemInfo.getProcessInfo')).processInfo.find(p => p.type === 'GPU')?.id;
  }
  const oldPid = await gpuProcess();
  assert(oldPid > 0, 'a dedicated Viz GPU process must own the review');
  await control.send('Browser.crashGpuProcess');
  const deadline = Date.now() + 10000;
  let newPid;
  do {
    await page.screenshot({ timeout: 5000 }).catch(() => {});
    newPid = await gpuProcess();
  } while ((!newPid || newPid === oldPid) && Date.now() < deadline);
  assert(newPid > 0 && newPid !== oldPid, 'disposable browser must replace its crashed GPU process');
  assert.equal((await click(session, beforeGpuLoss)).admitted, false);
  assert.equal(await page.evaluate(() => clicks), 3);
  const recovered = await capture(session);
  assert.equal((await click(session, recovered)).admitted, true);
  await page.waitForFunction(() => clicks === 4, undefined, { timeout: 3000 });
  result.steps.push('gpu-process-loss-and-fresh-recovery');

  const beforeRendererLoss = await capture(session);
  const crashed = page.waitForEvent('crash', { timeout: 5000 });
  void session.send('Page.crash').catch(() => {});
  await crashed;
  await page.close().catch(() => {});
  ({ page, session } = await open());
  assert.equal((await click(session, beforeRendererLoss)).admitted, false);
  const fresh = await capture(session);
  assert.equal((await click(session, fresh)).admitted, true);
  await page.waitForFunction(() => clicks === 1, undefined, { timeout: 3000 });
  result.steps.push('renderer-loss-and-fresh-target');
  result.passed = true;
} catch (error) {
  result.error = error.stack;
  process.exitCode = 1;
} finally {
  await browser?.close();
  await writeFile(values.out, JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
  console.log(JSON.stringify(result));
}
