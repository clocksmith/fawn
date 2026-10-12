// Actual Chrome WebGPU canvas and failure regressions; not forced-Doe evidence.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import http from 'node:http';
import { resolve } from 'node:path';

const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const baselineRef = '3d3b9c30b7939828d8ce07958475449db1fde25b';
const output = resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-canvas-lifecycle');
const names = ['start', 'magnetic-fluids', 'prismatic-fluids'];
const browserArgs = process.env.FAWN_CHROME_ARGS ? JSON.parse(process.env.FAWN_CHROME_ARGS)
  : ['--enable-unsafe-webgpu', '--use-angle=vulkan', '--enable-features=Vulkan', '--disable-vulkan-surface'];
const sources = {};
for (const name of names) {
  const file = `browser/chromium/resources/fawn-${name}.html`;
  sources[name] = {
    baseline: execFileSync('git', ['show', `${baselineRef}:${file}`], { encoding: 'utf8' }),
    candidate: await readFile(file, 'utf8'),
  };
}
const report = {
  classification: 'Chrome WebGPU application correctness; emulated viewports; no Doe or performance claim',
  observedAt: new Date().toISOString(), baselineRef, browserArgs, passed: false, cases: [],
  sourceHashes: Object.fromEntries(names.map(name => [name, Object.fromEntries(
    Object.entries(sources[name]).map(([variant, source]) =>
      [variant, createHash('sha256').update(source).digest('hex')]))])),
};
await mkdir(output, { recursive: true });
const server = http.createServer(async (request, response) => {
  const [, variant, filename] = new URL(request.url, 'http://localhost').pathname.split('/');
  if (filename === 'fawn-flow-logo.js') {
    response.setHeader('Content-Type', 'text/javascript');
    response.end(await readFile('browser/chromium/resources/fawn-flow-logo.js')); return;
  }
  if (filename === 'fawn-icon.svg') {
    response.setHeader('Content-Type', 'image/svg+xml');
    response.end(await readFile('browser/chromium/resources/fawn-icon.svg')); return;
  }
  const name = filename?.replace(/^fawn-/, '').replace(/\.html$/, '');
  const source = sources[name]?.[variant];
  if (!source) { response.writeHead(404).end(); return; }
  response.setHeader('Content-Type', 'text/html'); response.end(source);
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
let browser;

function instrument(injectInitializationFailure) {
  const s = window.canvasTest = { callbacks: new Map(), next: 0, clock: 1000,
    errors: [], destroyed: 0, configurations: 0, unconfigurations: 0, submissions: 0 };
  requestAnimationFrame = callback => { s.callbacks.set(++s.next, callback); return s.next; };
  cancelAnimationFrame = id => s.callbacks.delete(id);
  const configure = GPUCanvasContext.prototype.configure;
  GPUCanvasContext.prototype.configure = function (...args) {
    s.configurations++; return configure.apply(this, args);
  };
  const unconfigure = GPUCanvasContext.prototype.unconfigure;
  GPUCanvasContext.prototype.unconfigure = function () {
    s.unconfigurations++; return unconfigure.call(this);
  };
  const request = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function (...args) {
    s.adapter = this;
    s.adapterInfo = { vendor: this.info.vendor, architecture: this.info.architecture,
      isFallbackAdapter: this.info.isFallbackAdapter };
    const device = s.device = await request.apply(this, args);
    const destroy = device.destroy.bind(device);
    device.destroy = () => { s.destroyed++; destroy(); };
    device.addEventListener('uncapturederror', event => s.errors.push(event.error.message));
    const submit = device.queue.submit.bind(device.queue);
    device.queue.submit = (...args) => { s.submissions++; return submit(...args); };
    if (injectInitializationFailure) {
      device.createBuffer = () => { throw new Error('Injected initialization failure'); };
    }
    return device;
  };
}

async function open(name, variant, viewport, deviceScaleFactor = 1, inject = false) {
  // Separate browsers avoid cross-case device teardown affecting another case.
  await browser?.close();
  browser = await chromium.launch({ channel: 'chrome', headless: true,
    args: browserArgs });
  report.browser = browser.version();
  const context = await browser.newContext({ viewport, deviceScaleFactor, reducedMotion: 'reduce' });
  await context.addInitScript(instrument, inject);
  const page = await context.newPage();
  page.setDefaultTimeout(15000);
  page.on('pageerror', error => { report.pageErrors ??= []; report.pageErrors.push(error.message); });
  await page.goto(`http://127.0.0.1:${server.address().port}/${variant}/fawn-${name}.html`);
  for (let attempt = 0; attempt < 3; attempt++) {
    await page.waitForFunction(() => !document.querySelector('#pause').disabled || !document.querySelector('#failure').hidden,
      null, { polling: 50 });
    const failure = await page.locator('#failure').textContent();
    if (!failure.startsWith('No adapter')) break;
    report.adapterFailures ??= [];
    report.adapterFailures.push({ name, variant, attempt, failure });
    if (attempt < 2) await page.reload();
  }
  return page;
}

async function tick(page) {
  await page.evaluate(async () => {
    const s = canvasTest;
    s.clock = Math.max(s.clock + 1000 / 60, performance.now());
    const callbacks = [...s.callbacks.values()]; s.callbacks.clear();
    await Promise.all(callbacks.map(callback => callback(s.clock)));
    await s.device?.queue.onSubmittedWorkDone().catch(() => {});
  });
}

async function snapshot(page) {
  return page.evaluate(() => {
    const s = canvasTest, canvas = document.querySelector('#view'), failure = document.querySelector('#failure');
    return { width: canvas.width, height: canvas.height, cssWidth: canvas.clientWidth, cssHeight: canvas.clientHeight,
      limit: s.device?.limits.maxTextureDimension2D, configurations: s.configurations,
      unconfigurations: s.unconfigurations, submissions: s.submissions, destroyed: s.destroyed,
      errors: s.errors, failure: failure.hidden ? null : failure.textContent,
      controlsDisabled: [...document.querySelectorAll('button,select')].every(el => el.disabled),
      adapter: s.adapterInfo };
  });
}

function assertHealthy(result) {
  assert.equal(result.failure, null);
  assert.deepEqual(result.errors, []);
  assert.ok(result.submissions > 0);
  assert.ok(result.configurations > 0);
  assert.equal(result.destroyed, 0);
  assert.ok(result.width <= result.limit && result.height <= result.limit);
  // Flooring either dimension can contribute less than one pixel of error.
  assert.ok(Math.abs(result.width * result.cssHeight - result.height * result.cssWidth)
    < result.cssWidth + result.cssHeight);
}

try {
  for (const variant of ['baseline', 'candidate']) {
    let page = await open('start', variant, { width: 300, height: 150 });
    await tick(page);
    let result = await snapshot(page);
    report.cases.push({ name: 'default canvas initialization', variant, result });
    if (variant === 'baseline') {
      assert.match(result.failure, /colorAttachments|undefined/);
      assert.equal(result.configurations, 0);
    } else assertHealthy(result);

    for (const name of names) {
      // The device's default maximum is tested, not the adapter's higher limit.
      page = await open(name, variant, { width: 4500, height: 200 }, 3);
      await tick(page);
      if (variant === 'baseline') await page.waitForFunction(() => !document.querySelector('#failure').hidden,
        null, { polling: 50 });
      result = await snapshot(page);
      report.cases.push({ name: `${name} high-DPR limit and controls`, variant, result });
      if (variant === 'baseline') {
        assert.ok(result.width > result.limit);
        assert.ok(result.errors.some(error => /maximum texture size/.test(error)));
      } else {
        assertHealthy(result);
        await page.setViewportSize({ width: 200, height: 4500 });
        await page.waitForFunction(() => document.querySelector('#view').height > document.querySelector('#view').width,
          null, { polling: 50 });
        await tick(page);
        const portrait = await snapshot(page); assertHealthy(portrait);
        report.cases.push({ name: `${name} tall high-DPR resize`, variant, result: portrait });
        await page.setViewportSize({ width: 390, height: 844 });
        await page.waitForFunction(expected => document.querySelector('#view').width === expected,
          name === 'start' ? 780 : 1170, { polling: 50 });
        await tick(page); assertHealthy(await snapshot(page));
        await page.locator('#restart').click(); await tick(page);
        await page.locator('#pause').click(); await tick(page);
        await page.locator('#pause').click(); await tick(page);
        const paused = await snapshot(page); assertHealthy(paused);
        await tick(page);
        assert.equal((await snapshot(page)).submissions, paused.submissions);
      }
    }

    for (const inject of [false, true]) {
      page = await open('prismatic-fluids', variant, { width: 800, height: 600 }, 1, inject);
      if (!inject) {
        await tick(page); assertHealthy(await snapshot(page));
        await page.evaluate(() => canvasTest.device.dispatchEvent(new GPUUncapturedErrorEvent('uncapturederror', {
          error: new GPUValidationError('Injected terminal validation failure'),
        })));
      }
      const failed = await snapshot(page);
      assert.ok(failed.controlsDisabled);
      assert.match(failed.failure, /Injected/);
      assert.equal(failed.destroyed, variant === 'candidate' ? 1 : 0);
      if (variant === 'candidate') {
        assert.equal(failed.unconfigurations, 1);
        await page.evaluate(() => canvasTest.device.dispatchEvent(new GPUUncapturedErrorEvent('uncapturederror', {
          error: new GPUValidationError('Secondary failure must not replace the first'),
        })));
        await page.setViewportSize({ width: 900, height: 650 }); await tick(page);
        const settled = await snapshot(page);
        assert.equal(settled.failure, failed.failure);
        assert.equal(settled.destroyed, 1);
        assert.equal(settled.configurations, failed.configurations);
        assert.equal(settled.submissions, failed.submissions);
      }
      report.cases.push({ name: inject ? 'partial initialization cleanup' : 'terminal error cleanup', variant, result: failed });
    }
    console.log(`${variant}: canvas and lifecycle checks complete`);
  }
  assert.deepEqual(report.pageErrors || [], []);
  report.passed = true;
} catch (error) {
  report.failure = String(error.stack || error);
  throw error;
} finally {
  await writeFile(`${output}/results.json`, JSON.stringify(report, null, 2) + '\n');
  await browser?.close();
  await new Promise(done => server.close(done));
}
