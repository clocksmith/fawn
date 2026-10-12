// Source-bound fluid formation and interaction checks; no native Doe or performance claim.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const resources = new URL('../resources/', import.meta.url);
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-logo-flow/formation');
await mkdir(output, { recursive: true });
const sourceFiles = ['fawn-prismatic-fluids.html', 'fawn-formation.js'];
const identities = await Promise.all(sourceFiles.map(async file => ({ file,
  sha256: createHash('sha256').update(await readFile(new URL(file, resources))).digest('hex') })));
const report = { sourceFiles: identities, classification: 'Chrome fluid artwork; native animation frames, GPU buffer readback, emulated mobile. No model inference, mass-conservation, native Doe, or performance claim.', cases: [], errors: [], passed: false };
const server = http.createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  const file = new URL('.' + pathname, resources);
  if (!file.href.startsWith(resources.href)) { response.writeHead(403).end(); return; }
  try {
    response.setHeader('Content-Type', pathname.endsWith('.js') ? 'text/javascript'
      : pathname.endsWith('.svg') ? 'image/svg+xml' : 'text/html');
    response.end(await readFile(file));
  } catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const args = process.env.FAWN_CHROME_ARGS ? JSON.parse(process.env.FAWN_CHROME_ARGS)
  : ['--enable-unsafe-webgpu', '--use-angle=vulkan', '--enable-features=Vulkan'];
const headless = process.env.FAWN_HEADED !== '1';
const browser = await chromium.launch({ channel: 'chrome', headless, args });
report.browser = browser.version(); report.browserArgs = args; report.headless = headless;
function instrument() {
  const s = globalThis.flowProbe = { frames: 0, buffers: [], uniforms: [], errors: [], history: [] };
  const request = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function(...args) {
    s.adapter = { vendor: this.info.vendor, architecture: this.info.architecture,
      isFallbackAdapter: this.info.isFallbackAdapter };
    const device = s.device = await request.apply(this, args);
    device.addEventListener('uncapturederror', e => s.errors.push(e.error.message));
    const create = device.createBuffer.bind(device);
    device.createBuffer = descriptor => {
      const storage = Boolean(descriptor.usage & GPUBufferUsage.STORAGE);
      const buffer = create(storage ? { ...descriptor, usage: descriptor.usage | GPUBufferUsage.COPY_SRC } : descriptor);
      if (storage) {
        const entry = { buffer, label: descriptor.label, destroyed: false };
        const destroy = buffer.destroy.bind(buffer);
        buffer.destroy = () => { entry.destroyed = true; destroy(); }; s.buffers.push(entry);
      }
      return buffer;
    };
    const write = device.queue.writeBuffer.bind(device.queue);
    device.queue.writeBuffer = (buffer, offset, data, ...args) => {
      if (buffer.usage & GPUBufferUsage.UNIFORM) {
        s.uniforms = Array.from(data); s.frames++;
        s.history.push({ guide: data[11], openingForce: data[15], dt: data[8] * data[18] / 0.82 });
        if (s.frames === 1) queueMicrotask(() => document.querySelector('#pause').click());
      }
      return write(buffer, offset, data, ...args);
    };
    return device;
  };
}
async function frames(page, count) {
  const from = await page.evaluate(() => flowProbe.frames);
  await page.waitForFunction(target => flowProbe.frames >= target, from + count,
    { polling: 16, timeout: 180000 });
  await page.evaluate(() => flowProbe.device.queue.onSubmittedWorkDone());
}
async function sample(page) {
  return page.evaluate(async () => {
    const s = flowProbe, live = s.buffers.filter(b => !b.destroyed);
    async function read(buffer) {
      const result = s.device.createBuffer({ size: buffer.size, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
      const encoder = s.device.createCommandEncoder(); encoder.copyBufferToBuffer(buffer, 0, result, 0, buffer.size);
      s.device.queue.submit([encoder.finish()]); await result.mapAsync(GPUMapMode.READ);
      const values = new Float32Array(result.getMappedRange()).slice(); result.unmap(); result.destroy(); return values;
    }
    const field = await read(live.find(b => b.label === 'Fawn target field').buffer);
    const dye = await Promise.all(live.slice(-7).slice(2, 4).map(b => read(b.buffer)));
    const rows = dye.map(values => {
      let intersection = 0, union = 0, inside = 0, outside = 0, nInside = 0, max = 0;
      for (let i = 0; i < values.length; i += 4) {
        const value = Math.max(values[i], values[i + 1], values[i + 2]);
        const expected = field[i + 2] > 0.5, present = value > 0.115;
        if (expected && present) intersection++; if (expected || present) union++;
        if (expected) { inside += value; nInside++; } else outside += value;
        max = Math.max(max, value);
      }
      return { iou: intersection / union, insideMean: inside / nInside,
        outsideMean: outside / (values.length / 4 - nInside), max,
        finite: values.every(Number.isFinite), cells: values.length / 4, targetCells: nInside };
    });
    return { rows, strength: s.uniforms[11], errors: s.errors, adapter: s.adapter,
      time: s.uniforms[9], targetFinite: field.every(Number.isFinite) };
  });
}
try {
  for (const viewport of [{ width: 1280, height: 900 }, { width: 390, height: 844 }]) {
    const context = await browser.newContext({ viewport, isMobile: viewport.width < 600, hasTouch: viewport.width < 600 });
    await context.addInitScript(instrument);
    const page = await context.newPage(); page.on('pageerror', e => report.errors.push(e.message));
    const origin = process.env.FAWN_TEST_ORIGIN || `http://127.0.0.1:${server.address().port}`;
    await page.goto(origin + '/fawn-prismatic-fluids.html');
    await page.waitForFunction(() => !document.querySelector('#pause').disabled, null, { polling: 50 });
    const row = { viewport, captures: [] }; report.cases.push(row);
    const capture = async phase => {
      const name = `${viewport.width}-${phase}.png`;
      await page.screenshot({ path: path.join(output, name) }); row.captures.push(name);
    };
    await page.waitForFunction(() => document.querySelector('#pause').textContent === 'Resume');
    row.intro = await sample(page); await capture('intro');
    assert.equal(row.intro.strength, 1);
    assert.ok(row.intro.rows.every(r => r.finite && r.iou > 0.85), JSON.stringify(row.intro));
    await page.click('#pause');
    await page.waitForFunction(() => flowProbe.uniforms[11] < 0.01, null, { timeout: 30000 });
    row.dispersed = await sample(page); await capture('dispersed');
    row.openingForce = await page.evaluate(() => Math.max(...flowProbe.history.map(h => h.openingForce)));
    assert.ok(row.openingForce > 0.85, 'Opening dispersion must reach the GPU solver');
    assert.ok(row.dispersed.rows[0].iou < row.intro.rows[0].iou - 0.15, JSON.stringify(row.dispersed));
    await frames(page, 650); row.formed = await sample(page); await capture('formed');
    assert.equal(row.formed.strength, 1); assert.ok(row.formed.targetFinite);
    assert.ok(row.formed.rows.every(r => r.finite && r.iou > 0.65 && r.insideMean > r.outsideMean * 6), JSON.stringify(row.formed));
    assert.ok(row.formed.rows[0].iou > row.dispersed.rows[0].iou + 0.15);
    await page.mouse.move(viewport.width * 0.4, viewport.height * 0.52); await page.mouse.down();
    await page.mouse.move(viewport.width * 0.67, viewport.height * 0.52, { steps: 10 });
    await frames(page, 25); row.stirred = await sample(page); await capture('stirred');
    assert.ok(row.stirred.strength < 0.25, 'Stirring smoothly releases logo guidance');
    assert.ok(row.stirred.rows.some((r, i) => Math.abs(r.iou - row.formed.rows[i].iou) > 0.02));
    await page.mouse.up(); await page.evaluate(() => dispatchEvent(new Event('blur')));
    await frames(page, 650); row.recovered = await sample(page); await capture('recovered');
    assert.ok(row.recovered.rows.every(r => r.finite && r.iou > 0.65));
    row.maxBlendStep = await page.evaluate(() => Math.max(...flowProbe.history.slice(1).map((h, i) => Math.abs(h.guide - flowProbe.history[i].guide))));
    assert.ok(row.maxBlendStep < 0.12, 'Guidance must blend rather than switch');
    await page.click('#pause'); const frozen = await sample(page); await page.waitForTimeout(200);
    assert.deepEqual((await sample(page)).rows, frozen.rows); assert.equal((await sample(page)).time, frozen.time);
    await page.click('#restart'); await page.waitForTimeout(100); row.reset = await sample(page);
    assert.ok(row.reset.rows.every(r => r.iou < 0.2)); assert.equal(await page.textContent('#pause'), 'Resume');
    await page.click('.details > summary');
    await page.selectOption('#formation-select', 'free'); await page.waitForTimeout(100);
    assert.equal(await page.inputValue('#formation-select'), 'free');
    await page.click('.details > summary');
    await page.click('#pause');
    await page.waitForFunction(() => flowProbe.uniforms[11] === 0, null, { timeout: 30000 });
    assert.equal((await sample(page)).strength, 0);
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    row.passed = true; assert.deepEqual(row.recovered.errors, []);
    console.log(JSON.stringify({ viewport, formed: row.formed.rows[0].iou, recovered: row.recovered.rows[0].iou, passed: true }));
    await context.close();
  }
  assert.deepEqual(report.errors, []); report.passed = true;
} finally {
  await writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close(); await new Promise(resolve => server.close(resolve));
}
