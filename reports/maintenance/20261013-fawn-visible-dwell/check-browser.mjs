// Real WebGPU scene boundaries and force attenuation under sustained mouse/touch input.
import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import http from 'node:http';
import path from 'node:path';
const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve('browser/chromium/resources');
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-visible-dwell');
const baseline = process.env.FAWN_BASELINE_REF;
const files = ['fawn-cycle.js', 'fawn-cycle-clock.js', 'fawn-cycle-gpu.js',
  'fawn-formation.js', 'fawn-parcels.js', 'fawn-parcel-renderer.js', 'fawn-cycle-transfers.js',
  'fawn-shaders.js', 'fawn-cycle.html'];
const source = async file => baseline
  ? execFileSync('git', ['show', `${baseline}:browser/chromium/resources/${file}`])
  : readFile(path.join(root, file));
await mkdir(output, { recursive: true });
const report = { passed: false, baseline: baseline || null,
  harnessSha256: createHash('sha256').update(await readFile(new URL(import.meta.url))).digest('hex'),
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
const browser = await chromium.launch({ channel: 'chrome', headless: true,
  args: ['--enable-unsafe-webgpu', '--use-angle=swiftshader'] });
report.browser = browser.version();
try {
  for (const viewport of [{ width: 640, height: 400 }, { width: 300, height: 500 }]) {
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
          else { p.uploads++; if (buffer.label === 'Cycle logo targets') p.goals = Array.from(data); }
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
    await page.evaluate(count => { globalThis.diagnosticCount = count; }, process.env.FAWN_DEFAULT_COUNTS ? (viewport.width < 400 ? 16384 : 65536) : 1024);
    await page.evaluate(() => {
      const count = document.querySelector('#count-select'); count.value = String(globalThis.diagnosticCount || 1024);
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
      const distances = [];
      for (let i = 0; i < data.length / 12; i++) {
        const hash = ((Math.imul(i, 1664525) + 1013904223) ^ (i >>> 9)) >>> 0;
        const target = (hash % (p.goals.length / 4)) * 4;
        distances.push(Math.hypot(data[i * 12] - p.goals[target], data[i * 12 + 1] - p.goals[target + 1]));
      }
      distances.sort((a, b) => a - b);
      return { logo: { within: distances.filter(d => d < .04).length / distances.length,
        p95: distances[Math.floor(distances.length * .95)], max: distances.at(-1) }, parcelUniform: p.uniforms['Cycle parcel uniforms'], waveUniform:p.uniforms['Cycle Wave uniforms'], shape: p.uniforms['Cycle parcel uniforms'][7],
        elapsed: p.uniforms['Cycle parcel rendering uniforms'][12],
        weights: p.uniforms['Cycle parcel rendering uniforms'].slice(8, 11),
        allocations: p.allocations, uploads: p.uploads, handoffs: p.handoffs.slice(),
        effect: document.querySelector('#effect').textContent, data };
    });
    if (!baseline) {
      assert.equal(await page.locator('#pause, #restart').count(), 0);
      const boxes = await page.locator('.metrics #frame, .metrics summary, .metrics .project-link').evaluateAll(elements => elements.map(e => {const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};}));
      assert.equal(boxes.length, 3);
      assert.ok(Math.max(...boxes.map(b => b.y+b.height/2))-Math.min(...boxes.map(b => b.y+b.height/2)) < 2, 'FPS, Settings and GitHub share the footer row');
      await page.locator('summary').click();
      const panel = await page.locator('.details-panel').boundingBox();
      assert.ok(panel.x >= 0 && panel.x+panel.width <= viewport.width && panel.y >= 0, 'Settings panel fits viewport');
      await page.locator('summary').click();
    }
    await advance(2);
    const row = { viewport, count: Number(await page.locator('#count-select').inputValue()), boundaries: [], interactions: [] }; report.cases.push(row);
    const initial = await state();
    for (let scene = 0; scene < (process.env.FAWN_FORMATION_ONLY ? 3 : 9); scene++) {
      await advance(32, 100);
      const gathered = await state();
      assert.ok(gathered.logo.within >= .95, `Logo not visibly gathered: ${JSON.stringify(gathered.logo)}`);
      await page.screenshot({ path: path.join(output, `scene-${viewport.width}-${scene}-gathered.png`) });
      await advance(12, 100);
      const formed = await state();
      assert.ok(formed.logo.within >= .99, 'Logo stays physically gathered during its visible dwell');
      await page.screenshot({ path: path.join(output, `scene-${viewport.width}-${scene}-formed.png`) });
      await advance(1, 100); const start = await state();
      await advance(12, 100); const middle = await state();
      assert.ok(middle.weights.filter(v => v > .1 && v < .9).length === 2);
      if (scene === 0) await page.screenshot({ path: path.join(output, `scene-${viewport.width}-middle.png`) });
      await advance(13, 100); const end = await state();
      assert.equal(end.effect, ['Magnets', 'Particles', 'Flows'][scene % 3]);
      assert.ok(end.data.every(Number.isFinite));
      assert.equal(end.allocations, initial.allocations);
      assert.equal(end.uploads, initial.uploads);
      row.boundaries.push({ scene, gathered: gathered.logo, held: formed.logo, verifiedDwellS: 1.2, shape: formed.shape, elapsed: end.elapsed,
        middle: middle.weights, effect: end.effect, handoffs: end.handoffs.length });
    }
    assert.ok(Math.abs(row.boundaries.at(-1).elapsed - initial.elapsed - (process.env.FAWN_FORMATION_ONLY ? 21 : 63)) < .001);
    if (process.env.FAWN_FORMATION_ONLY) {
      assert.deepEqual(await page.evaluate(() => probe.errors), []); assert.deepEqual(errors, []);
      await page.close(); console.log(`Default formation and footer: ${viewport.width} × ${viewport.height}`); continue;
    }
    await page.locator('#view').press('r'); await advance(2);
    await advance(44, 100);
    await page.mouse.move(viewport.width / 2, viewport.height / 2); await page.mouse.down();
    await advance(1, 100);
    const start = await state();
    assert.ok(start.weights[1] > 0, 'Late input does not defer the blend');
    await advance(12, 100);
    const middle = await state();
    assert.ok(Math.abs(middle.weights[1] - .5) < .0001);
    const flowGain = middle.parcelUniform[14] > 0 ? null :
      await page.evaluate(() => probe.uniforms['Cycle Flow uniforms'][6] / 1.4);
    assert.ok(Math.abs(flowGain - .15) < .0001, 'Flow force follows the transition curve');
    assert.ok(Math.abs(middle.waveUniform[6] - .15) < .0001, 'Magnetic force follows the transition curve');
    assert.ok(Math.abs(middle.parcelUniform[10] / (26.67 * middle.weights[1]) - .15) < .0001,
      'Parcel attraction follows the transition curve');
    await advance(13, 100); const firstBoundary = await state();
    assert.equal(firstBoundary.effect, 'Magnets');
    const midpointForces = [];
    for (const expected of ['Particles', 'Flows', 'Magnets']) {
      await advance(57, 100);
      const midpoint = await state();
      const expectedForce = 26.67 * (midpoint.weights[1] - midpoint.weights[2]) * .15;
      assert.ok(Math.abs(midpoint.parcelUniform[10] - expectedForce) < .001,
        'Attraction and repulsion both follow the transition curve');
      midpointForces.push({ effect: midpoint.effect, weights: midpoint.weights,
        force: midpoint.parcelUniform[10], expectedForce });
      await advance(13, 100);
      assert.equal((await state()).effect, expected, 'Held input cannot delay any scene');
    }
    assert.ok(Math.abs((await state()).elapsed - firstBoundary.elapsed - 21) < .001);
    row.interactions.push({ uninterruptedHoldRotation: true, midpointGain: flowGain,
      lateInputNoDeferral: true, midpointForces });
    // Release the charged magnet at crossfade midpoint; attenuation includes its outward pulse.
    await advance(57, 100);
    await page.mouse.up(); await advance(1, 50);
    const release = await state();
    assert.ok(release.parcelUniform[14] > .1 && release.parcelUniform[14] < .14,
      'Release pulse is softened at the transition midpoint');
    await advance(13, 100); assert.equal((await state()).effect, 'Particles');
    assert.ok((await state()).data.every(Number.isFinite));
    await page.locator('#view').press('r'); await advance(2);
    const cdp = await page.context().newCDPSession(page);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{x:viewport.width/2,y:viewport.height/2}] });
    await advance(210, 100);
    const touchRotation = await state();
    assert.equal(touchRotation.effect, 'Flows');
    assert.ok(Math.abs(touchRotation.elapsed - .05 - 21) < .001, 'Touch hold keeps the full rotation on time');
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchCancel', touchPoints: [] });
    await advance(1); assert.equal((await state()).parcelUniform[14], 0);
    await page.locator('#view').press('Space');
    const paused = await state(); await advance(20, 100);
    assert.equal((await state()).elapsed, paused.elapsed);
    await page.locator('#view').press('Space'); await advance(2);
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
