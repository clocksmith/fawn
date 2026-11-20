// Independent comparison of two read paths on exactly the same permission-restricted frames.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { chromium } from 'playwright-core';
import { playwrightLaunchOptions } from '../../src/browser-launch.js';
import { createResidentCanvasReader } from '../../src/adapters/resident-canvas.js';
import { testBrowserSelection } from '../support/browser-selection.js';
import { compareCapturedColorPair } from '../support/restricted-frame-color.js';

if (process.env.BLITZEN_TEST_ELEMENT_CAPTURE !== '1') {
  console.log('skip: controlled display and permission chooser required'); process.exit(0);
}
const server = createServer((_request, response) => {
  response.setHeader('Content-Type', 'text/html');
  response.end('<title>Blitzen restricted color fixture</title><style>*{margin:0}canvas{display:block;isolation:isolate;position:relative;z-index:1}</style><canvas id="surface" width="512" height="256"></canvas>');
});
const dir = await mkdtemp(path.join(tmpdir(), 'restricted-color-'));
let context;
try {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const { config } = testBrowserSelection(dir, { headless: false });
  context = await chromium.launchPersistentContext(dir, { ...playwrightLaunchOptions(config.browser, false),
    headless: false, viewport: { width: 512, height: 520 }, deviceScaleFactor: 1 });
  const page = context.pages()[0];
  await page.goto('http://127.0.0.1:' + server.address().port);
  const session = await context.newCDPSession(page);
  const { frameTree } = await session.send('Page.getFrameTree');
  const { executionContextId } = await session.send('Page.createIsolatedWorld', { frameId: frameTree.frame.id, worldName: 'Blitzen restricted color fixture' });
  const read = await session.send('Runtime.evaluate', { contextId: executionContextId, returnByValue: true,
    userGesture: true, awaitPromise: true, expression: '(' + compareCapturedColorPair.toString() + ')(' + createResidentCanvasReader.toString() + ', "#surface")' });
  if (read.exceptionDetails) throw Error(read.exceptionDetails.exception?.description ?? read.exceptionDetails.text);
  const result = read.result.value, [first, second] = result.rows;
  assert.ok(second.mediaTimestampUs > first.mediaTimestampUs);
  if (process.env.BLITZEN_TEST_CAPTURE_COLOR_COLLISION === '1') {
    assert.equal(first.nativeFormat, 'I420');
    assert.notEqual(first.nativePlaneSha256, second.nativePlaneSha256);
    assert.notEqual(first.canonicalSha256, second.canonicalSha256);
    assert.deepEqual(first.externalTileSha256, second.externalTileSha256);
    assert.equal(second.externalRehashed, 0);
    assert.equal(second.explicitRehashed, second.tileCount);
  }
  console.log(JSON.stringify({ schema: 'blitzen.same-frame-color/v1', browser: await session.send('Browser.getVersion'), ...result }));
} finally {
  await context?.close();
  await new Promise(resolve => server.close(resolve));
  await rm(dir, { recursive: true, force: true });
}
