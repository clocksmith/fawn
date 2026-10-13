import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import http from 'node:http';
import path from 'node:path';
import {chromium} from '/Users/xyz/deco/reploid/node_modules/playwright/index.mjs';
const root=path.resolve('browser/chromium/resources'),out=path.resolve('bench/out/fawn-pointer-strength');
await mkdir(out,{recursive:true});
const baseline=execFileSync('git',['show','38178d5d7:browser/chromium/resources/fawn-cycle-gpu.js']);
const candidate=await readFile(path.join(root,'fawn-cycle-gpu.js'));
const report={passed:false,classification:'Software WebGPU input/finite-state diagnostic; not physical performance or Safari qualification',sources:{baseline:createHash('sha256').update(baseline).digest('hex'),candidate:createHash('sha256').update(candidate).digest('hex')},cases:[]};
const server=http.createServer(async(req,res)=>{try{const url=new URL(req.url,'http://localhost');const file=path.basename(url.pathname);res.setHeader('Content-Type',file.endsWith('.js')?'text/javascript':file.endsWith('.svg')?'image/svg+xml':'text/html');res.end(file==='baseline.js'?baseline:await readFile(path.join(root,file)));}catch{res.writeHead(404).end();}});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const browser=await chromium.launch({headless:true,args:['--enable-unsafe-webgpu','--use-angle=swiftshader']});
try{
 for(const version of ['before','after']){
 const page=await browser.newPage({viewport:{width:390,height:600}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
 if(version==='before')await page.route('**/fawn-cycle-gpu.js',route=>route.fulfill({contentType:'text/javascript',body:baseline}));
 await page.addInitScript(()=>{const p=globalThis.probe={clock:1000,id:0,callbacks:new Map(),uniforms:{},buffers:new Map(),errors:[]};Object.defineProperty(performance,'now',{value:()=>p.clock});requestAnimationFrame=fn=>{p.callbacks.set(++p.id,fn);return p.id;};cancelAnimationFrame=id=>p.callbacks.delete(id);const request=GPUAdapter.prototype.requestDevice;GPUAdapter.prototype.requestDevice=async function(...args){const d=p.device=await request.apply(this,args);p.adapter={vendor:this.info.vendor,architecture:this.info.architecture};d.addEventListener('uncapturederror',e=>p.errors.push(e.error.message));const write=d.queue.writeBuffer.bind(d.queue);d.queue.writeBuffer=(b,o,data,...rest)=>{if(b.usage&GPUBufferUsage.UNIFORM)p.uniforms[b.label]=Array.from(data);return write(b,o,data,...rest);};const create=d.createBuffer.bind(d);d.createBuffer=o=>{const b=create(o);if(o.label)p.buffers.set(o.label,b);return b;};return d;};});
 await page.goto(`http://127.0.0.1:${server.address().port}/fawn-cycle.html`);
 await page.waitForFunction(()=>document.querySelector('#status').textContent==='WebGPU active',null,{polling:50});
 await page.evaluate(()=>{const count=document.querySelector('#count-select');count.value='1024';count.dispatchEvent(new Event('change'));});
 const advance=()=>page.evaluate(async()=>{probe.clock+=16;const callbacks=[...probe.callbacks.values()];probe.callbacks.clear();callbacks.forEach(cb=>cb(probe.clock));await probe.device.queue.onSubmittedWorkDone();await Promise.resolve();});
 const strengths=()=>page.evaluate(()=>({flow:probe.uniforms['Cycle Flow uniforms'][6],radius:probe.uniforms['Cycle Flow uniforms'][7],magnetic:probe.uniforms['Cycle Wave uniforms'][6],parcels:probe.uniforms['Cycle parcel uniforms'][10]}));
 await advance();await page.mouse.move(195,260);await advance();const hover=await strengths();
 await page.mouse.down();await advance();const pressed=await strengths();
 await page.mouse.up();await advance();const released=await strengths();assert.deepEqual(released,hover);
 await page.evaluate(()=>document.querySelector('#view').dispatchEvent(new PointerEvent('pointerleave',{pointerId:1,pointerType:'mouse'})));await advance();const absent=await strengths();assert.equal(absent.flow,0);assert.equal(absent.magnetic,0);assert.equal(absent.parcels,0);
 const row={version,hover,pressed,released,absent,adapter:await page.evaluate(()=>probe.adapter)};
 if(version==='after')row.execution=await page.evaluate(async()=>{
  const {createCycleGpu}=await import('./fawn-cycle-gpu.js');const d=probe.device,c=document.querySelector('#view');const format=navigator.gpu.getPreferredCanvasFormat();const gpu=createCycleGpu(d,c,format,1024);
  const target=d.createTexture({size:[c.width,c.height],format,usage:GPUTextureUsage.RENDER_ATTACHMENT});const results=[];
  for(const [effect,weights]of[['flow',[1,0,0]],['wave',[0,1,0]],['particles',[0,0,1]]]){
   for(let i=0;i<30;i++){gpu.uniforms(.016,i*.016,0,{x:.55,y:.45,hover:true,down:true,dx:0,dy:0,press:i===0?1:0},4,weights);const e=d.createCommandEncoder();gpu.simulate(e,effect,4);gpu.advanceParcels(e);gpu.draw(e,target.createView(),weights,i*.016);d.queue.submit([e.finish()]);await d.queue.onSubmittedWorkDone();}
   let finite=true;for(const label of['Cycle parcels 0','Cycle parcels 1','Cycle surface 0','Cycle surface 1']){const b=probe.buffers.get(label),r=d.createBuffer({size:b.size,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});const e=d.createCommandEncoder();e.copyBufferToBuffer(b,0,r,0,b.size);d.queue.submit([e.finish()]);await r.mapAsync(GPUMapMode.READ);finite&&=new Float32Array(r.getMappedRange()).every(Number.isFinite);r.unmap();r.destroy();}results.push({effect,finite});
  }gpu.destroy();target.destroy();return results;
 });
 assert.deepEqual(errors,[]);assert.deepEqual(await page.evaluate(()=>probe.errors),[]);report.cases.push(row);await page.close();
 }
 const [a,b]=report.cases;for(const field of['flow','magnetic','parcels'])assert.ok(Math.abs(b.hover[field]/a.hover[field]-1/3)<1e-6,field);
 assert.equal(b.pressed.flow,a.pressed.flow);assert.equal(b.hover.radius,a.hover.radius);assert.equal(b.pressed.radius,a.pressed.radius);
 for(const field of['magnetic','parcels'])assert.ok(Math.abs(b.pressed[field]/a.pressed[field]-3)<1e-6,field);
 assert.ok(b.execution.every(x=>x.finite));report.passed=true;
}finally{await writeFile(path.join(out,'report.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));await browser.close();server.closeAllConnections();await new Promise(r=>server.close(r));}
