// Browser artwork qualification: actual simulation buffers and headed captures, not native Doe proof.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const resources = new URL('../resources/', import.meta.url);
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-formation');
await mkdir(output, { recursive: true });
const files = ['fawn-start.html', 'fawn-magnetic-fluids.html', 'fawn-prismatic-fluids.html', 'fawn-formation.js'];
const report = { passed: false, classification: 'Browser WebGPU artwork; desktop and emulated mobile. No native Doe or physical fluid qualification.',
  metrics: { wave: 'IoU of height above 0.2 against logo coverage above 0.5', particles: 'Fraction of positions inside logo coverage above 0.5' },
  sourceFiles: await Promise.all(files.map(async file => ({ file, sha256: createHash('sha256').update(await readFile(new URL(file, resources))).digest('hex') }))), cases: [], errors: [] };
const server = http.createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  const file = new URL('.' + pathname, resources);
  if (!file.href.startsWith(resources.href)) { response.writeHead(403).end(); return; }
  try { response.setHeader('Content-Type', pathname.endsWith('.js') ? 'text/javascript' : 'text/html'); response.end(await readFile(file)); }
  catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = process.env.FAWN_TEST_ORIGIN || `http://127.0.0.1:${server.address().port}`;
const args = JSON.parse(process.env.FAWN_CHROME_ARGS || '["--enable-unsafe-webgpu","--use-angle=vulkan","--enable-features=Vulkan","--disable-vulkan-surface"]');
if (process.env.FAWN_TEST_ORIGIN) {
  report.servedFiles = [];
  for (const identity of report.sourceFiles) {
    const response = await fetch(origin + '/' + identity.file);
    assert.equal(response.status, 200);
    const bytes = await response.text();
    const canonical = bytes.replace('<script src="/fawn-domain-migration.js"></script>\n', '');
    assert.equal(createHash('sha256').update(canonical).digest('hex'), identity.sha256, identity.file);
    report.servedFiles.push({ file: identity.file, sha256: createHash('sha256').update(bytes).digest('hex') });
  }
  report.release = await (await fetch(origin + '/release.json')).json();
}
const browser = await chromium.launch({ channel: 'chrome', headless: process.env.FAWN_HEADED !== '1', args });
report.browser = browser.version(); report.headless = process.env.FAWN_HEADED !== '1'; report.browserArgs = args;
function instrument() {
  const s = globalThis.formationProbe = { frames: 0, buffers: [], errors: [], history: [] };
  const request = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function(...args) {
    s.adapter = { vendor: this.info.vendor, architecture: this.info.architecture, isFallbackAdapter: this.info.isFallbackAdapter };
    const device = s.device = await request.apply(this, args);
    device.addEventListener('uncapturederror', e => s.errors.push(e.error.message));
    const create = device.createBuffer.bind(device);
    device.createBuffer = descriptor => {
      const storage = Boolean(descriptor.usage & GPUBufferUsage.STORAGE);
      const buffer = create(storage ? { ...descriptor, usage: descriptor.usage | GPUBufferUsage.COPY_SRC } : descriptor);
      if (storage) {
        const entry = { buffer, label: descriptor.label, destroyed: false };
        const destroy = buffer.destroy.bind(buffer); buffer.destroy = () => { entry.destroyed = true; destroy(); };
        s.buffers.push(entry);
      }
      return buffer;
    };
    const write = device.queue.writeBuffer.bind(device.queue);
    device.queue.writeBuffer = (buffer, offset, data, ...args) => {
      if (buffer.usage & GPUBufferUsage.UNIFORM) {
        s.uniforms = Array.from(data); s.frames++;
        s.history.push(data[11]);
        if (s.frames === 1 && location.pathname.includes('magnetic')) {
          queueMicrotask(() => document.querySelector('#pause').click());
        }
      }
      return write(buffer, offset, data, ...args);
    };
    return device;
  };
}
async function state(page, kind) {
  return page.evaluate(async kind => {
    const s = formationProbe, live = s.buffers.filter(b => !b.destroyed);
    async function read(buffer) {
      const result = s.device.createBuffer({ size: buffer.size, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
      const encoder = s.device.createCommandEncoder(); encoder.copyBufferToBuffer(buffer, 0, result, 0, buffer.size);
      s.device.queue.submit([encoder.finish()]); await result.mapAsync(GPUMapMode.READ);
      const values = new Float32Array(result.getMappedRange()).slice(); result.unmap(); result.destroy(); return values;
    }
    const values = await read(live.filter(b => !b.label?.startsWith('Fawn'))[0].buffer);
    let covered = 0, error = 0, sum = 0, square = 0;
    if (kind === 'wave') {
      const field = await read(live.find(b => b.label === 'Fawn target field').buffer);
      let intersection = 0, union = 0;
      for (let i = 0; i < values.length; i += 4) {
        const expected = field[i + 2] > 0.5, present = values[i] > 0.2;
        if (expected && present) intersection++;
        if (expected || present) union++;
        error += Math.abs(values[i] - (expected ? 0.42 : 0));
      }
      covered = intersection / Math.max(union, 1); error /= values.length / 4;
    } else {
      const grid = 384, field = FawnFormation.field(grid, document.querySelector('#view'), document.querySelector('.brand-logo'));
      for (let i = 0; i < values.length; i += 8) {
        const x = Math.max(0, Math.min(grid - 1, Math.floor((values[i] + 1) * 0.5 * grid)));
        const y = Math.max(0, Math.min(grid - 1, Math.floor((values[i + 1] + 1) * 0.5 * grid)));
        if (field[(y * grid + x) * 4 + 2] > 0.5) covered++;
        error += Math.max(0, -field[(y * grid + x) * 4 + 3]);
      }
      covered /= values.length / 8; error /= values.length / 8;
    }
    for (let i = 0; i < values.length; i++) { sum += values[i] * (1 + i % 17); square += values[i] ** 2; }
    return { covered, error, sum, square, finite: values.every(Number.isFinite),
      strength: s.uniforms[kind === 'wave' ? 11 : 8], time: s.uniforms[kind === 'wave' ? 9 : 1], adapter: s.adapter, errors: s.errors };
  }, kind);
}
async function settle(page, kind) {
  await page.waitForFunction(kind => formationProbe.uniforms?.[kind === 'wave' ? 11 : 8] === 1, kind, { timeout: 120000 });
  await page.waitForTimeout(2500);
}
async function mode(page, value) {
  await page.locator('.details').evaluate(el => { el.open = true; });
  await page.selectOption('#formation-select', value);
  await page.locator('.details').evaluate(el => { el.open = false; });
}
try {
  for (const kind of ['wave', 'particles']) for (const viewport of [{ width: 1280, height: 900 }, { width: 390, height: 844 }]) {
    const context = await browser.newContext({ viewport, isMobile: viewport.width < 600, hasTouch: viewport.width < 600 });
    await context.addInitScript(instrument);
    const page = await context.newPage(); page.on('pageerror', e => report.errors.push(e.message));
    await page.goto(origin + (kind === 'wave' ? '/fawn-magnetic-fluids.html' : '/fawn-start.html'));
    await page.waitForFunction(() => !document.querySelector('#formation-select').disabled);
    const row = { kind, viewport }; report.cases.push(row);
    const capture = phase => page.screenshot({ path: path.join(output, `${kind}-${viewport.width}-${phase}.png`) });
    if (kind === 'wave') {
      await page.waitForFunction(() => document.querySelector('#pause').textContent === 'Resume');
      row.intro = await state(page, kind); await capture('intro');
      assert.equal(row.intro.strength, 1); assert.ok(row.intro.covered > 0.90);
      row.anchorCoverage = await page.evaluate(() => {
        const field = FawnFormation.field(96, document.querySelector('#view'), document.querySelector('.brand-logo'));
        const sources = formationProbe.uniforms.slice(12);
        let covered = 0;
        for (let i = 0; i < sources.length; i += 4) {
          const x = Math.min(95, Math.floor(sources[i] * 96)), y = Math.min(95, Math.floor(sources[i + 1] * 96));
          if (field[(y * 96 + x) * 4 + 2] > 0.5) covered++;
        }
        return covered / (sources.length / 4);
      });
      assert.equal(row.anchorCoverage, 1, 'Highlights originate inside the Fawn shape');
      await page.click('#pause');
      await page.waitForFunction(() => formationProbe.uniforms[11] < 0.01, null, { timeout: 30000 });
    }
    row.initial = await state(page, kind); await capture('initial');
    await settle(page, kind); row.formed = await state(page, kind); await capture('formed');
    if (kind === 'particles') {
      await page.waitForTimeout(350); row.wandering = await state(page, kind);
      assert.notEqual(row.wandering.sum, row.formed.sum, 'Settled particles keep moving');
      assert.ok(row.wandering.finite && row.wandering.covered > 0.90);
    }
    assert.ok(row.formed.finite && row.formed.covered > 0.90, JSON.stringify(row.formed));
    assert.ok(row.formed.covered > row.initial.covered + (kind === 'wave' ? 0.10 : 0.5));
    await page.mouse.move(viewport.width * 0.40, viewport.height * 0.52); await page.mouse.down();
    await page.mouse.move(viewport.width * 0.67, viewport.height * 0.52, { steps: 12 });
    await page.waitForTimeout(1200); row.stirred = await state(page, kind); await capture('stirred');
    assert.ok(row.stirred.strength < 0.001); assert.ok(Math.abs(row.stirred.sum - row.formed.sum) > 1);
    await page.mouse.up(); await page.evaluate(() => dispatchEvent(new Event('blur')));
    await settle(page, kind); row.recovered = await state(page, kind); await capture('recovered');
    assert.ok(row.recovered.finite && row.recovered.covered > 0.90, JSON.stringify(row.recovered));
    if (kind === 'wave') {
      row.maxBlendStep = await page.evaluate(() => Math.max(...formationProbe.history.slice(1).map((v, i) => Math.abs(v - formationProbe.history[i]))));
      assert.ok(row.maxBlendStep < 0.12, 'Wave guidance blends continuously');
    }
    await page.click('#pause'); await page.waitForTimeout(100); const frozen = await state(page, kind);
    await page.waitForTimeout(200); assert.deepEqual(await state(page, kind), frozen);
    await mode(page, 'free'); await page.waitForTimeout(100); const switched = await state(page, kind);
    assert.equal(switched.strength, kind === 'wave' ? frozen.strength : 0);
    assert.equal(switched.sum, frozen.sum); assert.equal(switched.time, frozen.time);
    assert.equal(await page.textContent('#pause'), 'Resume');
    await page.click('#pause');
    await page.waitForFunction(kind => formationProbe.uniforms[kind === 'wave' ? 11 : 8] === 0, kind, { timeout: 30000 });
    await page.waitForTimeout(100); row.free = await state(page, kind);
    assert.equal(row.free.strength, 0); assert.notEqual(row.free.sum, switched.sum);
    await page.click('#pause'); await page.click('#restart'); await page.waitForTimeout(150);
    assert.equal(await page.textContent('#pause'), 'Resume');
    await mode(page, 'branded'); await page.setViewportSize({ width: viewport.width - 40, height: viewport.height - 50 });
    await page.waitForTimeout(150); assert.equal((await state(page, kind)).strength, 0);
    await page.click('#pause'); await settle(page, kind); row.resized = await state(page, kind);
    assert.ok(row.resized.finite && row.resized.covered > 0.90);
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    assert.deepEqual(row.resized.errors, []);
    // The chosen mode is retained when switching to either other background.
    await mode(page, 'free'); await page.goto(origin + '/fawn-prismatic-fluids.html');
    await page.waitForFunction(() => !document.querySelector('#formation-select').disabled);
    assert.equal(await page.inputValue('#formation-select'), 'free');
    row.passed = true; console.log(JSON.stringify({ kind, viewport, formed: row.formed.covered, recovered: row.recovered.covered, passed: true }));
    await context.close();
  }
  for (const file of files.filter(f => f.endsWith('.html'))) {
    const context = await browser.newContext({ reducedMotion: 'reduce', viewport: { width: 390, height: 844 } });
    const page = await context.newPage(); page.on('pageerror', e => report.errors.push(e.message));
    await page.goto(origin + '/' + file); await page.waitForFunction(() => !document.querySelector('#formation-select').disabled);
    assert.equal(await page.textContent('#pause'), 'Resume');
    await mode(page, 'free'); await page.click('#restart'); assert.equal(await page.textContent('#pause'), 'Resume');
    await context.close();
  }
  assert.deepEqual(report.errors, []); report.passed = true;
} finally {
  await writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close(); await new Promise(resolve => server.close(resolve));
}
