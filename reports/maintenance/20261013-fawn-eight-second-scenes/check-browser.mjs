// Real WebGPU scene boundaries and force attenuation under sustained mouse/touch input.
import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import http from 'node:http';
import path from 'node:path';
const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve('browser/chromium/resources');
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-eight-second-scenes');
const baseline = process.env.FAWN_BASELINE_REF;
const files = ['fawn-cycle.js', 'fawn-cycle-clock.js', 'fawn-cycle-gpu.js',
  'fawn-formation.js', 'fawn-parcels.js', 'fawn-parcel-renderer.js', 'fawn-cycle-transfers.js',
  'fawn-shaders.js', 'fawn-cycle.html'];
const snapshot = new Map();
const source = async file => {
  if (!snapshot.has(file)) snapshot.set(file, baseline
    ? execFileSync('git', ['show', `${baseline}:browser/chromium/resources/${file}`])
    : await readFile(path.join(root, file)));
  return snapshot.get(file);
};
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
          else { p.uploads++; if (buffer.label === 'Cycle logo boundary') p.mark = Array.from(data); }
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
    await page.evaluate(count => { globalThis.diagnosticCount = count; }, process.env.FAWN_DEFAULT_COUNTS ? (viewport.width < 400 ? 16384 : 65536) : Number(process.env.FAWN_LIVING_COUNT || 4096));
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
      const read = p.device.createBuffer({ size: p.current.size, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
      const e = p.device.createCommandEncoder(); e.copyBufferToBuffer(p.current, 0, read, 0, p.current.size);
      p.device.queue.submit([e.finish()]); await read.mapAsync(GPUMapMode.READ);
      const data = Array.from(new Float32Array(read.getMappedRange())); read.unmap(); read.destroy();
      const grid = 384;
      p.mark ||= Array.from(FawnFormation.field(grid, document.querySelector('#view'), document.querySelector('.brand-logo')));
      let inside = 0;
      for(let i=0;i<data.length;i+=12){
        const x=Math.max(0,Math.min(grid-1,(data[i]*.5+.5)*grid-.5));
        const y=Math.max(0,Math.min(grid-1,(data[i+1]*.5+.5)*grid-.5));
        const x0=Math.floor(x),y0=Math.floor(y),x1=Math.min(grid-1,x0+1),y1=Math.min(grid-1,y0+1),fx=x-x0,fy=y-y0;
        const at=(x,y)=>p.mark[(y*grid+x)*4+3]*2;
        const sdf=(at(x0,y0)*(1-fx)+at(x1,y0)*fx)*(1-fy)+(at(x0,y1)*(1-fx)+at(x1,y1)*fx)*fy;
        if(sdf>=-.008)inside++;
      }
      return { data, inside:inside/(data.length/12), shape:p.uniforms['Cycle parcel uniforms'][7],
        flowDt:p.uniforms['Cycle Flow uniforms'][8],waveDt:p.uniforms['Cycle Wave uniforms'][8],
        elapsed:p.uniforms['Cycle parcel rendering uniforms'][12],
        weights:p.uniforms['Cycle parcel rendering uniforms'].slice(8,11),
        allocations:p.allocations, uploads:p.uploads, effect:document.querySelector('#effect').textContent };
    });
    const difference = (a,b) => {
      let distance=0, material=0, maximum=0;
      for(let i=0;i<a.data.length;i+=12){ const step=Math.hypot(b.data[i]-a.data[i],b.data[i+1]-a.data[i+1]); distance+=step;maximum=Math.max(maximum,step);
        for(let c=4;c<12;c++)material+=Math.abs(b.data[i+c]-a.data[i+c]); }
      return { meanDistance:distance/(a.data.length/12),maxDistance:maximum,materialChange:material/(a.data.length/12) };
    };
    const picture = async name => {
      const png = await page.screenshot({path:path.join(output,`${viewport.width}-${name}.png`)});
      return page.evaluate(async encoded => {
        const bitmap=await createImageBitmap(await (await fetch('data:image/png;base64,'+encoded)).blob());
        const c=document.createElement('canvas');c.width=bitmap.width;c.height=bitmap.height;
        const ctx=c.getContext('2d');ctx.drawImage(bitmap,0,0);bitmap.close();
        const pixels=ctx.getImageData(0,0,c.width,c.height).data;let light=0,n=0,lit=0,change=0;
        for(let y=Math.floor(c.height*.2);y<c.height*.8;y++)for(let x=Math.floor(c.width*.1);x<c.width*.9;x++){
          const i=(y*c.width+x)*4,l=(pixels[i]*.2126+pixels[i+1]*.7152+pixels[i+2]*.0722)/255;
          light+=l;n++;if(l>.08)lit++;
          if(probe.previousPixels)for(let channel=0;channel<3;channel++)change+=Math.abs(pixels[i+channel]-probe.previousPixels[i+channel])/255;
        }
        probe.previousPixels=pixels;
        return {meanLuminance:light/n,litFraction:lit/n,pixelChangeFromPrevious:change/(n*3)};
      },png.toString('base64'));
    };
    await advance(2);
    const row={viewport,count:Number(await page.locator('#count-select').inputValue()),scenes:[],issues:[]};report.cases.push(row);
    const initial=await state();
    const check=(condition,problem)=>{if(!condition)row.issues.push(problem);};
    for(let scene=0;scene<(process.env.FAWN_MAGNET_ONLY ? 0 : 3);scene++){
      await advance(10,100);const freePixels=await picture(`${scene}-free`);
      await advance(32,100);const gathered=await state();
      await advance(6,100);const moving=await state();const motion=difference(gathered,moving);
      const gatheredPixels=await picture(`${scene}-branded`);
      await advance(6,100);const held=await state();const heldPixels=await picture(`${scene}-held`);
      check(gathered.inside>=.95 && held.inside>=.95,`Logo silhouette lost in ${gathered.effect}: ${gathered.inside}, ${held.inside}`);
      check(motion.meanDistance>.003,`Frozen branded ${gathered.effect}: ${JSON.stringify(motion)}`);
      check(motion.materialChange>.003,`Frozen branded material in ${gathered.effect}: ${JSON.stringify(motion)}`);
      check(moving.flowDt>0 && moving.waveDt>0,`Simulation timestep suppressed in ${gathered.effect}`);
      const samples=[];
      for(const frames of [6,4,5,5,6]){
        await advance(frames,100);const snapshot=await state();
        samples.push({elapsed:snapshot.elapsed,weights:snapshot.weights,pixels:await picture(`${scene}-blend-${samples.length}`)});
      }
      const end=await state();
      const reference=Math.min(heldPixels.meanLuminance,samples.at(-1).pixels.meanLuminance);
      check(samples.every(s=>s.pixels.meanLuminance>=reference*.65),`Black dip in ${gathered.effect} transition: ${JSON.stringify(samples)}`);
      check(samples.every(s=>s.pixels.meanLuminance>.012),`Dark transition from ${gathered.effect}`);
      check(samples.at(-1).pixels.meanLuminance>.012,`Dark incoming ${end.effect}`);
      assert.equal(end.effect,['Magnets','Particles','Flows'][scene]);
      assert.ok(end.data.every(Number.isFinite)); assert.equal(end.allocations,initial.allocations);assert.equal(end.uploads,initial.uploads);
      row.scenes.push({effect:gathered.effect,inside:gathered.inside,heldInside:held.inside,motion,freePixels,gatheredPixels,heldPixels,samples});
    }
    if(!process.env.FAWN_MAGNET_ONLY)assert.ok(Math.abs((await state()).elapsed-initial.elapsed-24)<.001);
    // Frozen geometry separates magnetic-field rendering from a mere animated pigment palette.
    let magnetic;
    for(let i=0;i<240;i++){
      await advance(1,100);magnetic=await state();
      if(magnetic.effect==='Magnets' && magnetic.weights[1]>.99 && magnetic.shape>.99)break;
    }
    assert.equal(magnetic.effect,'Magnets');assert.ok(magnetic.weights[1]>.99 && magnetic.shape>.99);
    await page.locator('#view').press('Space');
    const visibleFilm=await picture('magnetic-live-field');
    await page.evaluate(async()=>{
      probe.waveRestore=[];
      for(const label of ['Cycle surface 0','Cycle surface 1']){
        const source=probe.buffers.get(label),copy=probe.device.createBuffer({size:source.size,usage:GPUBufferUsage.MAP_READ|GPUBufferUsage.COPY_DST});
        const e=probe.device.createCommandEncoder();e.copyBufferToBuffer(source,0,copy,0,source.size);probe.device.queue.submit([e.finish()]);await copy.mapAsync(GPUMapMode.READ);
        const data=new Float32Array(copy.getMappedRange()).slice();copy.unmap();copy.destroy();
        probe.waveRestore.push({source,data});probe.device.queue.writeBuffer(source,0,new Float32Array(data.length));
      }
      document.querySelector('#steps-select').dispatchEvent(new Event('change'));
    });
    await advance(1,100);const missingFilm=await picture('magnetic-zero-field');
    check(missingFilm.pixelChangeFromPrevious>.008,`Magnetic relief missing from rendered scene: ${missingFilm.pixelChangeFromPrevious}`);
    await page.evaluate(()=>{for(const {source,data} of probe.waveRestore)probe.device.queue.writeBuffer(source,0,data);document.querySelector('#steps-select').dispatchEvent(new Event('change'));});
    await advance(1,100);const restoredFilm=await picture('magnetic-restored-field');
    check(Math.abs(restoredFilm.meanLuminance-visibleFilm.meanLuminance)<.001,'Magnetic field restoration changes frozen scene');
    row.magneticFieldAblation={visibleFilm,missingFilm,restoredFilm};
    assert.deepEqual(await page.evaluate(()=>probe.errors),[]);assert.deepEqual(errors,[]);
    await page.close();console.log(JSON.stringify({viewport,issues:row.issues}));
  }
  report.passed=report.cases.every(c=>!c.issues.length);
  assert.ok(report.passed,'Live logo/transition acceptance failed; see retained report');
} catch(error){report.error=error.stack;throw error;}
finally{await writeFile(path.join(output,'report.json'),JSON.stringify(report,null,2)+'\n');await browser.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
