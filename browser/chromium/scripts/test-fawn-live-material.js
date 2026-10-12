// GPU transport and ordinary gesture/settling checks, separate from native Doe qualification.
import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import http from 'node:http';
import path from 'node:path';
import vm from 'node:vm';
const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const resources = new URL('../resources/', import.meta.url);
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-live-material');
await mkdir(output, { recursive: true });
const files = ['fawn-formation.js', 'fawn-prismatic-fluids.html', 'fawn-magnetic-fluids.html'];
const sources = Object.fromEntries(await Promise.all(files.map(async file => [file, await readFile(new URL(file, resources), 'utf8')])));
const report = { passed: false, classification: 'Chrome WebGPU material transport and interaction; emulated screens, no native Doe or physical-phone qualification',
  sources: files.map(file => ({ file, sha256: createHash('sha256').update(sources[file]).digest('hex') })), projections: [], journeys: [], errors: [] };
report.harnessSha256 = createHash('sha256').update(await readFile(new URL(import.meta.url))).digest('hex');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const server = http.createServer(async (req, res) => {
  const file = new URL('.' + new URL(req.url, 'http://localhost').pathname, resources);
  if (!file.href.startsWith(resources.href)) { res.writeHead(403).end(); return; }
  try { res.setHeader('Content-Type', file.pathname.endsWith('.js') ? 'text/javascript' : 'text/html'); res.end(await readFile(file)); }
  catch { res.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({ channel: 'chrome', headless: true, args: ['--enable-unsafe-webgpu'] });
report.browser = browser.version();
function instrument() {
  const s = globalThis.materialProbe = { clock: 1000, callbacks: new Map(), next: 0, errors: [], allocations: 0, uploads: 0, dispatches: 0 };
  Object.defineProperty(performance, 'now', { value: () => s.clock });
  requestAnimationFrame = callback => { s.callbacks.set(++s.next, callback); return s.next; };
  cancelAnimationFrame = id => s.callbacks.delete(id);
  const request = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function(...args) {
    s.adapter = { vendor: this.info.vendor, architecture: this.info.architecture, isFallbackAdapter: this.info.isFallbackAdapter };
    const device = s.device = await request.apply(this, args);
    device.addEventListener('uncapturederror', e => s.errors.push(e.error.message));
    const create = device.createBuffer.bind(device);
    device.createBuffer = descriptor => { s.allocations++; return create(descriptor.usage & GPUBufferUsage.STORAGE
      ? { ...descriptor, usage: descriptor.usage | GPUBufferUsage.COPY_SRC } : descriptor); };
    const write = device.queue.writeBuffer.bind(device.queue);
    device.queue.writeBuffer = (buffer, offset, data, ...args) => {
      if (buffer.usage & GPUBufferUsage.UNIFORM) s.uniforms = Array.from(data); else s.uploads++;
      return write(buffer, offset, data, ...args);
    };
    const groups = new WeakMap(), createGroup = device.createBindGroup.bind(device);
    device.createBindGroup = descriptor => { const group = createGroup(descriptor); groups.set(group, descriptor.entries); return group; };
    const encode = device.createCommandEncoder.bind(device);
    device.createCommandEncoder = (...args) => {
      const encoder = encode(...args), beginRender = encoder.beginRenderPass.bind(encoder), beginCompute = encoder.beginComputePass.bind(encoder);
      encoder.beginRenderPass = (...args) => {
        const pass = beginRender(...args), bind = pass.setBindGroup.bind(pass);
        pass.setBindGroup = (index, group, ...rest) => {
          s.material = groups.get(group)?.find(e => e.binding === 1)?.resource.buffer;
          return bind(index, group, ...rest);
        };
        return pass;
      };
      encoder.beginComputePass = (...args) => {
        const pass = beginCompute(...args), dispatch = pass.dispatchWorkgroups.bind(pass);
        pass.dispatchWorkgroups = (...args) => { s.dispatches++; return dispatch(...args); };
        return pass;
      };
      return encoder;
    };
    return device;
  };
}
async function advance(page, count) {
  await page.evaluate(async count => {
    for (let i=0;i<count;i++) {
      const s=materialProbe;s.clock+=33;const callbacks=[...s.callbacks.values()];s.callbacks.clear();
      for(const callback of callbacks) callback(s.clock);
      await s.device.queue.onSubmittedWorkDone();
    }
  }, count);
}
async function state(page) {
  return page.evaluate(async () => {
    const s=materialProbe, buffer=s.material;
    const read=s.device.createBuffer({size:buffer.size,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
    const encoder=s.device.createCommandEncoder();encoder.copyBufferToBuffer(buffer,0,read,0,buffer.size);s.device.queue.submit([encoder.finish()]);
    await read.mapAsync(GPUMapMode.READ);const bytes=new Uint8Array(read.getMappedRange()).slice();
    const values=new Float32Array(bytes.buffer);const finite=values.every(Number.isFinite);
    read.unmap();read.destroy();
    return { bytes:Array.from(bytes), finite, blend:s.uniforms[11], allocations:s.allocations, uploads:s.uploads, dispatches:s.dispatches, errors:s.errors };
  });
}
try {
  const sandbox=vm.createContext({});vm.runInContext(sources['fawn-formation.js'],sandbox);
  const shaders={};
  for(const file of files.slice(1)) {
    const context=vm.createContext({FawnFormation:sandbox.FawnFormation});
    const render=vm.runInContext(sources[file].split('<script>')[1].split('(async () => {')[0]+'; RENDER_WGSL',context);
    // Exercise each production vertex, binding and viewport projection with an independent material marker.
    shaders[file]=render.slice(0,render.indexOf('@fragment'))+`@fragment fn fs(in: VSOut)->@location(0) vec4<f32> {
      if(length(in.uv-vec2<f32>(0.40,0.40))>0.018){discard;}return vec4<f32>(1.0,0.0,0.0,1.0);}`;
  }
  const page=await browser.newPage();await page.goto(origin+'/fawn-icon.svg');
  report.projections=await page.evaluate(async shaders=>{
    const adapter=await navigator.gpu.requestAdapter(),device=await adapter.requestDevice(),errors=[];
    device.addEventListener('uncapturederror',e=>errors.push(e.error.message));
    const rows=[];
    for(const [file,code] of Object.entries(shaders)) for(const [width,height] of [[512,512],[800,320],[320,640]]) {
      const grid=32,format='rgba8unorm',bytesPerRow=Math.ceil(width*4/256)*256;
      const uniform=device.createBuffer({size:80,usage:GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST});
      const positions=new Float32Array(grid*grid*4);positions.set([0.20,0.15,1,0,0.80,0.75,1,0]);
      const transport=device.createBuffer({size:positions.byteLength,usage:GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_DST});device.queue.writeBuffer(transport,0,positions);
      const target=device.createTexture({size:[width,height],format,usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});
      const module=device.createShaderModule({code});const pipeline=device.createRenderPipeline({layout:'auto',vertex:{module,entryPoint:'vs'},fragment:{module,entryPoint:'fs',targets:[{format}]},primitive:{topology:'triangle-strip'}});
      const group=device.createBindGroup({layout:pipeline.getBindGroupLayout(0),entries:[{binding:0,resource:{buffer:uniform}},{binding:file.includes('prismatic')?4:2,resource:{buffer:transport}}]});
      for(const blend of [0,0.25,0.5,0.75,1]) {
        const side=Math.max(width,height),u=new Float32Array([grid,grid,1/grid,1/grid,0,0,0,0,0,0,width/height,blend,0,0,0,0,width/side,height/side,1,0]);
        device.queue.writeBuffer(uniform,0,u);
        const read=device.createBuffer({size:bytesPerRow*height,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
        const encoder=device.createCommandEncoder(),pass=encoder.beginRenderPass({colorAttachments:[{view:target.createView(),loadOp:'clear',storeOp:'store',clearValue:[0,0,0,1]}]});
        pass.setPipeline(pipeline);pass.setBindGroup(0,group);pass.draw(4);pass.end();encoder.copyTextureToBuffer({texture:target},{buffer:read,bytesPerRow},[width,height]);device.queue.submit([encoder.finish()]);await read.mapAsync(GPUMapMode.READ);
        const pixels=new Uint8Array(read.getMappedRange());let sx=0,sy=0,n=0;
        for(let y=0;y<height;y++)for(let x=0;x<width;x++)if(pixels[y*bytesPerRow+x*4]>200){sx+=x+0.5;sy+=y+0.5;n++;}
        const uv=[0.40+(0.20+0.40*0.60-0.40)*blend,0.40+(0.15+0.40*0.60-0.40)*blend];
        rows.push({file,width,height,blend,pixels:n,center:[sx/n,sy/n],expected:[width/2+(uv[0]-.5)*side,height/2-(uv[1]-.5)*side]});
        read.unmap();read.destroy();
      }
      uniform.destroy();transport.destroy();target.destroy();
    }
    await device.queue.onSubmittedWorkDone();device.destroy();return {rows,errors};
  },shaders);
  assert.deepEqual(report.projections.errors,[]);
  for(const r of report.projections.rows) {assert.ok(r.pixels>20,JSON.stringify(r));assert.ok(r.center.every((v,i)=>Math.abs(v-r.expected[i])<1.5),JSON.stringify(r));}
  await page.close();
  for(const file of files.slice(1)) for(const viewport of [{width:1200,height:800},{width:390,height:844}]) {
    const context=await browser.newContext({viewport,reducedMotion:'reduce'});await context.addInitScript(instrument);
    const page=await context.newPage();page.on('pageerror',e=>report.errors.push(e.message));
    await page.goto(origin+'/'+file);await page.waitForFunction(()=>!document.querySelector('#pause').disabled);
    await page.locator('.details').evaluate(e=>e.open=true);await page.selectOption('#count-select','4096');await page.selectOption('#steps-select','1');await page.selectOption('#formation-select','free');await page.locator('.details').evaluate(e=>e.open=false);
    await page.locator('#pause').click();await advance(page,40);
    const free=await state(page);
    await page.locator('.details').evaluate(e=>e.open=true);await page.selectOption('#formation-select','branded');await page.locator('.details').evaluate(e=>e.open=false);
    const changed=await state(page);assert.deepEqual(changed.bytes,free.bytes,'Formation must preserve the currently active field before its first interpolated frame');assert.equal(changed.uploads,free.uploads,'No new logo dye/height upload');
    await advance(page,300);const settled=await state(page);assert.equal(settled.blend,1);
    await advance(page,20);const resting=await state(page);assert.deepEqual(resting.bytes,settled.bytes,'Settled material remains the accepted live field');assert.equal(resting.dispatches,settled.dispatches);
    await page.screenshot({path:path.join(output,file+'-'+viewport.width+'-settled.png')});
    await page.mouse.move(viewport.width*.5,viewport.height*.5);await page.mouse.down();await advance(page,4);const interrupted=await state(page);
    assert.ok(interrupted.blend<1 && interrupted.blend>0,'Interaction starts from the accepted deformation');await page.mouse.up();await advance(page,20);const stirred=await state(page);assert.notDeepEqual(stirred.bytes,settled.bytes,'Existing fluid resumes on interaction');assert.equal(stirred.uploads,free.uploads,'Input and re-settling never reseed the field');
    await advance(page,300);const resettled=await state(page);assert.equal(resettled.blend,1);assert.notDeepEqual(resettled.bytes,settled.bytes,'A different active history must settle with its own material');assert.equal(resettled.uploads,free.uploads);
    await page.locator('#pause').click();const paused=await state(page);await advance(page,10);const still=await state(page);assert.deepEqual(still.bytes,paused.bytes);assert.equal(still.blend,paused.blend);
    for(const row of [free,settled,resting,interrupted,stirred,resettled,still]) {assert.ok(row.finite);assert.deepEqual(row.errors,[]);}
    report.journeys.push({file,viewport,adapter:await page.evaluate(()=>materialProbe.adapter),free:hash(Buffer.from(free.bytes)),settled:hash(Buffer.from(settled.bytes)),stirred:hash(Buffer.from(stirred.bytes)),resettled:hash(Buffer.from(resettled.bytes)),uploads:stirred.uploads,blend:{settled:settled.blend,interrupted:interrupted.blend},passed:true});
    await context.close();
  }
  assert.deepEqual(report.errors,[]);report.passed=true;console.log(JSON.stringify(report));
} finally {
  await writeFile(path.join(output,'report.json'),JSON.stringify(report,null,2)+'\n');
  await browser.close();await new Promise(resolve=>server.close(resolve));
}
