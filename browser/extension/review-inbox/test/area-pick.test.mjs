// Shift and a click, then a drag, in a real Chrome: the drag's release ends the
// pick, the click the browser makes from that release never reaches the page,
// and a click the person makes a moment later does.
//
// A local page records every click its document receives. Through the inbox
// page, a person picks its Pay button with Shift held, then drags a box around
// the button and the total beside it:
//   1. from one element to another (Chrome makes a click on their common ancestor)
//   2. within one element (Chrome makes a click on it)
// After each, the card must exist with the drawn area as its compared rectangle,
// the page must have recorded no click, and a deliberate click on another button
// 200 ms later must be recorded.
//   FAWN_EXTENSION_BROWSER (chrome binary)
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as wait } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';

const binary = process.env.FAWN_EXTENSION_BROWSER;
if (!binary) { console.log('skip: requires a Chrome binary'); process.exit(0); }
const { chromium } = await import('playwright-core').catch(error => { if (error.code !== 'ERR_MODULE_NOT_FOUND') throw error; return {}; });
if (!chromium) { console.log('skip: requires playwright-core'); process.exit(0); }
const extension = process.env.FAWN_EXTENSION_DIR ?? fileURLToPath(new URL('..', import.meta.url));

const page = `<!doctype html><title>Release a payment</title><style>body{margin:0;font:14px sans-serif;height:600px}
div,button{position:absolute;box-sizing:border-box}#total{left:20px;top:20px;width:220px;height:50px;background:#eef}
#pay{left:40px;top:100px;width:120px;height:36px}#note{left:320px;top:200px;width:220px;height:60px;background:#efe}
#other{left:320px;top:320px;width:140px;height:36px}</style>
<div id="total">Total: 1,280.00</div><button id="pay">Pay</button><div id="note">A note</div><button id="other">Something else</button>
<script>window.clicks = []; document.addEventListener('click', event => clicks.push(event.target.id || event.target.tagName));</script>`;
const server = createServer((request, response) => { response.setHeader('content-type', 'text/html'); response.end(page); });
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;

const profile = mkdtempSync(join(tmpdir(), 'fawn-inbox-area-'));
const report = { areaPick: true, drags: [] };
let context;
try {
  context = await chromium.launchPersistentContext(profile, { executablePath: binary, headless: false, chromiumSandbox: true,
    viewport: null, ignoreDefaultArgs: ['--disable-extensions'],
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--disable-gpu-compositing', '--force-device-scale-factor=1',
      `--disable-extensions-except=${extension}`, `--load-extension=${extension}`, '--disable-features=DisableLoadExtensionCommandLineSwitch'] });
  report.browserVersion = context.browser()?.version() ?? null;
  const worker = context.serviceWorkers()[0] ?? await context.waitForEvent('serviceworker');
  const extensionId = new URL(worker.url()).host;
  const inbox = context.pages()[0] ?? await context.newPage();
  await inbox.goto(`chrome-extension://${extensionId}/inbox.html`);
  const target = await context.newPage();
  await target.goto(origin + '/');
  const centre = async selector => { const box = await target.locator(selector).boundingBox(); return { x: box.x + box.width / 2, y: box.y + box.height / 2 }; };

  for (const [name, from, to] of [['across elements', { x: 8, y: 8 }, { x: 430, y: 230 }], ['within one element', { x: 330, y: 210 }, { x: 530, y: 250 }]]) {
    await target.evaluate(() => { window.clicks = []; });
    const before = await inbox.locator('[data-item]').count();
    await inbox.bringToFront();
    await inbox.locator('[data-tabs]').selectOption({ label: await target.title() });
    await inbox.locator('[data-pick]').click();
    await target.getByText('Click the button you want approved').waitFor({ state: 'visible', timeout: 15000 });
    const pay = await centre('#pay');
    await target.keyboard.down('Shift');
    await target.mouse.click(pay.x, pay.y);
    await target.keyboard.up('Shift');
    await target.getByText('Now drag a box around the button').waitFor({ state: 'visible', timeout: 5000 });
    const downAt = await target.evaluate(([x, y]) => document.elementFromPoint(x, y)?.id || document.elementFromPoint(x, y)?.tagName, [from.x, from.y]);
    const upAt = await target.evaluate(([x, y]) => document.elementFromPoint(x, y)?.id || document.elementFromPoint(x, y)?.tagName, [to.x, to.y]);
    await target.mouse.move(from.x, from.y);
    await target.mouse.down();
    await target.mouse.move((from.x + to.x) / 2, (from.y + to.y) / 2);
    await target.mouse.move(to.x, to.y);
    await target.mouse.up();
    // The pick ends on the release, with no click from the test.
    await inbox.bringToFront();
    await inbox.locator('[data-item]').nth(before).waitFor({ timeout: 15000 }).catch(async error => {
      throw new Error(`${name}: no card (down on ${downAt}, up on ${upAt}): notice=${JSON.stringify(await inbox.locator('[data-notice]').textContent())} ` +
        `hint=${JSON.stringify(await target.evaluate(() => [...document.documentElement.children].filter(e => e.tagName === 'DIV').map(e => e.textContent).join(' | ')))} (${error.message.split('\n')[0]})`);
    });
    const item = await inbox.evaluate(async () => Object.values((await chrome.storage.local.get('items')).items).sort((a, b) => a.createdAt.localeCompare(b.createdAt)).at(-1));
    const clicksAfterDrag = await target.evaluate(() => [...window.clicks]);
    // A deliberate click a moment later reaches the page.
    await wait(200);
    const other = await centre('#other');
    await target.mouse.click(other.x, other.y);
    const clicksAfterOwnClick = await target.evaluate(() => [...window.clicks]);
    report.drags.push({ name, downAt, upAt, rect: item.rect, controlRect: item.controlRect, clicksAfterDrag, clicksAfterOwnClick, lines: item.summary.lines });
  }
  for (const drag of report.drags) {
    assert.deepEqual(drag.clicksAfterDrag, [], `${drag.name}: the page received no click from the pick`);
    assert.deepEqual(drag.clicksAfterOwnClick, ['other'], `${drag.name}: the person's own click reached the page`);
    assert.ok(drag.rect.width > drag.controlRect.width && drag.rect.height > drag.controlRect.height, `${drag.name}: the drawn area is compared`);
  }
  assert.notEqual(report.drags[0].downAt, report.drags[0].upAt, 'the first drag ends on another element');
  assert.equal(report.drags[1].downAt, report.drags[1].upAt, 'the second drag stays on one element');
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
