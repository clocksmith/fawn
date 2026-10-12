// Independent render oracle: inject a circular state, then measure its actual screen pixels.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const resources = new URL('../resources/', import.meta.url);
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-square/projection');
await mkdir(output, { recursive: true });
const report = { passed: false, classification: 'Chrome WebGPU render projection and workload controls; injected circular oracle, desktop and emulated mobile; no physical or native Doe claim.', cases: [], errors: [], sources: [] };
const names = { flow: 'fawn-prismatic-fluids.html', wave: 'fawn-magnetic-fluids.html', particles: 'fawn-start.html' };
for (const file of [...Object.values(names), 'fawn-formation.js']) report.sources.push({ file, sha256: createHash('sha256').update(await readFile(new URL(file, resources))).digest('hex') });
const server = http.createServer(async (req, res) => {
  const file = new URL('.' + new URL(req.url, 'http://localhost').pathname, resources);
  if (!file.href.startsWith(resources.href)) { res.writeHead(403).end(); return; }
  try { res.setHeader('Content-Type', file.pathname.endsWith('.js') ? 'text/javascript' : 'text/html'); res.end(await readFile(file)); }
  catch { res.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = process.env.FAWN_TEST_ORIGIN || `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({ channel: 'chrome', headless: process.env.FAWN_HEADED !== '1', args: JSON.parse(process.env.FAWN_CHROME_ARGS || '["--enable-unsafe-webgpu","--use-angle=vulkan","--enable-features=Vulkan","--disable-vulkan-surface"]') });
report.headless = process.env.FAWN_HEADED !== '1'; report.browser = browser.version();
function instrument() {
  const s = globalThis.squareProbe = { buffers: [], frames: [], dispatches: 0, errors: [] };
  const request = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function(...args) {
    s.adapter = { vendor: this.info.vendor, architecture: this.info.architecture, isFallbackAdapter: this.info.isFallbackAdapter };
    const device = s.device = await request.apply(this, args);
    device.addEventListener('uncapturederror', e => s.errors.push(e.error.message));
    const create = device.createBuffer.bind(device);
    device.createBuffer = descriptor => {
      const buffer = create(descriptor);
      if (descriptor.usage & GPUBufferUsage.STORAGE) {
        const entry = { buffer, label: descriptor.label, destroyed: false };
        const destroy = buffer.destroy.bind(buffer); buffer.destroy = () => { entry.destroyed = true; destroy(); }; s.buffers.push(entry);
      }
      return buffer;
    };
    const write = device.queue.writeBuffer.bind(device.queue);
    device.queue.writeBuffer = (buffer, offset, data, ...args) => {
      if (buffer.usage & GPUBufferUsage.UNIFORM) {
        if (s.uniforms) s.frames.push({ dispatches: s.dispatches - s.previousDispatches, values: s.uniforms });
        s.previousDispatches = s.dispatches; s.uniforms = Array.from(data);
      }
      return write(buffer, offset, data, ...args);
    };
    const dispatch = GPUComputePassEncoder.prototype.dispatchWorkgroups;
    GPUComputePassEncoder.prototype.dispatchWorkgroups = function(...args) { s.dispatches++; return dispatch.apply(this, args); };
    return device;
  };
}
try {
  for (const kind of Object.keys(names)) for (const viewport of [{ width: 1600, height: 500 }, { width: 390, height: 844 }, { width: 900, height: 900 }]) {
    const context = await browser.newContext({ viewport, isMobile: viewport.width < 600, hasTouch: viewport.width < 600 });
    await context.addInitScript(instrument); const page = await context.newPage();
    page.on('pageerror', e => report.errors.push(e.message));
    await page.goto(origin + '/' + names[kind]); await page.waitForFunction(() => !document.querySelector('#pause').disabled);
    await page.locator('.details').evaluate(el => { el.open = true; });
    assert.deepEqual(await page.locator('.settings label').evaluateAll(labels => labels.map(label => label.firstChild.textContent)), ['Formation', 'Count', 'Steps']);
    assert.deepEqual(await page.locator('#steps-select option').allTextContents(), ['1', '2', '4', '8']);
    await page.selectOption('#formation-select', 'free');
    for (const steps of [1, 8]) {
      await page.selectOption('#steps-select', String(steps));
      await page.evaluate(() => { squareProbe.frames = []; });
      await page.waitForFunction(() => squareProbe.frames.length > 4);
      const frames = await page.evaluate(() => squareProbe.frames.slice(-3));
      assert.ok(frames.every(frame => frame.dispatches === steps * (kind === 'flow' ? 16 : 1)), JSON.stringify({kind, steps, frames}));
    }
    await page.click('#pause'); await page.selectOption('#count-select', '16384');
    assert.equal(await page.textContent('#pause'), 'Resume');
    await page.locator('.details').evaluate(el => { el.open = false; });
    await page.waitForTimeout(100);
    const row = { kind, viewport, adapter: await page.evaluate(() => squareProbe.adapter) }; report.cases.push(row);
    row.oracle = await page.evaluate(kind => {
      const s = squareProbe, live = s.buffers.filter(b => !b.destroyed && !b.label?.startsWith('Fawn'));
      const count = Number(document.querySelector('#count-select').value), grid = Math.sqrt(count);
      const buffers = kind === 'flow' ? live.slice(2, 4) : live;
      for (const entry of buffers) {
        const values = new Float32Array(entry.buffer.size / 4);
        if (kind === 'particles') {
          if (values.length !== count * 8) throw Error('Particle count differs from buffer capacity');
          for (let i = 0; i < count; i++) {
            const angle = i / count * Math.PI * 2;
            values.set([Math.cos(angle) * 0.16, Math.sin(angle) * 0.16, 0, 0, 1, 0.65, 0.2, 0.002], i * 8);
          }
        } else {
          if (values.length !== count * 4) throw Error('Cell count differs from buffer capacity');
          for (let y = 0; y < grid; y++) for (let x = 0; x < grid; x++) {
            const inside = Math.hypot((x + 0.5) / grid - 0.5, (y + 0.5) / grid - 0.5) < 0.08;
            if (inside) values.set(kind === 'flow' ? [0.9, 0.08, 0.12, 1] : [0.7, 0, 1, 2], (y * grid + x) * 4);
          }
        }
        s.device.queue.writeBuffer(entry.buffer, 0, values);
      }
      document.querySelector('#formation-select').dispatchEvent(new Event('change'));
      return { count, grid, canvas: [document.querySelector('#view').clientWidth, document.querySelector('#view').clientHeight] };
    }, kind);
    await page.waitForTimeout(150);
    const filename = `${kind}-${viewport.width}x${viewport.height}-circle.png`;
    const screenshot = await page.screenshot({ path: path.join(output, filename) });
    row.pixels = await page.evaluate(async base64 => {
      const bitmap = await createImageBitmap(await (await fetch('data:image/png;base64,' + base64)).blob());
      const canvas = document.createElement('canvas'); canvas.width = bitmap.width; canvas.height = bitmap.height;
      const ctx = canvas.getContext('2d'); ctx.drawImage(bitmap, 0, 0);
      const { width, height } = canvas, pixels = ctx.getImageData(0, 0, width, height).data;
      const margin = Math.max(width, height) * 0.10 + 8;
      let x0 = width, y0 = height, x1 = 0, y1 = 0, count = 0;
      for (let y = Math.floor(height / 2 - margin); y < height / 2 + margin; y++) for (let x = Math.floor(width / 2 - margin); x < width / 2 + margin; x++) {
        const i = (y * width + x) * 4;
        if (Math.max(pixels[i], pixels[i + 1], pixels[i + 2]) > 45) {
          x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y); count++;
        }
      }
      return { width: x1 - x0 + 1, height: y1 - y0 + 1, count };
    }, screenshot.toString('base64'));
    assert.ok(row.pixels.count > 50, JSON.stringify(row));
    const diameter = Math.max(viewport.width, viewport.height) * 0.16;
    assert.ok(Math.abs(row.pixels.width - diameter) < Math.max(8, diameter * 0.06), JSON.stringify(row));
    assert.ok(Math.abs(row.pixels.height - diameter) < Math.max(8, diameter * 0.06), JSON.stringify(row));
    assert.ok(Math.abs(row.pixels.width - row.pixels.height) <= Math.max(5, row.pixels.width * 0.04), JSON.stringify(row));
    assert.deepEqual(await page.evaluate(() => squareProbe.errors), []);
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    row.capture = filename; row.passed = true; console.log(JSON.stringify(row)); await context.close();
  }
  assert.deepEqual(report.errors, []); report.passed = true;
} finally {
  await writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close(); await new Promise(resolve => server.close(resolve));
}
