import assert from 'node:assert/strict';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import http from 'node:http';
import path from 'node:path';
import {chromium} from '/Users/xyz/deco/reploid/node_modules/playwright/index.mjs';
const root=path.resolve('browser/chromium/resources');
const report={passed:false,classification:'Software WebGPU direction diagnostic; not physical performance or Safari qualification',sourceHash:createHash('sha256').update(await readFile(path.join(root,'fawn-cycle-gpu.js'))).digest('hex')};
const server=http.createServer(async(req,res)=>{try{const file=path.basename(new URL(req.url,'http://localhost').pathname);res.setHeader('Content-Type',file.endsWith('.js')?'text/javascript':file.endsWith('.svg')?'image/svg+xml':'text/html');res.end(await readFile(path.join(root,file)));}catch{res.writeHead(404).end();}});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const browser=await chromium.launch({headless:true,args:['--enable-unsafe-webgpu','--use-angle=swiftshader']});
try{
 const page=await browser.newPage({viewport:{width:390,height:600}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.addInitScript(()=>{requestAnimationFrame=()=>1;cancelAnimationFrame=()=>{};});
 await page.goto(`http://127.0.0.1:${server.address().port}/fawn-cycle.html`);
 await page.waitForFunction(()=>document.querySelector('#status').textContent==='WebGPU active',null,{polling:50});
 report.cases=await page.evaluate(async()=>{
  const {createCycleGpu}=await import('./fawn-cycle-gpu.js');const adapter=await navigator.gpu.requestAdapter();const d=await adapter.requestDevice();const errors=[];d.addEventListener('uncapturederror',e=>errors.push(e.error.message));
  const buffers=new Map(),uniforms={};const create=d.createBuffer.bind(d);d.createBuffer=o=>{const b=create(o);buffers.set(o.label,b);return b;};const write=d.queue.writeBuffer.bind(d.queue);d.queue.writeBuffer=(b,o,data,...rest)=>{if(b.usage&GPUBufferUsage.UNIFORM)uniforms[b.label]=Array.from(data);return write(b,o,data,...rest);};
  const gpu=createCycleGpu(d,document.querySelector('#view'),navigator.gpu.getPreferredCanvasFormat(),1024);
  const read=async b=>{const r=create({size:b.size,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});const e=d.createCommandEncoder();e.copyBufferToBuffer(b,0,r,0,b.size);d.queue.submit([e.finish()]);await r.mapAsync(GPUMapMode.READ);const a=Array.from(new Float32Array(r.getMappedRange()));r.unmap();r.destroy();return a;};
  const results=[];
  for(const [mode,weights] of [['magnetic',[0,1,0]],['blend',[0,.5,.5]],['particles',[0,0,1]]]){
   const states={};let hoverForce;
   for(const input of ['neutral','hover','pressed']){
    gpu.reset();const before=await read(buffers.get('Cycle parcels 0'));
    gpu.uniforms(.016,0,0,{x:.5,y:.5,hover:input!=='neutral',down:input==='pressed',dx:0,dy:0,press:0},4,weights);
    const e=d.createCommandEncoder();gpu.advanceParcels(e);d.queue.submit([e.finish()]);
    const after=await read(buffers.get('Cycle parcels 1'));states[input]={before,after,force:uniforms['Cycle parcel uniforms'][10]};
   }
   let radialDelta=0,count=0;
   for(let i=0;i<states.neutral.before.length;i+=12){const x=states.neutral.before[i],y=states.neutral.before[i+1],r=Math.hypot(x,y);if(r>.15&&r<.6){radialDelta+=((states.pressed.after[i]-states.neutral.after[i])*x+(states.pressed.after[i+1]-states.neutral.after[i+1])*y)/r;count++;}}
   for(let i=0;i<30;i++){gpu.uniforms(.016,i*.016,0,{x:.5,y:.5,hover:true,down:true,dx:0,dy:0,press:0},4,weights);const e=d.createCommandEncoder();gpu.simulate(e,mode==='particles'?'particles':'wave',4);gpu.advanceParcels(e);d.queue.submit([e.finish()]);}
   const sustained=await read(buffers.get('Cycle parcels 1'));
   results.push({mode,hoverForce:states.hover.force,pressedForce:states.pressed.force,meanOutwardDisplacementFromClick:radialDelta/count,samples:count,finite:sustained.every(Number.isFinite)});
  }
  gpu.destroy();await d.queue.onSubmittedWorkDone();d.destroy();if(errors.length)throw new Error(errors.join('\n'));return results;
 });
 assert.deepEqual(errors,[]);const [magnetic,blend,particles]=report.cases;
 assert.ok(magnetic.meanOutwardDisplacementFromClick<0);assert.ok(particles.meanOutwardDisplacementFromClick>0);assert.equal(blend.meanOutwardDisplacementFromClick,0);
 assert.equal(magnetic.pressedForce,-particles.pressedForce);assert.ok(Math.abs(magnetic.pressedForce-80.01)<.00001);
 assert.equal(blend.pressedForce,0);for(const row of report.cases){assert.ok(row.finite);assert.ok(Math.abs(row.hoverForce-2/3)<.00001);}
 report.passed=true;
}finally{await mkdir('bench/out/fawn-pointer-direction',{recursive:true});await writeFile('bench/out/fawn-pointer-direction/report.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));await browser.close();server.closeAllConnections();await new Promise(r=>server.close(r));}
