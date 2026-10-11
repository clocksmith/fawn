// Application regression on Chrome WebGPU; viewport emulation is not phone hardware evidence.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import http from 'node:http';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const sourcePath = 'browser/chromium/resources/fawn-start.html';
const baselineRef = process.env.FAWN_BASELINE_REF || '3faa1940dc229aad61fda24d5042549f271b6088';
const output = resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-particle-mobile');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const sources = {
  baseline: execFileSync('git', ['show', `${baselineRef}:${sourcePath}`], { cwd: root, encoding: 'utf8' }),
  candidate: await readFile(resolve(root, sourcePath), 'utf8'),
};
await mkdir(output, { recursive: true });
const server = http.createServer((request, response) => {
  const variant = new URL(request.url, 'http://localhost').pathname.split('/')[1];
  if (!sources[variant]) { response.writeHead(404).end(); return; }
  response.setHeader('Content-Type', 'text/html'); response.end(sources[variant]);
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
const launchOptions = { channel: 'chrome', headless: true,
  args: ['--enable-unsafe-webgpu', '--use-angle=vulkan', '--enable-features=Vulkan',
    '--disable-vulkan-surface'] };
let browser = null;
const report = {
  classification: 'Chrome application correctness; emulated viewports; no forced-Doe or performance claim',
  observedAt: new Date().toISOString(), browser: null, baselineRef,
  sourceHashes: Object.fromEntries(Object.entries(sources).map(([name, html]) => [name, hash(html)])),
  method: { frameMs: 1000 / 60, cycles: 3, particleCount: 65536,
    occupiedCellWidth: 0.1, minimumOccupiedCells: 8 },
  baseline: [], cases: [], pixels: [], errors: [], passed: false,
};

function instrument() {
  const s = window.particleTest = { clock: 1000, callbacks: new Map(), id: 0,
    buffers: [], errors: [], uniforms: [], allocations: 0 };
  Object.defineProperty(performance, 'now', { value: () => s.clock });
  requestAnimationFrame = callback => { s.callbacks.set(++s.id, callback); return s.id; };
  cancelAnimationFrame = id => s.callbacks.delete(id);
  const request = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function (...args) {
    const device = await request.apply(this, args);
    s.device = device;
    s.adapter = { vendor: this.info.vendor, architecture: this.info.architecture,
      isFallbackAdapter: this.info.isFallbackAdapter };
    device.addEventListener('uncapturederror', event => s.errors.push(event.error.message));
    const create = device.createBuffer.bind(device);
    device.createBuffer = descriptor => {
      const storage = Boolean(descriptor.usage & GPUBufferUsage.STORAGE);
      const buffer = create(storage
        ? { ...descriptor, usage: descriptor.usage | GPUBufferUsage.COPY_SRC } : descriptor);
      if (storage) { s.buffers.push(buffer); s.allocations += 1; }
      return buffer;
    };
    const textures = new WeakMap();
    const createTexture = device.createTexture.bind(device);
    device.createTexture = descriptor => {
      const texture = createTexture({ ...descriptor,
        usage: descriptor.usage | GPUTextureUsage.COPY_SRC });
      const createView = texture.createView.bind(texture);
      texture.createView = (...args) => {
        const view = createView(...args); textures.set(view, texture); return view;
      };
      return texture;
    };
    const createEncoder = device.createCommandEncoder.bind(device);
    device.createCommandEncoder = (...args) => {
      const encoder = createEncoder(...args);
      const begin = encoder.beginRenderPass.bind(encoder);
      encoder.beginRenderPass = descriptor => {
        const texture = textures.get(descriptor.colorAttachments[0].view);
        if (texture) s.latestTrail = texture;
        return begin(descriptor);
      };
      return encoder;
    };
    const write = device.queue.writeBuffer.bind(device.queue);
    device.queue.writeBuffer = (buffer, offset, data, ...args) => {
      if (buffer.usage & GPUBufferUsage.UNIFORM) s.uniforms = Array.from(data);
      return write(buffer, offset, data, ...args);
    };
    return device;
  };
}
async function open(variant, viewport, mobile = false) {
  if (browser) await browser.close();
  browser = await chromium.launch(launchOptions);
  report.browser = browser.version();
  const context = await browser.newContext({ viewport, deviceScaleFactor: mobile ? 3 : 1,
    isMobile: mobile, hasTouch: mobile, reducedMotion: 'no-preference' });
  await context.addInitScript(instrument);
  const page = await context.newPage();
  page.on('pageerror', error => report.errors.push(error.message));
  await page.goto(`${process.env.FAWN_TEST_ORIGIN || `http://127.0.0.1:${server.address().port}/${variant}/`}`);
  await page.waitForFunction(() => document.querySelector('#status').textContent === 'WebGPU active');
  assert.equal(await page.evaluate(() => particleTest.adapter.isFallbackAdapter), false);
  return { context, page };
}
async function advance(page, frames) {
  await page.evaluate(async frames => {
    const s = particleTest;
    for (let i = 0; i < frames; i++) {
      s.clock += 1000 / 60;
      const callbacks = [...s.callbacks.values()]; s.callbacks.clear();
      await Promise.all(callbacks.map(callback => callback(s.clock)));
      await s.device.queue.onSubmittedWorkDone();
    }
  }, frames);
}
async function capture(page, name) {
  const png = await page.evaluate(async () => {
    const s = particleTest, device = s.device, texture = s.latestTrail;
    const width = texture.width, height = texture.height;
    const bytesPerRow = Math.ceil(width * 4 / 256) * 256;
    const read = device.createBuffer({ size: bytesPerRow * height,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const encoder = device.createCommandEncoder();
    encoder.copyTextureToBuffer({ texture }, { buffer: read, bytesPerRow }, { width, height });
    device.queue.submit([encoder.finish()]); await read.mapAsync(GPUMapMode.READ);
    const bytes = new Uint8Array(read.getMappedRange());
    const rgba = new Uint8ClampedArray(width * height * 4);
    let brightPixels = 0;
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
      const input = y * bytesPerRow + x * 4, output = (y * width + x) * 4;
      rgba[output] = bytes[input + (texture.format.startsWith('bgra') ? 2 : 0)];
      rgba[output + 1] = bytes[input + 1];
      rgba[output + 2] = bytes[input + (texture.format.startsWith('bgra') ? 0 : 2)];
      rgba[output + 3] = 255;
      if (Math.max(rgba[output], rgba[output + 1], rgba[output + 2]) > 64) brightPixels += 1;
    }
    read.unmap(); read.destroy();
    const image = document.createElement('canvas'); image.width = width; image.height = height;
    image.getContext('2d').putImageData(new ImageData(rgba, width, height), 0, 0);
    return { png: image.toDataURL('image/png').split(',')[1], width, height, brightPixels };
  });
  await writeFile(`${output}/${name}.canvas.png`, Buffer.from(png.png, 'base64'));
  if (!name.startsWith('baseline')) assert.ok(png.brightPixels > png.width * png.height * .001,
    'GPU pixels must visibly contain the particles');
  report.pixels.push({ name, width: png.width, height: png.height, brightPixels: png.brightPixels,
    sha256: hash(Buffer.from(png.png, 'base64')) });
}
async function sample(page, variant = 'candidate') {
  const result = await page.evaluate(async variant => {
    const s = particleTest, device = s.device;
    // All offered step counts are even, so the first buffer is the current output.
    const buffer = s.buffers.at(-2);
    const read = device.createBuffer({ size: buffer.size,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const encoder = device.createCommandEncoder();
    encoder.copyBufferToBuffer(buffer, 0, read, 0, buffer.size);
    device.queue.submit([encoder.finish()]);
    await read.mapAsync(GPUMapMode.READ);
    const values = new Float32Array(read.getMappedRange());
    const bytes = new Uint8Array(values.buffer).slice();
    const view = document.querySelector('#view');
    const aspect = view.width / view.height;
    let visible = 0, edge = 0;
    const cells = new Set();
    for (let i = 0; i < values.length; i += 8) {
      const x = values[i] / (variant === 'baseline' ? aspect : 1), y = values[i + 1];
      if (!Number.isFinite(x) || !Number.isFinite(y)) throw new Error('Non-finite GPU position');
      if (Math.abs(x) < 1 && Math.abs(y) < 1) visible += 1;
      if (Math.abs(x) > 0.95 || Math.abs(y) > 0.95) edge += 1;
      cells.add(`${Math.floor(x * 10)},${Math.floor(y * 10)}`);
    }
    const count = values.length / 8;
    const digest = [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))]
      .map(byte => byte.toString(16).padStart(2, '0')).join('');
    read.unmap(); read.destroy();
    return { visibleFraction: visible / count, edgeFraction: edge / count, cells: cells.size,
      count, digest, uniforms: s.uniforms, allocations: s.allocations,
      canvas: { width: view.width, height: view.height }, adapter: s.adapter, errors: s.errors };
  }, variant);
  assert.deepEqual(result.errors, []);
  return result;
}
function check(row) {
  assert.equal(row.count, 65536);
  assert.equal(row.visibleFraction, 1, 'Particles stay inside the visible canvas');
  assert.ok(row.cells >= report.method.minimumOccupiedCells, `Particle cloud collapsed: ${row.cells} cells`);
  assert.ok(row.edgeFraction < 0.5, 'Particles must not accumulate at the viewport edge');
}
try {
  if (!process.env.FAWN_TEST_ORIGIN) {
    const { page, context } = await open('baseline', { width: 390, height: 844 }, true);
    await advance(page, 12 * 60);
    const row = await sample(page, 'baseline'); report.baseline.push({ seconds: 12, ...row });
    assert.equal(row.visibleFraction, 0, 'Frozen source must reproduce the offscreen corner trap');
    assert.ok(row.cells <= 2, 'Frozen source must reproduce collapsed particle identities');
    await capture(page, 'baseline-mobile');
    await context.close();
  }
  let reference = null;
  for (const [name, viewport, mobile] of [
    ['mobile', { width: 390, height: 844 }, true],
    ['small-mobile', { width: 320, height: 568 }, true],
    ['desktop', { width: 1440, height: 900 }, false],
  ]) {
    const { page, context } = await open('candidate', viewport, mobile);
    const result = { name, viewport, samples: [] }; report.cases.push(result);
    for (let seconds = 0; seconds <= 63; seconds += 3) {
      if (seconds) await advance(page, 3 * 60);
      const row = await sample(page);
      result.samples.push({ seconds, ...row }); check(row);
      if ([12, 42, 63].includes(seconds)) {
        await capture(page, `${name}-${seconds}`);
      }
    }
    if (reference) assert.deepEqual(result.samples.map(row => row.digest), reference,
      'Viewport and DPR must not alter the simulated state');
    else reference = result.samples.map(row => row.digest);
    await page.locator('#pause').click();
    await advance(page, 1);
    const before = await sample(page);
    await page.setViewportSize({ width: viewport.height, height: viewport.width });
    await page.waitForFunction(() => particleTest.callbacks.size > 0, null, { polling: 50 });
    await advance(page, 1);
    const after = await sample(page);
    assert.equal(after.digest, before.digest, 'Rotation preserves paused particle positions and velocities');
    assert.equal(after.allocations, before.allocations, 'Rotation must not reseed the particle cloud');
    assert.ok(Math.abs(after.uniforms[2] - after.canvas.width / after.canvas.height) < 1e-6);
    result.resize = { before, after };
    await page.setViewportSize(viewport);
    await page.waitForFunction(() => particleTest.callbacks.size > 0, null, { polling: 50 });
    await advance(page, 1);
    await page.locator('#pause').click();
    await advance(page, 1);
    const cdp = await context.newCDPSession(page);
    const point = { x: viewport.width * 0.75, y: viewport.height * 0.3 };
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [point] });
    await advance(page, 30);
    const held = await sample(page);
    assert.ok(held.uniforms[3] > 20);
    assert.ok(Math.abs(held.uniforms[4] - 0.5) < 1e-6);
    assert.ok(Math.abs(held.uniforms[5] - 0.4) < 1e-6);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await advance(page, 120);
    const released = await sample(page);
    assert.ok(released.uniforms[3] < 1e-6, 'Touch release removes the attracting force');
    result.touch = { held, released }; await cdp.detach();
    await page.locator('#restart').click();
    await advance(page, 1); check(await sample(page));
    await page.locator('#view').focus();
    await page.keyboard.press('ArrowLeft'); await page.keyboard.down('Space');
    await advance(page, 1);
    assert.ok((await sample(page)).uniforms[3] > 0);
    await page.keyboard.up('Space');
    await advance(page, 120);
    assert.ok((await sample(page)).uniforms[3] < 1e-6);
    await capture(page, `${name}-interaction`);
    await page.screenshot({ path: `${output}/${name}-layout.png` });
    assert.equal(await page.locator('#failure').isVisible(), false);
    await context.close();
    console.log(`${name}: three cycles, visible cloud, resize, touch, reset and keyboard passed`);
  }
  assert.deepEqual(report.errors, []); report.passed = true;
} catch (error) { report.error = error.stack; throw error; }
finally {
  await writeFile(`${output}/results.json`, JSON.stringify(report, null, 2) + '\n');
  await browser?.close(); await new Promise(done => server.close(done));
}
