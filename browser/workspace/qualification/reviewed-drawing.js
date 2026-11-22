// Local, no-model comparison. Only a Fawn native receipt can establish input
// admission. Stock arms explicitly use independent validation and ordinary input.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { chromium } from 'playwright-core';
import { reviewedClicks } from '../../chromium/scripts/reviewed-click.mjs';
import { NativeFrameHelper } from '../src/adapters/native-frame-helper.js';
import { playwrightLaunchOptions } from '../src/browser-launch.js';
import { loadClientServiceConfig } from '../src/client/browser-selection.js';
import { openDrawingSite } from './reviewed-drawing-site.js';

const { PNG } = createRequire(import.meta.url)('playwright-core/lib/utilsBundle');
const [arm, browserConfig] = process.argv.slice(2);
const native = ['fawn-dawn', 'fawn-doe'].includes(arm);
if (!(native ? browserConfig : ['stock', 'stock-helper'].includes(arm) && !browserConfig)) {
  throw Error('Usage: node reviewed-drawing.js stock | stock-helper | fawn-dawn PROFILE | fawn-doe PROFILE');
}
const { browser: config } = loadClientServiceConfig(native ? { browserConfig, browserChannel: 'fawn', headless: true }
  : { browserChannel: 'chrome', headless: true });
if (native) assert.equal(config.fawnBuild.runtime.mode, arm === 'fawn-doe' ? 'doe-c' : 'dawn');
const launch = playwrightLaunchOptions(config, true);
const site = await openDrawingSite();
const clip = { x: 0, y: 0, width: 48, height: 48 };
const point = { x: 16, y: 16 };
let browser, page, client, helper;
const report = { arm, modelCalls: 0 };

async function openReviewer(account) {
  browser = await chromium.launch({ ...launch, args: [...(launch.args ?? []), '--window-size=800,600'], headless: true });
  report.browser = browser.version();
  const context = await browser.newContext({ viewport: null });
  page = await context.newPage();
  await page.goto(site.login(account));
  client = reviewedClicks(await context.newCDPSession(page));
  if (arm === 'stock-helper') helper = new NativeFrameHelper();
}
async function interrupt() {
  await helper?.close(); helper = null;
  await browser?.close(); browser = null;
}
async function screenshot() {
  const bytes = await page.screenshot({ clip });
  const { data, width, height } = PNG.sync.read(bytes);
  assert.equal(width, clip.width); assert.equal(height, clip.height);
  return data;
}
async function capture() {
  if (native) return client.capture(clip);
  const pixels = await screenshot();
  const digest = helper ? (await helper.sample(clip.width, clip.height, pixels.toString('base64'), true)).hashes.join(',')
    : createHash('sha256').update(pixels).digest('hex');
  return { digest, documentId: (await documentIdentity()), rgba: pixels.toString('base64') };
}
async function documentIdentity() {
  const session = await page.context().newCDPSession(page);
  try { return (await session.send('Page.getFrameTree')).frameTree.frame.loaderId; }
  finally { await session.detach(); }
}
async function validateStock(review) {
  if (native) return;
  const fresh = await capture();
  if (fresh.documentId !== review.documentId) throw Error('stale-document');
  assert.equal(fresh.digest, review.digest);
}
async function repaint() {
  await page.evaluate(() => {
    const ctx = document.querySelector('canvas').getContext('2d');
    ctx.fillStyle = '#0000ff'; ctx.fillRect(0, 0, 1, 1);
  });
  assert.deepEqual([...(await screenshot()).subarray(0, 4)], [0, 0, 255, 255]);
}
async function send(review) {
  if (native) return client.click(review, point);
  await page.mouse.click(point.x, point.y);
  return { ordinaryInputSent: true };
}
async function waitForReply() {
  const reply = await page.waitForFunction(() => window.lastReply, undefined, { timeout: 5000 });
  try { return await reply.jsonValue(); }
  finally { await reply.dispose(); }
}
function verifyRed(review) {
  const rgba = Buffer.from(review.rgba, 'base64');
  assert.deepEqual([...rgba.subarray(0, 4)], [200, 0, 0, 255]);
}
try {
  await openReviewer('sender');
  const sender = await capture();
  verifyRed(sender);
  // Only the evidence fingerprint accompanies the handoff across browser lifetimes.
  const handoffDigest = createHash('sha256').update(Buffer.from(sender.rgba, 'base64')).digest('hex');
  await interrupt();
  const start = performance.now();
  await openReviewer('recipient');
  if (native) await assert.rejects(client.click(sender, point), /again in this session/);
  const old = await capture();
  verifyRed(old);
  assert.equal(createHash('sha256').update(Buffer.from(old.rgba, 'base64')).digest('hex'), handoffDigest);
  report.recipientReproducedDrawing = true;
  await validateStock(old);
  // The image changes only after the comparison. Confirm the blue pixel made it
  // into the browser surface, even if two successive rAFs coalesced.
  await repaint();
  site.revise('repair');
  const stale = await send(old);
  if (native) {
    assert.equal(stale.admitted, false); assert.equal(stale.reason, 'stale-compositor-revision');
    assert.equal(site.attempts.length, 0);
  } else {
    const reply = await waitForReply();
    assert.equal(reply.status, 409); assert.equal(reply.receipt.error, 'stale-record');
    assert.equal(site.attempts.length, 1);
  }
  report.versionChangeAttemptsAtApplication = site.attempts.length;
  assert.equal(site.receipts.size, 0);
  report.versionChangeWrites = site.receipts.size;
  // Two pages show identical drawings but represent different records.
  await page.reload();
  const original = await capture();
  await page.goto(site.origin + '/draw/lookalike');
  assert.equal(original.rgba, (await screenshot()).toString('base64'));
  if (native) assert.equal((await send(original)).admitted, false);
  else await assert.rejects(validateStock(original), /stale-document/);
  assert.equal(site.attempts.length, report.versionChangeAttemptsAtApplication);
  // Fresh authorized review of the intended record. The server is the oracle.
  await page.goto(site.origin + '/draw/repair');
  const fresh = await capture();
  verifyRed(fresh);
  await validateStock(fresh);
  const admitted = await send(fresh);
  if (native) assert.equal(admitted.admitted, true);
  const result = await waitForReply();
  assert.equal(result.status, 200);
  assert.equal(result.receipt.record, 'repair'); assert.equal(result.receipt.account, 'recipient');
  assert.equal(result.receipt.revision, 2); assert.equal(site.receipts.size, 1);
  assert.equal(site.attempts.length, report.versionChangeAttemptsAtApplication + 1);
  report.recipientToVerifiedWriteMs = Math.round(performance.now() - start);
  // Exercise a duplicate at the application boundary independently of input
  // admission, as if the original HTTP acknowledgment had been lost.
  const replay = await page.evaluate(async () => {
    const res = await fetch('/commit/' + request.record, { method: 'POST', body: JSON.stringify(request) });
    return { status: res.status, receipt: await res.json() };
  });
  assert.deepEqual(replay, result); assert.equal(site.receipts.size, 1);
  Object.assign(report, { lookalikeRejected: true, freshRepairWrites: 1, duplicateReturnedOriginalReceipt: true });
  // Isolate the browser guarantee: pixel-only damage need not change the site's
  // version. This deliberately disposable record lets a stale click have an effect.
  await page.goto(site.origin + '/draw/visual-only');
  const unversionedRepaint = await capture();
  await validateStock(unversionedRepaint);
  await repaint();
  const input = await send(unversionedRepaint);
  if (native) {
    assert.equal(input.admitted, false); assert.equal(input.reason, 'stale-compositor-revision');
  } else {
    assert.equal((await waitForReply()).status, 200);
  }
  report.pixelOnlyStaleWrites = [...site.receipts.values()].filter(r => r.record === 'visual-only').length;
  assert.equal(report.pixelOnlyStaleWrites, native ? 0 : 1);
  // Admit current input, then let the record change while the server is processing
  // that request. Even perfect browser evidence cannot authorize the eventual write.
  await page.goto(site.origin + '/draw/late-write');
  const late = await capture();
  await validateStock(late);
  const held = site.pauseCommit('late-write');
  const deadline = AbortSignal.timeout(5000);
  try {
    const delivered = await send(late);
    if (native) assert.equal(delivered.admitted, true);
    await Promise.race([held.arrived, new Promise((_, reject) => {
      if (deadline.aborted) reject(deadline.reason);
      else deadline.addEventListener('abort', () => reject(deadline.reason), { once: true });
    })]);
    site.revise('late-write');
  } finally { held.release(); }
  assert.equal((await waitForReply()).status, 409);
  assert.ok(![...site.receipts.values()].some(r => r.record === 'late-write'));
  report.changeAfterInputRejectedByApplication = true;
  report.completed = true;
} catch (error) {
  report.completed = false;
  report.error = error.message;
  process.exitCode = 1;
} finally {
  try { await interrupt(); } finally { await site.close(); }
  console.log(JSON.stringify(report));
}
