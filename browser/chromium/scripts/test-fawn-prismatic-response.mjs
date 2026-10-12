// Chrome application diagnostics for immediate prismatic drag and press response.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import http from 'node:http';
import { resolve } from 'node:path';

const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const file = 'browser/chromium/resources/fawn-prismatic-fluids.html';
const baselineRef = 'e365eee31';
const sources = { baseline: execFileSync('git', ['show', `${baselineRef}:${file}`], { encoding: 'utf8' }),
  candidate: await readFile(file, 'utf8') };
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const output = resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-prismatic-response');
await mkdir(output, { recursive: true });
const report = { classification: 'Chrome application correctness; emulated mobile; no performance claim',
  observedAt: new Date().toISOString(), origin: process.env.FAWN_TEST_ORIGIN || 'local', baselineRef,
  sourceHashes: Object.fromEntries(Object.entries(sources).map(([name, source]) => [name, hash(source)])),
  cases: [], errors: [], passed: false };
const server = http.createServer(async (request, response) => {
  const path = new URL(request.url, 'http://localhost').pathname;
  if (path.endsWith('/fawn-flow-logo.js')) {
    response.setHeader('Content-Type', 'text/javascript');
    response.end(await readFile('browser/chromium/resources/fawn-flow-logo.js')); return;
  }
  if (path.endsWith('.svg')) {
    response.setHeader('Content-Type', 'image/svg+xml');
    response.end(await readFile('browser/chromium/resources/fawn-icon.svg')); return;
  }
  const variant = path.split('/')[1];
  if (!sources[variant]) { response.writeHead(404).end(); return; }
  response.setHeader('Content-Type', 'text/html'); response.end(sources[variant]);
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
let browser;
function instrument() {
  const s = window.responseTest = { clock: 1000, callbacks: new Map(), id: 0, buffers: [], uniforms: [], errors: [] };
  Object.defineProperty(performance, 'now', { value: () => s.clock });
  requestAnimationFrame = fn => { s.callbacks.set(++s.id, fn); return s.id; };
  cancelAnimationFrame = id => s.callbacks.delete(id);
  const request = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function (...args) {
    s.adapter = { vendor: this.info.vendor, architecture: this.info.architecture,
      isFallbackAdapter: this.info.isFallbackAdapter };
    const device = s.device = await request.apply(this, args);
    device.addEventListener('uncapturederror', e => s.errors.push(e.error.message));
    const create = device.createBuffer.bind(device);
    device.createBuffer = d => {
      const storage = Boolean(d.usage & GPUBufferUsage.STORAGE);
      const b = create(storage ? { ...d, usage: d.usage | GPUBufferUsage.COPY_SRC } : d);
      if (storage) s.buffers.push(b); return b;
    };
    const write = device.queue.writeBuffer.bind(device.queue);
    device.queue.writeBuffer = (b, o, data, ...args) => {
      if (b.usage & GPUBufferUsage.UNIFORM) s.uniforms = Array.from(data);
      return write(b, o, data, ...args);
    };
    return device;
  };
}
async function open(variant, viewport, mobile) {
  await browser?.close();
  browser = await chromium.launch({ channel: 'chrome', headless: true,
    args: ['--enable-unsafe-webgpu', '--use-angle=vulkan', '--enable-features=Vulkan', '--disable-vulkan-surface'] });
  report.browser = browser.version();
  const context = await browser.newContext({ viewport, isMobile: mobile, hasTouch: mobile });
  await context.addInitScript(instrument);
  const page = await context.newPage(); page.on('pageerror', e => report.errors.push(e.message));
  const origin = `http://127.0.0.1:${server.address().port}/${variant}/fawn-prismatic-fluids.html`;
  await page.goto(process.env.FAWN_TEST_ORIGIN ? `${process.env.FAWN_TEST_ORIGIN}/fawn-prismatic-fluids.html` : origin);
  await page.waitForFunction(() => !document.querySelector('#pause').disabled, null, { polling: 50 });
  assert.equal(await page.evaluate(() => responseTest.adapter.isFallbackAdapter), false);
  return { page, context };
}
async function frames(page, count) {
  await page.evaluate(async count => {
    const s = responseTest;
    for (let i = 0; i < count; i++) {
      s.clock += 1000 / 60;
      const callbacks = [...s.callbacks.values()]; s.callbacks.clear();
      await Promise.all(callbacks.map(fn => fn(s.clock))); await s.device.queue.onSubmittedWorkDone();
    }
  }, count);
}
async function sample(page, stroke = false) {
  return page.evaluate(async stroke => {
    const s = responseTest, buffers = s.buffers.slice(-7);
    const stats = [];
    for (const b of buffers.slice(0, 4)) {
      const read = s.device.createBuffer({ size: b.size, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
      const encoder = s.device.createCommandEncoder(); encoder.copyBufferToBuffer(b, 0, read, 0, b.size);
      s.device.queue.submit([encoder.finish()]); await read.mapAsync(GPUMapMode.READ);
      const values = new Float32Array(read.getMappedRange());
      const row = { finite: values.every(Number.isFinite), maxSpeed: 0, maxDye: 0, strokeMinSpeed: Infinity };
      for (let i = 0; i < values.length; i += 4) {
        row.maxSpeed = Math.max(row.maxSpeed, Math.hypot(values[i], values[i + 1]));
        row.maxDye = Math.max(row.maxDye, values[i], values[i + 1], values[i + 2]);
      }
      if (stroke) {
        const grid = s.uniforms[0];
        for (const x of [.53, .56, .59, .62]) {
          const i = (Math.floor(.5 * grid) * grid + Math.floor(x * grid)) * 4;
          row.strokeMinSpeed = Math.min(row.strokeMinSpeed, Math.hypot(values[i], values[i + 1]));
        }
      } else row.strokeMinSpeed = null;
      read.unmap(); read.destroy(); stats.push(row);
    }
    return { uniforms: s.uniforms, stats, errors: s.errors, adapter: s.adapter };
  }, stroke);
}
try {
  const variants = process.env.FAWN_TEST_ORIGIN ? ['candidate'] : ['baseline', 'candidate'];
  for (const [name, viewport, mobile] of [
    ['desktop', { width: 1440, height: 900 }, false], ['mobile', { width: 390, height: 844 }, true],
  ]) {
    const comparisons = {};
    for (const variant of variants) {
      const { page, context } = await open(variant, viewport, mobile);
      const cdp = await context.newCDPSession(page);
      const touch = async (type, x = .5) => cdp.send('Input.dispatchTouchEvent', { type,
        touchPoints: type === 'touchEnd' || type === 'touchCancel' ? []
          : [{ x: viewport.width * x, y: viewport.height * .5 }] });
      await frames(page, 1);
      if (mobile) await touch('touchStart');
      else { await page.mouse.move(viewport.width * .5, viewport.height * .5); await page.mouse.down(); }
      await frames(page, 1); const press = await sample(page);
      if (mobile) await touch('touchMove', .65);
      else await page.mouse.move(viewport.width * .65, viewport.height * .5);
      await frames(page, 1); const drag = await sample(page, true);
      assert.ok(Math.abs(drag.uniforms[12] - .15) < 1e-6);
      await frames(page, 1); const stopped = await sample(page);
      if (mobile) await touch('touchEnd'); else await page.mouse.up();
      await frames(page, 1); const released = await sample(page);
      if (variant === 'candidate') {
        assert.equal(stopped.uniforms[12], 0, 'movement is consumed without a synthetic drag tail');
        assert.ok(drag.stats[1].strokeMinSpeed > .25, 'the first frame affects the full fast stroke');
        assert.equal(released.uniforms[6], mobile ? 0 : Math.fround(.28));
        assert.equal(released.uniforms[14], 0);
      } else assert.ok(stopped.uniforms[12] > .1, 'preceding source replays decaying movement');
      await page.locator('#restart').click(); await frames(page, 1);
      if (!mobile) { await page.mouse.move(viewport.width * .5, viewport.height * .5); await frames(page, 1); }
      if (mobile) { await touch('touchStart'); await touch('touchEnd'); }
      else { await page.mouse.down(); await page.mouse.up(); }
      await frames(page, 1); const tap = await sample(page);
      if (variant === 'candidate') {
        assert.equal(tap.uniforms[14], 1, 'tap between frames retains its press impulse');
        assert.ok(tap.stats[1].maxSpeed > .5, 'stationary tap produces immediate fluid movement');
        await frames(page, 1); assert.equal((await sample(page)).uniforms[14], 0, 'press impulse is consumed once');
        if (mobile) {
          await touch('touchStart'); await touch('touchCancel'); await frames(page, 1);
          const cancelled = await sample(page); assert.equal(cancelled.uniforms[6], 0); assert.equal(cancelled.uniforms[14], 0);
        }
        await frames(page, 90); const later = await sample(page);
        assert.ok(later.stats.every(row => row.finite), 'prolonged fluid state stays finite');
        assert.deepEqual(later.errors, []);
      }
      const row = { name, variant, viewport, press, drag, stopped, released, tap };
      report.cases.push(row); comparisons[variant] = row;
      await cdp.detach(); await context.close();
    }
    if (comparisons.baseline) {
      assert.ok(comparisons.candidate.drag.stats[1].maxSpeed > comparisons.baseline.drag.stats[1].maxSpeed * 10);
      assert.ok(comparisons.candidate.tap.stats[1].maxSpeed > comparisons.baseline.tap.stats[1].maxSpeed * 10);
      assert.ok(comparisons.candidate.tap.stats.slice(2).some((row, i) => row.maxDye
        > comparisons.baseline.tap.stats[i + 2].maxDye * 2));
    }
  }
  assert.deepEqual(report.errors, []); report.passed = true;
  console.log('Immediate full-stroke drag, stronger stationary/quick taps, one-time impulses, release/cancel and finite GPU fluid state passed');
} catch (error) { report.error = error.stack; throw error; }
finally {
  await writeFile(`${output}/results.json`, JSON.stringify(report, null, 2) + '\n');
  await browser?.close(); await new Promise(done => server.close(done));
}
