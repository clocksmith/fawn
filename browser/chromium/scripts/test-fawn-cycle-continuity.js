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
  for (const viewport of [{ width: 640, height: 400 }, { width: 300, height: 500 }]) {
    const page = await browser.newPage({ viewport });
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
      const count = document.querySelector('#count-select'); count.value = '4096';
      count.dispatchEvent(new Event('change'));
      const formation = document.querySelector('#formation-select'); formation.value = 'branded';
      formation.dispatchEvent(new Event('change'));
    });
    const advance = (frames, milliseconds = 40) => page.evaluate(async ({ frames, milliseconds }) => {
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
      return { shape: p.uniforms['Cycle parcel uniforms'][7],
        elapsed: p.uniforms['Cycle parcel rendering uniforms'][12],
        weights: p.uniforms['Cycle parcel rendering uniforms'].slice(8, 11),
        allocations: p.allocations, uploads: p.uploads, handoffs: p.handoffs.slice(),
        effect: document.querySelector('#effect').textContent, data };
    });
    await advance(2);
    const initial = await state();
    const row = { viewport, adapter: await page.evaluate(() => probe.adapter), boundaries: [] };
    report.cases.push(row);
    // Check the start, end and wrap of every blend through two full rotations.
    for (const second of [11, 16, 27, 32, 43, 48, 59, 64, 75, 80, 91, 96]) {
      let current = await state();
      while (current.elapsed < second - 0.08) {
        await advance(Math.max(1, Math.floor((second - 0.08 - current.elapsed) / 0.04)));
        current = await state();
      }
      const before = current;
      await advance(4);
      const after = await state();
      assert.ok(after.data.every(Number.isFinite));
      assert.equal(after.allocations, initial.allocations, 'No scene replacement allocations');
      assert.equal(after.uploads, initial.uploads, 'No scene reseeding');
      let maxPositionStep = 0;
      for (let i = 0; i < before.data.length; i += 12)
        maxPositionStep = Math.max(maxPositionStep,
          Math.hypot(after.data[i] - before.data[i], after.data[i + 1] - before.data[i + 1]));
      assert.ok(maxPositionStep < 0.25, `Position discontinuity at ${second}s: ${maxPositionStep}`);
      assert.ok(Math.abs(after.shape - before.shape) < 0.04,
        `Formation jumps at ${second}s: ${before.shape} -> ${after.shape}`);
      row.boundaries.push({ second, before: before.shape, after: after.shape, maxPositionStep });
      if ([43, 48, 91, 96].includes(second))
        await page.screenshot({ path: path.join(output, `${viewport.width}-${second}.png`) });
    }
    assert.deepEqual((await state()).handoffs, [0, 1, 2, 0, 1, 2].map(i => `Cycle handoff ${i}`));
    // A suspended main thread must not jump past a transition into an unprepared scene.
    const beforeStall = await state(); await advance(1, 20000);
    const afterStall = await state();
    assert.ok(afterStall.elapsed - beforeStall.elapsed <= 0.101, 'Stall skips visible cycle');
    assert.equal(afterStall.effect, beforeStall.effect);
    row.stallAdvance = afterStall.elapsed - beforeStall.elapsed;
    await page.mouse.move(viewport.width / 2, viewport.height / 2); await page.mouse.down();
    await advance(30); assert.ok((await state()).shape < 0.01);
    await page.mouse.up(); await advance(100); assert.ok((await state()).shape > 0.99);
    await page.evaluate(() => document.querySelector('#pause').click());
    const frozen = await state(); await advance(20); assert.deepEqual(await state(), frozen);
    assert.deepEqual(await page.evaluate(() => probe.errors), []);
    assert.deepEqual(errors, []);
    row.inputRecovery = true; row.pauseFrozen = true;
    await page.close();
    console.log(`Repeated cycle, stalled frame and input: ${viewport.width} × ${viewport.height}`);
  }
  report.passed = true;
} catch (error) {
  report.error = error.stack;
  throw error;
} finally {
  await writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close(); server.closeAllConnections(); await new Promise(resolve => server.close(resolve));
}
