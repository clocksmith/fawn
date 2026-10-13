import { chromium } from '/Users/xyz/deco/reploid/node_modules/playwright/index.mjs';
import http from 'node:http';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import path from 'node:path';
const root=path.resolve('browser/chromium/resources'), out=path.resolve('bench/out/fawn-cycle-shimmer');
await mkdir(out,{recursive:true});
const file='browser/chromium/resources/fawn-parcel-renderer.js';
const baselineRef=process.env.FAWN_BASELINE_REF||'68e0ff8e4';
const baseline=execFileSync('git',['show',`${baselineRef}:${file}`],{encoding:'utf8'});
const candidate=await readFile(file,'utf8');
const server=http.createServer(async(req,res)=>{try{
 const pathname=new URL(req.url,'http://localhost').pathname;
 res.setHeader('Content-Type',pathname.endsWith('.js')?'text/javascript':pathname.endsWith('.svg')?'image/svg+xml':'text/html');
 res.end(pathname==='/baseline.js'?baseline:await readFile(path.join(root,pathname)));
}catch{res.writeHead(404).end();}});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const browser=await chromium.launch({headless:true,args:['--enable-unsafe-webgpu','--use-angle=swiftshader']});
const report={baselineRef,classification:'Software WebGPU artwork comparison; no native Doe, physical GPU, or performance qualification',sources:{baseline:createHash('sha256').update(baseline).digest('hex'),candidate:createHash('sha256').update(candidate).digest('hex')},screens:[]};
try {
 for(const width of [1100,390]){
 const page=await browser.newPage({viewport:{width,height:760}});
 await page.addInitScript(()=>{
  const p=window.probe={clock:1000,callbacks:new Map(),id:0,buffers:new Map(),errors:[],writes:[]};
  Object.defineProperty(performance,'now',{value:()=>p.clock});
  requestAnimationFrame=cb=>{p.callbacks.set(++p.id,cb);return p.id;};cancelAnimationFrame=id=>p.callbacks.delete(id);
  const request=GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice=async function(...args){const d=p.device=await request.apply(this,args);p.adapter={vendor:this.info.vendor,architecture:this.info.architecture};d.addEventListener('uncapturederror',e=>p.errors.push(e.error.message));const create=d.createBuffer.bind(d);d.createBuffer=o=>{const b=create(o);if(o.label)p.buffers.set(o.label,b);return b;};return d;};
 });
 await page.goto(`http://127.0.0.1:${server.address().port}/fawn-cycle.html`);
 await page.waitForFunction(()=>probe.device&&probe.callbacks.size>0,null,{polling:50});
 await page.locator('summary').click();await page.locator('#formation-select').selectOption('free');await page.locator('summary').click();
 await page.evaluate(async()=>{for(let i=0;i<24;i++){probe.clock+=33;const callbacks=[...probe.callbacks.values()];probe.callbacks.clear();callbacks.forEach(cb=>cb(probe.clock));await probe.device.queue.onSubmittedWorkDone();await Promise.resolve();}});
 const result=await page.evaluate(async()=>{
  const [{createParcelRenderer:before},{createParcelRenderer:after}]=await Promise.all([import('/baseline.js'),import('/fawn-parcel-renderer.js')]);
  const d=probe.device,canvas=document.querySelector('#view'),format=navigator.gpu.getPreferredCanvasFormat();
  const parcels=[probe.buffers.get('Cycle parcels 0'),probe.buffers.get('Cycle parcels 1')],count=parcels[0].size/48;
  const empty=d.createBuffer({size:48,usage:GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_DST});
  d.queue.writeBuffer(empty,0,new Float32Array([100,100,0,0,0,0,0,0,0,0,0,0]));
  const renderers={before:before(d,canvas,format,count,parcels),after:after(d,canvas,format,count,parcels),emptyBefore:before(d,canvas,format,1,[empty]),emptyAfter:after(d,canvas,format,1,[empty])};
  const target=d.createTexture({size:[canvas.width,canvas.height],format,usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});
  const pitch=Math.ceil(canvas.width*4/256)*256;
  const read=d.createBuffer({size:pitch*canvas.height,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
  const shots={};const raw={};
  for(const [effect,weights] of [['flow',[1,0,0]],['wave',[0,1,0]],['particles',[0,0,1]]])for(const [version,renderer] of Object.entries(renderers).filter(([name])=>effect==='flow'||!name.startsWith('empty')))for(const time of effect==='flow'?[1,1.4]:[1]){
   const e=d.createCommandEncoder();renderer.draw(e,target.createView(),0,weights,time);e.copyTextureToBuffer({texture:target},{buffer:read,bytesPerRow:pitch},{width:canvas.width,height:canvas.height});d.queue.submit([e.finish()]);await read.mapAsync(GPUMapMode.READ);const data=new Uint8ClampedArray(canvas.width*canvas.height*4),mapped=new Uint8Array(read.getMappedRange());
   for(let y=0;y<canvas.height;y++)data.set(mapped.subarray(y*pitch,y*pitch+canvas.width*4),y*canvas.width*4);read.unmap();
   if(format.startsWith('bgra'))for(let i=0;i<data.length;i+=4){const r=data[i];data[i]=data[i+2];data[i+2]=r;}
   const key=`${effect}-${version}-${time}`;raw[key]=data;
   if(effect==='flow'){const image=document.createElement('canvas');image.width=canvas.width;image.height=canvas.height;image.getContext('2d').putImageData(new ImageData(data,canvas.width,canvas.height),0,0);shots[key]=image.toDataURL().split(',')[1];}
  }
  function difference(a,b){let sum=0,changed=0,max=0;for(let i=0;i<a.length;i+=4){let delta=0;for(let c=0;c<3;c++)delta+=Math.abs(a[i+c]-b[i+c]);sum+=delta;max=Math.max(max,delta);if(delta>0)changed++;}return {meanChannelDelta:sum/(a.length/4*3),changedFraction:changed/(a.length/4),maxRGBDelta:max};}
  const effects={};for(const effect of ['flow','wave','particles'])effects[effect]=difference(raw[`${effect}-before-1`],raw[`${effect}-after-1`]);
  const chroma=data=>{let total=0;for(let i=0;i<data.length;i+=4){const rgb=[data[i],data[i+1],data[i+2]];total+=Math.max(...rgb)-Math.min(...rgb);}return total/(data.length/4);};
  effects.chroma={before:chroma(raw['flow-before-1']),after:chroma(raw['flow-after-1'])};
  effects.backgroundLight={};for(const v of ['Before','After']){const d=raw[`flow-empty${v}-1`];let sum=0,max=0;for(let i=0;i<d.length;i+=4){const light=(d[i]+d[i+1]+d[i+2])/3;sum+=light;max=Math.max(max,light);}effects.backgroundLight[v]={mean:sum/(d.length/4),max};}
  effects.background=difference(raw['flow-emptyBefore-1'],raw['flow-emptyAfter-1']);
  effects.backgroundAnimation=difference(raw['flow-emptyAfter-1'],raw['flow-emptyAfter-1.4']);
  effects.animation=difference(raw['flow-after-1'],raw['flow-after-1.4']);
  Object.values(renderers).forEach(renderer=>renderer.destroy());empty.destroy();target.destroy();read.destroy();
  return {shots,effects,adapter:probe.adapter,errors:probe.errors,count};
 });
 for(const [key,data]of Object.entries(result.shots))await writeFile(path.join(out,`${width}-${key}.png`),Buffer.from(data,'base64'));
 delete result.shots;report.screens.push({width,...result});
 if(result.errors.length||result.effects.wave.changedFraction!==0||result.effects.particles.changedFraction!==0||result.effects.flow.changedFraction===0||result.effects.animation.changedFraction===0||result.effects.background.changedFraction===0||result.effects.backgroundAnimation.changedFraction===0||result.effects.backgroundLight.After.mean<=result.effects.backgroundLight.Before.mean||result.effects.backgroundLight.After.max<=result.effects.backgroundLight.Before.max)throw Error('Material comparison failed');
 await page.locator('#pause').click();await page.screenshot({path:path.join(out,`${width}-application.png`)});await page.close();
 }
 report.passed=true;
}finally{await writeFile(path.join(out,'comparison.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));await browser.close();server.close();}
