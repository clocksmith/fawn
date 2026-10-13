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
    await advance(2);await advance(320);
    const row={viewport,checks:[]};report.cases.push(row);
    await page.mouse.move(320,200);await page.mouse.down();await advance(30);
    const held=await state();assert.equal(held.effect,'Magnets');assert.ok(Math.abs(held.parcelUniform[10]-26.67)<.00001);assert.equal(held.waveUniform[6],1);
    const cohort=[];for(let i=0;i<held.data.length;i+=12){if(Math.hypot(held.data[i],held.data[i+1])<.3)cohort.push(i);}
    assert.ok(cohort.length>40,'Magnet gathers parcels near pointer');
    await page.mouse.up();await advance(1);const released=await state();
    assert.ok(released.parcelUniform[14]>1.5,'Charged release starts radial pulse');assert.equal(released.parcelUniform[10],0,'Hover cannot pull against burst');
    await page.mouse.move(400,180);await advance(5);const expanded=await state();
    assert.equal(expanded.parcelUniform[12],0,'Release origin stays fixed');assert.equal(expanded.parcelUniform[13],0);
    const radius=data=>cohort.reduce((sum,i)=>sum+Math.hypot(data[i],data[i+1]),0)/cohort.length;
    assert.ok(radius(expanded.data)>radius(held.data)+.02,'Collected parcels expand on release');
    await advance(15);const ended=await state();assert.equal(ended.parcelUniform[14],0,'Burst expires');
    await advance(95);const recovered=await state();assert.ok(recovered.shape>.99,'Logo recovers after release');
    row.checks.push({cohort:cohort.length,heldRadius:radius(held.data),releasedRadius:radius(expanded.data),releaseStrength:released.parcelUniform[14],finalShape:recovered.shape});
    assert.ok(recovered.data.every(Number.isFinite));
    await page.screenshot({path:path.join(output,'recovered.png')});
    // A cancelled gesture must never generate the release effect.
    await page.mouse.down();await advance(4);
    await page.evaluate(()=>document.querySelector('#view').dispatchEvent(new PointerEvent('pointercancel',{pointerId:1,pointerType:'mouse'})));
    await advance(1);assert.equal((await state()).parcelUniform[14],0);await page.mouse.up();
    assert.deepEqual(await page.evaluate(()=>probe.errors),[]);assert.deepEqual(errors,[]);
    row.cancelNoBurst=true;
    await page.evaluate(()=>document.querySelector('#restart').click());await advance(2);await advance(235);
    const partial=await state();assert.ok(partial.weights[1]>0&&partial.weights[1]<1);
    await page.mouse.move(330,190);await advance(1);const deferred=await state();
    assert.deepEqual(deferred.weights,partial.weights,'Deferring clock preserves rendered blend');
    const handoffs=deferred.handoffs.length;
    await advance(18);const quiet=await state();assert.deepEqual(quiet.weights,deferred.weights,'No further transition before one quiet second');
    await page.mouse.move(332,190);await advance(1);await advance(18);assert.deepEqual((await state()).weights,deferred.weights,'Repeated motion renews one-second guard');
    await advance(5);const resumed=await state();assert.ok(resumed.weights[1]>deferred.weights[1]);assert.equal(resumed.handoffs.length,handoffs,'No duplicated handoff');
    row.debounce={heldBlend:deferred.weights[1],resumedBlend:resumed.weights[1],handoffs};
    // Native browser touch events must produce the same release, independent of hover.
    await page.evaluate(()=>document.querySelector('#restart').click());await advance(2);await advance(320);
    const cdp=await page.context().newCDPSession(page);
    await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:320,y:200}]});await advance(2);
    await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});await advance(1);const tapped=await state();
    assert.ok(tapped.parcelUniform[14]>0&&tapped.parcelUniform[14]<released.parcelUniform[14],'Short touch release is gentler than charged hold');
    await advance(16);
    await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:320,y:200}]});await advance(20);
    await cdp.send('Input.dispatchTouchEvent',{type:'touchCancel',touchPoints:[]});await advance(1);assert.equal((await state()).parcelUniform[14],0,'Touch cancellation has no explosion');
    row.touch={tapStrength:tapped.parcelUniform[14],cancelNoBurst:true};
    assert.deepEqual(await page.evaluate(()=>probe.errors),[]);assert.deepEqual(errors,[]);
    await page.close();
    console.log(`Magnetic gather, release and recovery: ${viewport.width} × ${viewport.height}`);
  }
  report.passed = true;
} catch (error) {
  report.error = error.stack;
  throw error;
} finally {
  await writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close(); server.closeAllConnections(); await new Promise(resolve => server.close(resolve));
}
