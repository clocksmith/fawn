import assert from 'node:assert/strict';
import { readFile, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import vm from 'node:vm';
import http from 'node:http';
import path from 'node:path';
import { chromium } from '/Users/xyz/deco/reploid/node_modules/playwright/index.mjs';
const root = path.resolve('browser/chromium/resources');
const out = path.resolve('bench/out/fawn-natural-shimmer');
const report = { passed: false, classification: 'Software WebGPU visual lifecycle diagnostic at 1024 parcels; no physical GPU or browser performance qualification', sources: [], timing: {}, journey: {} };
for (const file of ['fawn-cycle.js', 'fawn-cycle-gpu.js', 'fawn-cycle-clock.js', 'fawn-formation.js', 'fawn-parcel-renderer.js', 'fawn-shaders.js']) {
  const source = await readFile(path.join(root, file));
  report.sources.push({ file, sha256: createHash('sha256').update(source).digest('hex') });
}
for (const [name, source] of [['before', execFileSync('git', ['show', '657a6abaf:browser/chromium/resources/fawn-formation.js'], { encoding: 'utf8' })], ['after', await readFile(path.join(root, 'fawn-formation.js'), 'utf8')]]) {
  const context = {}; vm.runInNewContext(source, context);
  const times = {};
  for (const interaction of [false, true]) {
    const formation = context.FawnFormation.transition(); formation.reset(true);
    if (interaction) formation.advance(1 / 120, true, true, true);
    let elapsed = 0, value = 0;
    while (value < 0.99 && elapsed < 15) { value = formation.advance(1 / 120, true, false, false); elapsed += 1 / 120; }
    assert.ok(value >= 0.99);
    times[interaction ? 'interaction99S' : 'automatic99S'] = elapsed;
  }
  report.timing[name] = times;
}
assert.ok(report.timing.after.automatic99S > report.timing.before.automatic99S);
assert.ok(report.timing.after.interaction99S / report.timing.before.interaction99S < 0.52);
const server = http.createServer(async (req, res) => {
  try {
    const name = new URL(req.url, 'http://localhost').pathname;
    res.setHeader('Content-Type', name.endsWith('.js') ? 'text/javascript' : name.endsWith('.svg') ? 'image/svg+xml' : 'text/html');
    res.end(await readFile(path.join(root, name)));
  } catch { res.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const browser = await chromium.launch({ headless: true, args: ['--enable-unsafe-webgpu', '--use-angle=swiftshader'] });
try {
  const page = await browser.newPage({ viewport: { width: 390, height: 390 } });
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  await page.addInitScript(() => {
    const p = globalThis.probe = { clock: 1000, id: 0, callbacks: new Map(), uniforms: {}, errors: [], devices: 0 };
    Object.defineProperty(performance, 'now', { value: () => p.clock });
    requestAnimationFrame = cb => { p.callbacks.set(++p.id, cb); return p.id; };
    cancelAnimationFrame = id => p.callbacks.delete(id);
    const request = GPUAdapter.prototype.requestDevice;
    GPUAdapter.prototype.requestDevice = async function(...args) {
      const device = p.device = await request.apply(this, args); p.devices++;
      p.adapter = { vendor: this.info.vendor, architecture: this.info.architecture };
      device.addEventListener('uncapturederror', e => p.errors.push(e.error.message));
      const write = device.queue.writeBuffer.bind(device.queue);
      device.queue.writeBuffer = (buffer, offset, data, ...rest) => {
        if (buffer.usage & GPUBufferUsage.UNIFORM) p.uniforms[buffer.label] = Array.from(data);
        return write(buffer, offset, data, ...rest);
      };
      return device;
    };
  });
  await page.goto(`http://127.0.0.1:${server.address().port}/fawn-cycle.html`);
  await page.waitForFunction(() => probe.device && probe.callbacks.size, null, { polling: 50 });
  await page.locator('summary').click();
  await page.selectOption('#count-select', '1024');
  await page.selectOption('#formation-select', 'branded');
  await page.locator('summary').click();
  await page.evaluate(() => document.querySelector('#restart').click());
  async function advance(count) {
    await page.evaluate(async count => {
      for (let i = 0; i < count; i++) {
        probe.clock += 33; const callbacks = [...probe.callbacks.values()]; probe.callbacks.clear();
        callbacks.forEach(cb => cb(probe.clock));
        await probe.device.queue.onSubmittedWorkDone(); await Promise.resolve();
      }
    }, count);
  }
  const state = () => page.evaluate(() => ({ shape: probe.uniforms['Cycle parcel uniforms'][7], renderTime: probe.uniforms['Cycle parcel rendering uniforms'][7], shimmerTime: probe.uniforms['Cycle parcel rendering uniforms'][12], effect: document.querySelector('#effect').textContent }));
  await advance(280);
  const settled = await state(); assert.ok(settled.shape > 0.99);
  await advance(20);
  const resting = await state(); assert.equal(resting.renderTime, settled.renderTime); assert.ok(resting.shimmerTime > settled.shimmerTime);
  report.journey.settled = settled; report.journey.resting = resting;
  await page.screenshot({ path: path.join(out, 'branded.png') });
  await page.evaluate(() => document.querySelector('#pause').click());
  await advance(20); assert.deepEqual(await state(), resting);
  await page.evaluate(() => { document.querySelector('#pause').click(); document.querySelector('#restart').click(); });
  await page.mouse.move(195, 195); await page.mouse.down(); await advance(30);
  assert.ok((await state()).shape < 0.01);
  await page.mouse.up(); await advance(115);
  report.journey.recovered = await state(); assert.ok(report.journey.recovered.shape > 0.99);
  const effects = [report.journey.recovered.effect];
  for (let i = 0; i < 3; i++) { await advance(485); effects.push((await state()).effect); }
  assert.deepEqual(effects, ['Flows', 'Magnets', 'Particles', 'Flows']);
  report.journey.effects = effects;
  report.journey.pauseFrozen = true;
  report.journey.adapter = await page.evaluate(() => probe.adapter);
  assert.equal(await page.evaluate(() => probe.devices), 1);
  assert.deepEqual(await page.evaluate(() => probe.errors), []);
  assert.deepEqual(errors, []);
  report.passed = true;
} finally {
  await writeFile(path.join(out, 'journey.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report));
  await browser.close(); server.closeAllConnections(); server.close();
}
