// Chrome application diagnostics: continuous magnet input and hosted background controls.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import http from 'node:http';
import { resolve } from 'node:path';
import vm from 'node:vm';

const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const file = 'browser/chromium/resources/fawn-magnetic-fluids.html';
const baselineRef = 'd9a3f1fc0';
const source = await readFile(file, 'utf8');
const baseline = execFileSync('git', ['show', `${baselineRef}:${file}`], { encoding: 'utf8' });
const hash = value => createHash('sha256').update(value).digest('hex');
const output = resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-magnetic-transition');
await mkdir(output, { recursive: true });
const report = { classification: 'Chrome WebGPU application diagnostics; emulated mobile viewports',
  observedAt: new Date().toISOString(), origin: process.env.FAWN_TEST_ORIGIN || 'local',
  baselineRef, sources: { baseline: hash(baseline), candidate: hash(source) },
  oracle: [], cases: [], layouts: [], errors: [], passed: false };

// Integrate acceleration independently with small RK4 steps, rather than duplicating the closed form.
function reference(position, velocity, target, duration) {
  const steps = Math.ceil(duration / .0001), dt = duration / steps;
  const acceleration = (p, v) => -36 * v - 324 * (p - target);
  for (let i = 0; i < steps; i++) {
    const a = acceleration(position, velocity);
    const v2 = velocity + a * dt / 2, a2 = acceleration(position + velocity * dt / 2, v2);
    const v3 = velocity + a2 * dt / 2, a3 = acceleration(position + v2 * dt / 2, v3);
    const v4 = velocity + a3 * dt, a4 = acceleration(position + v3 * dt, v4);
    position += dt * (velocity + 2 * v2 + 2 * v3 + v4) / 6;
    velocity += dt * (a + 2 * a2 + 2 * a3 + a4) / 6;
  }
  return [position, velocity];
}
const advance = vm.runInNewContext(source.split('<script>')[1].split('const COMPUTE_WGSL')[0]
  + '; advanceMagnet');
for (const fps of [30, 60, 120]) {
  const state = { x: .5, y: .5, strength: 0, xVelocity: 0, yVelocity: 0, strengthVelocity: 0 };
  for (let i = 0; i < fps; i++) advance(state, { x: .8, y: .2, hover: true, down: true }, 1 / fps);
  const [expected, velocity] = reference(0, 0, 1, 1);
  assert.ok(Math.abs(state.strength - expected) < 1e-9);
  assert.ok(Math.abs(state.strengthVelocity - velocity) < 1e-8);
  const [released, releaseVelocity] = reference(expected, velocity, 0, .1);
  advance(state, { x: .8, y: .2, hover: false, down: false }, .1);
  assert.ok(Math.abs(state.strength - released) < 1e-9);
  assert.ok(Math.abs(state.strengthVelocity - releaseVelocity) < 1e-8);
  report.oracle.push({ fps, expected, released, state });
}
const server = http.createServer(async (request, response) => {
  try {
    const path = new URL(request.url, 'http://localhost').pathname;
    const html = path === '/baseline.html' ? baseline
      : await readFile(resolve('browser/chromium/resources', path.slice(1)), 'utf8');
    response.setHeader('Content-Type', path.endsWith('.svg') ? 'image/svg+xml' : 'text/html');
    response.end(html);
  } catch { response.writeHead(404).end(); }
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
const origin = process.env.FAWN_TEST_ORIGIN || `http://127.0.0.1:${server.address().port}`;
let browser;
function instrument() {
  const s = window.magnetTest = { clock: 1000, callbacks: new Map(), id: 0, buffers: [], uniforms: [] };
  Object.defineProperty(performance, 'now', { value: () => s.clock });
  requestAnimationFrame = fn => { s.callbacks.set(++s.id, fn); return s.id; };
  cancelAnimationFrame = id => s.callbacks.delete(id);
  const request = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function (...args) {
    s.adapter = { vendor: this.info.vendor, architecture: this.info.architecture,
      isFallbackAdapter: this.info.isFallbackAdapter };
    const device = s.device = await request.apply(this, args);
    device.addEventListener('uncapturederror', e => s.error = e.error.message);
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
async function open(path, viewport, mobile) {
  await browser?.close();
  browser = await chromium.launch({ channel: 'chrome', headless: true,
    args: ['--enable-unsafe-webgpu', '--use-angle=vulkan', '--enable-features=Vulkan', '--disable-vulkan-surface'] });
  report.browser = browser.version();
  const context = await browser.newContext({ viewport, isMobile: mobile, hasTouch: mobile });
  await context.addInitScript(instrument);
  const page = await context.newPage(); page.on('pageerror', e => report.errors.push(e.message));
  await page.goto(origin + path);
  await page.waitForFunction(() => !document.querySelector('#pause').disabled, null, { polling: 50 });
  assert.equal(await page.evaluate(() => magnetTest.adapter.isFallbackAdapter), false);
  return { page, context };
}
async function frames(page, count) {
  await page.evaluate(async count => {
    const s = magnetTest;
    for (let i = 0; i < count; i++) {
      s.clock += 1000 / 60;
      const callbacks = [...s.callbacks.values()]; s.callbacks.clear();
      await Promise.all(callbacks.map(fn => fn(s.clock)));
      await s.device.queue.onSubmittedWorkDone();
    }
  }, count);
}
async function sample(page) {
  return page.evaluate(() => ({ uniforms: magnetTest.uniforms, adapter: magnetTest.adapter,
    error: magnetTest.error, callbacks: magnetTest.callbacks.size }));
}
async function dispatch(page, type, pointerType, x = .75, y = .3) {
  await page.evaluate(({ type, pointerType, x, y }) => document.querySelector('#view')
    .dispatchEvent(new PointerEvent(type, { bubbles: true, pointerId: 1, pointerType,
      clientX: innerWidth * x, clientY: innerHeight * y })), { type, pointerType, x, y });
}
try {
  if (!process.env.FAWN_TEST_ORIGIN) {
    const { page, context } = await open('/baseline.html', { width: 800, height: 600 }, false);
    await frames(page, 1); await dispatch(page, 'pointerenter', 'mouse'); await frames(page, 1);
    const hover = (await sample(page)).uniforms[6];
    await page.mouse.move(600, 180); await page.mouse.down(); await frames(page, 1);
    const pressed = (await sample(page)).uniforms[6];
    assert.equal(hover, Math.fround(.55)); assert.equal(pressed, 1);
    report.baseline = { hover, pressed, verdict: 'instant jump reproduced' };
    await page.mouse.up(); await context.close();
  }
  for (const [name, viewport, mobile] of [
    ['desktop', { width: 1440, height: 900 }, false], ['mobile', { width: 390, height: 844 }, true],
  ]) {
    const { page, context } = await open('/fawn-magnetic-fluids.html', viewport, mobile);
    await page.locator('.details > summary').click();
    await page.locator('#grid-select').selectOption('256');
    await page.locator('.details > summary').click();
    await frames(page, 1);
    const cdp = await context.newCDPSession(page);
    if (mobile) await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart',
      touchPoints: [{ x: viewport.width * .75, y: viewport.height * .3 }] });
    else { await page.mouse.move(viewport.width * .75, viewport.height * .3); await page.mouse.down(); }
    await frames(page, 1); const first = await sample(page);
    assert.ok(first.uniforms[6] > 0 && first.uniforms[6] < .1, 'first press frame is intermediate');
    assert.ok(first.uniforms[4] > .5 && first.uniforms[4] < .75, 'magnet position interpolates');
    await frames(page, 30); const held = await sample(page); assert.ok(held.uniforms[6] > .99);
    if (mobile) await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    else await page.mouse.up();
    await frames(page, 1); const release = await sample(page);
    assert.ok(release.uniforms[6] > .9 && release.uniforms[6] < held.uniforms[6]);
    await frames(page, 60); const settled = await sample(page);
    assert.ok(Math.abs(settled.uniforms[6] - (mobile ? 0 : .55)) < 1e-6);
    if (mobile) await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart',
      touchPoints: [{ x: viewport.width * .75, y: viewport.height * .3 }] });
    else await page.mouse.down();
    await frames(page, 12);
    await dispatch(page, 'pointercancel', mobile ? 'touch' : 'mouse'); await frames(page, 1);
    const cancelled = await sample(page); assert.ok(cancelled.uniforms[6] > .1);
    await frames(page, 60); assert.ok((await sample(page)).uniforms[6] < 1e-6);
    if (mobile) await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    else await page.mouse.up();
    const field = await page.evaluate(async () => {
      const s = magnetTest, b = s.buffers.at(-2);
      const read = s.device.createBuffer({ size: b.size, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
      const encoder = s.device.createCommandEncoder(); encoder.copyBufferToBuffer(b, 0, read, 0, b.size);
      s.device.queue.submit([encoder.finish()]); await read.mapAsync(GPUMapMode.READ);
      const values = new Float32Array(read.getMappedRange());
      const result = { finite: values.every(Number.isFinite), maxHeight: 0, changedCells: 0 };
      for (let i = 0; i < values.length; i += 4) {
        result.maxHeight = Math.max(result.maxHeight, values[i]); if (values[i] !== 0) result.changedCells++;
      }
      read.unmap(); read.destroy(); return result;
    });
    assert.equal(field.finite, true); assert.ok(field.maxHeight > 0 && field.changedCells > 1000);
    await page.locator('#pause').click(); const frozen = await sample(page);
    await frames(page, 20); assert.deepEqual(await sample(page), frozen);
    await page.locator('#pause').click(); await frames(page, 1);
    assert.equal((await sample(page)).error, undefined);
    report.cases.push({ name, viewport, first, held, release, settled, cancelled, field });
    await cdp.detach(); await context.close();
  }
  for (const name of ['start', 'magnetic-fluids', 'prismatic-fluids']) {
    const { page, context } = await open(`/fawn-${name}.html`, { width: 320, height: 568 }, true);
    assert.equal(await page.locator('.intro').count(), 0);
    assert.equal(await page.locator('.demo-switcher > summary').textContent(), 'Backgrounds');
    await page.locator('.demo-switcher > summary').click();
    const box = await page.locator('.demo-menu').boundingBox();
    assert.ok(box.x >= 0 && box.x + box.width <= 320 && box.y + box.height <= 568);
    assert.equal(await page.locator('.demo-menu a').count(), 3);
    report.layouts.push({ name, viewport: { width: 320, height: 568 }, menuBox: box });
    await context.close();
  }
  assert.deepEqual(report.errors, []); report.passed = true;
  console.log('Magnet continuity, independent ODE oracle, GPU fluid state, desktop/touch release, pause and background menu passed');
} catch (error) { report.error = error.stack; throw error; }
finally {
  await writeFile(`${output}/results.json`, JSON.stringify(report, null, 2) + '\n');
  await browser?.close(); await new Promise(done => server.close(done));
}
