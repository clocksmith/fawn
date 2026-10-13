// Repeated real WebGPU scene boundaries, including delayed frames and input interruption.
import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import http from 'node:http';
import path from 'node:path';
const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve('browser/chromium/resources');
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-cycle-continuity');
const baseline = process.env.FAWN_BASELINE_REF;
const files = ['fawn-cycle.js', 'fawn-cycle-clock.js', 'fawn-cycle-gpu.js',
  'fawn-formation.js', 'fawn-parcels.js', 'fawn-parcel-renderer.js', 'fawn-cycle-transfers.js',
  'fawn-shaders.js', 'fawn-cycle.html'];
const source = async file => baseline
  ? execFileSync('git', ['show', `${baseline}:browser/chromium/resources/${file}`])
  : readFile(path.join(root, file));
await mkdir(output, { recursive: true });
const report = { passed: false, baseline: baseline || null,
  classification: 'Software WebGPU continuity diagnostic; no physical GPU, Safari or performance qualification',
  sources: await Promise.all(files.map(async file => ({ file,
    sha256: createHash('sha256').update(await source(file)).digest('hex') }))), cases: [] };
const server = http.createServer(async (req, res) => {
  const file = new URL(req.url, 'http://localhost').pathname.slice(1);
  if (file.includes('/') || file.includes('..')) { res.writeHead(403).end(); return; }
  try {
    res.setHeader('Content-Type', file.endsWith('.js') ? 'text/javascript' :
      file.endsWith('.svg') ? 'image/svg+xml' : 'text/html');
    res.end(await source(file));
  } catch { res.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const browser = await chromium.launch({ headless: true,
  args: ['--enable-unsafe-webgpu', '--use-angle=swiftshader'] });
report.browser = browser.version();
try {
  for (const viewport of [{ width: 640, height: 400 }]) {
    const page = await browser.newPage({ viewport, hasTouch: true });
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.addInitScript(() => {
      const p = globalThis.probe = { clock: 1000, id: 0, callbacks: new Map(), uniforms: {},
        errors: [], allocations: 0, uploads: 0, buffers: new Map(), current: null, handoffs: [] };
      Object.defineProperty(performance, 'now', { value: () => p.clock });
      requestAnimationFrame = cb => { p.callbacks.set(++p.id, cb); return p.id; };
      cancelAnimationFrame = id => p.callbacks.delete(id);
      const request = GPUAdapter.prototype.requestDevice;
      GPUAdapter.prototype.requestDevice = async function(...args) {
        const device = p.device = await request.apply(this, args);
        p.adapter = { vendor: this.info.vendor, architecture: this.info.architecture };
        device.addEventListener('uncapturederror', e => p.errors.push(e.error.message));
        const create = device.createBuffer.bind(device);
        device.createBuffer = d => {
          const b = create(d);
          if (d.label) { p.buffers.set(d.label, b); p.allocations++; }
          return b;
        };
        const write = device.queue.writeBuffer.bind(device.queue);
        device.queue.writeBuffer = (buffer, offset, data, ...rest) => {
          if (buffer.usage & GPUBufferUsage.UNIFORM) p.uniforms[buffer.label] = Array.from(data);
          else p.uploads++;
          return write(buffer, offset, data, ...rest);
        };
        const group = device.createBindGroup.bind(device);
        device.createBindGroup = d => {
          const result = group(d);
          result.probeBuffers = d.entries.map(e => e.resource.buffer);
          return result;
        };
        const encode = device.createCommandEncoder.bind(device);
        device.createCommandEncoder = (...args) => {
          const e = encode(...args), begin = e.beginComputePass.bind(e);
          e.beginComputePass = (...args) => {
            const pass = begin(...args), pipeline = pass.setPipeline.bind(pass),
              bind = pass.setBindGroup.bind(pass);
            let label;
            pass.setPipeline = pipelineValue => {
              label = pipelineValue.label;
              if (label.startsWith('Cycle handoff')) p.handoffs.push(label);
              return pipeline(pipelineValue);
            };
            pass.setBindGroup = (index, value, ...rest) => {
              if (label === 'Cycle moving parcels') p.current = value.probeBuffers[2];
              return bind(index, value, ...rest);
            };
            return pass;
          };
          return e;
        };
      return device;
      };
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/fawn-cycle.html`);
    await page.waitForFunction(() => probe.device && probe.callbacks.size, null, { polling: 50 });
    await page.evaluate(() => {
      const count = document.querySelector('#count-select'); count.value = '1024';
      count.dispatchEvent(new Event('change'));
      const formation = document.querySelector('#formation-select'); formation.value = 'branded';
      formation.dispatchEvent(new Event('change'));
    });
    const advance = (frames, milliseconds = 50) => page.evaluate(async ({ frames, milliseconds }) => {
      for (let i = 0; i < frames; i++) {
        probe.clock += milliseconds;
        const callbacks = [...probe.callbacks.values()]; probe.callbacks.clear();
        callbacks.forEach(cb => cb(probe.clock));
        await probe.device.queue.onSubmittedWorkDone(); await Promise.resolve();
      }
    }, { frames, milliseconds });
    const state = () => page.evaluate(async () => {
      const p = probe;
      const read = p.device.createBuffer({ size: p.current.size,
        usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
      const e = p.device.createCommandEncoder();
      e.copyBufferToBuffer(p.current, 0, read, 0, p.current.size);
      p.device.queue.submit([e.finish()]); await read.mapAsync(GPUMapMode.READ);
      const data = Array.from(new Float32Array(read.getMappedRange())); read.unmap(); read.destroy();
      return { parcelUniform: p.uniforms['Cycle parcel uniforms'], waveUniform:p.uniforms['Cycle Wave uniforms'], shape: p.uniforms['Cycle parcel uniforms'][7],
        elapsed: p.uniforms['Cycle parcel rendering uniforms'][12],
        weights: p.uniforms['Cycle parcel rendering uniforms'].slice(8, 11),
        allocations: p.allocations, uploads: p.uploads, handoffs: p.handoffs.slice(),
        effect: document.querySelector('#effect').textContent, data };
    });
    await advance(2);
    const row = { viewport, boundaries: [], interactions: [] }; report.cases.push(row);
    const initial = await state();
    for (let scene = 0; scene < 9; scene++) {
      await advance(50, 100);
      const formed = await state();
      assert.ok(formed.shape > .99, 'Fawn forms within six-second background');
      await page.screenshot({ path: path.join(output, `scene-${scene}-formed.png`) });
      await advance(10, 100); const start = await state();
      await advance(10, 100); const middle = await state();
      assert.ok(middle.weights.filter(v => v > .1 && v < .9).length === 2);
      await advance(10, 100); const end = await state();
      assert.equal(end.effect, ['Magnets', 'Particles', 'Flows'][scene % 3]);
      assert.ok(end.data.every(Number.isFinite));
      assert.equal(end.allocations, initial.allocations);
      assert.equal(end.uploads, initial.uploads);
      row.boundaries.push({ scene, shape: formed.shape, elapsed: end.elapsed,
        middle: middle.weights, effect: end.effect, handoffs: end.handoffs.length });
    }
    assert.ok(Math.abs(row.boundaries.at(-1).elapsed - initial.elapsed - 72) < .001);
    await page.evaluate(() => document.querySelector('#restart').click()); await advance(2);
    await advance(55, 100);
    await page.mouse.move(320, 200); await advance(1, 100);
    const deferred = await state(); assert.equal(deferred.effect, 'Flows');
    await advance(8, 100); assert.deepEqual((await state()).weights, [1, 0, 0]);
    await advance(13, 100); const partial = await state();
    assert.ok(partial.weights[1] > 0 && partial.weights[1] < 1);
    await page.mouse.move(330, 190); await advance(1, 100);
    const moving = await state(); assert.ok(moving.weights[1] > partial.weights[1]);
    await advance(10, 100); assert.equal((await state()).effect, 'Magnets');
    row.interactions.push({ lateDeferral: true, midBlendContinues: true });
    // Hold through the scene deadline: release burst remains intact and touch cancellation is quiet.
    await page.mouse.down(); await advance(80, 100);
    const held = await state(); assert.equal(held.effect, 'Magnets');
    await page.mouse.up(); await advance(1, 50);
    const release = await state(); assert.ok(release.parcelUniform[14] > 1.5);
    await advance(80, 100); assert.ok((await state()).data.every(Number.isFinite));
    await page.evaluate(() => document.querySelector('#restart').click()); await advance(2);
    const cdp = await page.context().newCDPSession(page);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{x:320,y:200}] });
    await advance(20, 100);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchCancel', touchPoints: [] });
    await advance(1); assert.equal((await state()).parcelUniform[14], 0);
    await page.evaluate(() => document.querySelector('#pause').click());
    const paused = await state(); await advance(20, 100);
    assert.equal((await state()).elapsed, paused.elapsed);
    await page.evaluate(() => document.querySelector('#pause').click()); await advance(2);
    row.interactions.push({ chargedRelease: release.parcelUniform[14], touchCancelNoBurst: true, pause: true });
    assert.deepEqual(await page.evaluate(() => probe.errors), []); assert.deepEqual(errors, []);
    await page.close();
    console.log(`Three rotations and interaction timing: ${viewport.width} × ${viewport.height}`);
  }
  report.passed = true;
} catch (error) {
  report.error = error.stack;
  throw error;
} finally {
  await writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close(); server.closeAllConnections(); await new Promise(resolve => server.close(resolve));
}
