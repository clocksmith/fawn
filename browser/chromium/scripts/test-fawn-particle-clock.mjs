// Real GPU execution under controlled model time; checks pause and equal-time frame partitions.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const resources = new URL('../resources/', import.meta.url);
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-particle-clock');
await mkdir(output, { recursive: true });
const source = await readFile(new URL('fawn-start.html', resources));
const report = { passed: false, classification: 'Chrome WebGPU pointer response; controlled model clock with actual GPU submissions, no performance or native Doe claim.', source: { file: 'fawn-start.html', sha256: createHash('sha256').update(source).digest('hex') }, cases: [], errors: [] };
const server = http.createServer(async (req, res) => {
  const file = new URL('.' + new URL(req.url, 'http://localhost').pathname, resources);
  if (!file.href.startsWith(resources.href)) { res.writeHead(403).end(); return; }
  try { res.setHeader('Content-Type', file.pathname.endsWith('.js') ? 'text/javascript' : 'text/html'); res.end(await readFile(file)); }
  catch { res.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const args = JSON.parse(process.env.FAWN_CHROME_ARGS || '["--enable-unsafe-webgpu","--use-angle=vulkan","--enable-features=Vulkan","--disable-vulkan-surface"]');
const browser = await chromium.launch({ channel: 'chrome', headless: true, args });
const origin = process.env.FAWN_TEST_ORIGIN || `http://127.0.0.1:${server.address().port}`;
report.browser = browser.version(); report.browserArgs = args; report.origin = process.env.FAWN_TEST_ORIGIN || 'local';
async function open(steps = 4) {
  const page = await browser.newPage(); page.on('pageerror', e => report.errors.push(e.message));
  await page.addInitScript(() => {
    const s = globalThis.clockProbe = { clock: 1000, callbacks: new Map(), id: 0, errors: [] };
    Object.defineProperty(performance, 'now', { value: () => s.clock });
    requestAnimationFrame = fn => { s.callbacks.set(++s.id, fn); return s.id; };
    cancelAnimationFrame = id => s.callbacks.delete(id);
    const request = GPUAdapter.prototype.requestDevice;
    GPUAdapter.prototype.requestDevice = async function(...args) {
      s.adapter = { vendor: this.info.vendor, architecture: this.info.architecture, isFallbackAdapter: this.info.isFallbackAdapter };
      s.device = await request.apply(this, args);
      s.device.addEventListener('uncapturederror', e => s.errors.push(e.error.message));
      const write = s.device.queue.writeBuffer.bind(s.device.queue);
      s.device.queue.writeBuffer = (buffer, offset, data, ...args) => {
        if (buffer.usage & GPUBufferUsage.UNIFORM) s.uniforms = Array.from(data);
        return write(buffer, offset, data, ...args);
      };
      return s.device;
    };
  });
  await page.goto(origin + '/fawn-start.html');
  try { await page.waitForFunction(() => !document.querySelector('#pause').disabled); }
  catch (error) { report.errors.push({ failure: await page.locator('#failure').textContent(), gpuErrors: await page.evaluate(() => clockProbe.errors) }); throw error; }
  await page.locator('.details').evaluate(el => { el.open = true; });
  await page.selectOption('#steps-select', String(steps));
  await page.locator('.details').evaluate(el => { el.open = false; });
  await page.locator('#view').focus(); await page.keyboard.down('Space');
  return page;
}
async function frames(page, count, hz) {
  await page.evaluate(async ({ count, hz }) => {
    const s = clockProbe;
    for (let i = 0; i < count; i++) {
      s.clock += 1000 / hz;
      const callbacks = [...s.callbacks.values()]; s.callbacks.clear();
      await Promise.all(callbacks.map(fn => fn(s.clock))); await s.device.queue.onSubmittedWorkDone();
    }
  }, { count, hz });
}
const legacyReference = 4 * (1 - 0.85 ** 6) / (1 - 0.85);
try {
  const page = await open(); await frames(page, 6, 60);
  const start = await page.evaluate(() => clockProbe.uniforms);
  assert.ok(Math.abs(start[3] - legacyReference) < 1e-5, 'Preserve the original response at its reference frame rate');
  await page.locator('#pause').evaluate(el => el.click());
  await frames(page, 1, 60);
  const frozen = await page.evaluate(() => clockProbe.uniforms);
  await page.locator('.details').evaluate(el => { el.open = true; });
  await page.selectOption('#formation-select', 'free'); await frames(page, 1, 60);
  const changed = await page.evaluate(() => clockProbe.uniforms);
  report.cases.push({ name: 'paused redraw', beforeForce: frozen[3], afterForce: changed[3], beforeTime: frozen[1], afterTime: changed[1], adapter: await page.evaluate(() => clockProbe.adapter) });
  assert.equal(changed[0], 0); assert.equal(changed[1], frozen[1]);
  assert.equal(changed[3], frozen[3], 'Paused redraw must not advance the pointer-force filter');
  await page.keyboard.up('Space');
  await page.locator('.details').evaluate(el => { el.open = false; });
  await page.locator('#restart').click(); await frames(page, 1, Infinity);
  const restarted = await page.evaluate(() => clockProbe.uniforms);
  report.cases.push({ name: 'paused reset clears gesture', beforeForce: changed[3], afterForce: restarted[3], time: restarted[1] });
  assert.ok(changed[3] > 0, 'Reset must exercise an outstanding pointer response');
  assert.equal(restarted[3], 0, 'Reset must clear the previous gesture response');
  assert.equal(restarted[1], 0); assert.equal(restarted[0], 0);
  assert.equal(await page.locator('#pause').textContent(), 'Resume');
  await page.locator('#pause').evaluate(el => el.click());
  await frames(page, 1, 60);
  assert.equal(await page.evaluate(() => clockProbe.uniforms[3]), 0, 'Resume must not restore the old gesture');
  for (const hz of [40, 60, 120]) for (const steps of [1, 8]) {
    // Compare partitions of one accepted world; reset through actual controls.
    await page.keyboard.up('Space'); await frames(page, 160, 40);
    assert.ok(await page.evaluate(() => Math.abs(clockProbe.uniforms[3]) < 1e-8));
    await page.locator('.details').evaluate(el => { el.open = true; });
    await page.selectOption('#steps-select', String(steps));
    await page.locator('.details').evaluate(el => { el.open = false; });
    await page.locator('#restart').evaluate(el => el.click());
    await frames(page, 1, Infinity);
    assert.equal(await page.evaluate(() => clockProbe.uniforms[1]), 0);
    await page.locator('#view').focus(); await page.keyboard.down('Space');
    await frames(page, hz / 10, hz);
    const values = await page.evaluate(() => clockProbe.uniforms);
    report.cases.push({ name: 'equal model time', hz, steps, time: values[1], force: values[3], expectedForce: legacyReference, adapter: await page.evaluate(() => clockProbe.adapter) });
    assert.ok(Math.abs(values[1] - 0.1) < 1e-6);
    assert.ok(Math.abs(values[3] - legacyReference) < 1e-5, JSON.stringify({ hz, steps, force: values[3], legacyReference }));
    assert.deepEqual(await page.evaluate(() => clockProbe.errors), []);
    assert.equal(await page.locator('#failure').isVisible(), false);
  }
  assert.deepEqual(report.errors, []); report.passed = true;
} catch (error) {
  report.failure = error.message;
  throw error;
} finally {
  await writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close(); await new Promise(resolve => server.close(resolve));
}
console.log(JSON.stringify(report.cases));
