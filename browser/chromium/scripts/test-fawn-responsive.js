// Chrome WebGPU layout/material regressions; emulated sizes do not qualify phone hardware or Doe.
import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import http from 'node:http';
import path from 'node:path';
import vm from 'node:vm';
const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const resources = new URL('../resources/', import.meta.url);
const names = ['fawn-start.html', 'fawn-prismatic-fluids.html', 'fawn-magnetic-fluids.html'];
const baselineRef = process.env.FAWN_BASELINE_REF || '5583b0638';
const baseline = Object.fromEntries(names.map(name => [name,
  execFileSync('git', ['show', `${baselineRef}:browser/chromium/resources/${name}`], { encoding: 'utf8' })]));
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-responsive');
await mkdir(output, { recursive: true });
const report = { passed: false, baselineRef, classification: 'Chrome application render and layout; emulated sizes/DPR; no physical phone or Doe performance claim', cases: [], errors: [], sources: [] };
report.harnessSha256 = createHash('sha256').update(await readFile(new URL(import.meta.url))).digest('hex');
for (const name of [...names, 'fawn-formation.js']) report.sources.push({ name,
  sha256: createHash('sha256').update(await readFile(new URL(name, resources))).digest('hex') });
const server = http.createServer(async (req, res) => {
  const pathname = new URL(req.url, 'http://localhost').pathname;
  if (pathname.startsWith('/baseline/')) {
    const name = pathname.split('/').at(-1);
    if (baseline[name]) { res.setHeader('Content-Type', 'text/html'); res.end(baseline[name]); return; }
    if (name === 'fawn-formation.js' || name === 'fawn-icon.svg') {
      res.setHeader('Content-Type', name.endsWith('.js') ? 'text/javascript' : 'image/svg+xml');
      res.end(await readFile(new URL(name, resources))); return;
    }
  }
  const file = new URL('.' + pathname, resources);
  if (!file.href.startsWith(resources.href)) { res.writeHead(403).end(); return; }
  try { res.setHeader('Content-Type', file.pathname.endsWith('.js') ? 'text/javascript' : 'text/html'); res.end(await readFile(file)); }
  catch { res.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({ channel: 'chrome', headless: true, args: ['--enable-unsafe-webgpu'] });
report.browser = browser.version();
function instrument() {
  const s = globalThis.responsiveProbe = { errors: [], textures: 0, frames: 0 };
  const request = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function(...args) {
    s.adapter = { vendor: this.info.vendor, architecture: this.info.architecture, isFallbackAdapter: this.info.isFallbackAdapter };
    const device = s.device = await request.apply(this, args);
    s.limit = device.limits.maxTextureDimension2D;
    device.addEventListener('uncapturederror', event => s.errors.push(event.error.message));
    const create = device.createTexture.bind(device);
    device.createTexture = (...args) => { s.textures++; return create(...args); };
    const submit = device.queue.submit.bind(device.queue);
    device.queue.submit = (...args) => { s.frames++; return submit(...args); };
    return device;
  };
}
async function geometry(page) {
  return page.evaluate(() => {
    const c = document.querySelector('#view'), r = c.getBoundingClientRect(), style = getComputedStyle(c);
    return { css: [r.width, r.height], buffer: [c.width, c.height], dpr: devicePixelRatio,
      viewport: [innerWidth, innerHeight], limit: responsiveProbe.limit, adapter: responsiveProbe.adapter,
      outline: style.outlineStyle, border: style.borderStyle, shadow: style.boxShadow,
      textures: responsiveProbe.textures, errors: responsiveProbe.errors,
      overflow: document.documentElement.scrollWidth > innerWidth };
  });
}
try {
  // Reproduce the actual HTML frame independently of particle content.
  const old = await browser.newPage(); await old.goto(origin + '/baseline/fawn-start.html');
  await old.waitForFunction(() => !document.querySelector('#pause').disabled);
  await old.locator('#view').focus(); await old.keyboard.press('ArrowRight');
  report.previousOutline = await old.locator('#view').evaluate(c => getComputedStyle(c).outlineStyle);
  assert.equal(report.previousOutline, 'solid'); await old.close();
  for (const name of names) for (const config of [
    { viewport: { width: 320, height: 568 }, deviceScaleFactor: 1 },
    { viewport: { width: 390, height: 844 }, deviceScaleFactor: 3 },
    { viewport: { width: 1600, height: 500 }, deviceScaleFactor: 2 },
  ]) {
    const context = await browser.newContext(config); await context.addInitScript(instrument);
    const page = await context.newPage(); page.on('pageerror', e => report.errors.push(e.message));
    await page.goto(origin + '/' + name); await page.waitForFunction(() => !document.querySelector('#pause').disabled);
    await page.locator('#pause').click();
    await page.locator('#view').focus(); await page.keyboard.press('ArrowRight');
    const check = async () => {
      const g = await geometry(page), maxDpr = name === names[0] ? 2 : Infinity;
      const ratio = Math.min(g.dpr, maxDpr, g.limit / Math.max(...g.css));
      assert.deepEqual(g.css, g.viewport);
      assert.deepEqual(g.buffer, g.css.map(size => Math.max(1, Math.floor(size * ratio))));
      assert.ok(Math.abs(g.buffer[0] / g.buffer[1] - g.css[0] / g.css[1]) < 0.005);
      assert.deepEqual(g.errors, []); assert.equal(g.overflow, false);
      if (name === names[0]) { assert.equal(g.outline, 'none'); assert.equal(g.border, 'none'); assert.equal(g.shadow, 'none'); }
      return g;
    };
    const initial = await check();
    const rotated = { width: config.viewport.height, height: config.viewport.width };
    await page.setViewportSize(rotated);
    await page.waitForFunction(particles => {
      const c = document.querySelector('#view');
      const ratio = Math.min(devicePixelRatio, particles ? 2 : Infinity,
        responsiveProbe.limit / Math.max(c.clientWidth, c.clientHeight));
      return c.width === Math.floor(c.clientWidth * ratio) && c.height === Math.floor(c.clientHeight * ratio);
    }, name === names[0]);
    const resized = await check();
    assert.equal(await page.locator('#pause').textContent(), 'Resume');
    await page.screenshot({ path: path.join(output, `${name}-${config.viewport.width}-rotated.png`) });
    report.cases.push({ name, config, initial, resized }); await context.close();
  }
  // Compare exactly the same retained shader inputs, independent of animation timing.
  const formation = await readFile(new URL('fawn-formation.js', resources), 'utf8');
  const shaders = {};
  for (const [variant, html] of Object.entries({ baseline: baseline[names[1]], candidate: await readFile(new URL(names[1], resources), 'utf8') })) {
    const sandbox = vm.createContext({}); vm.runInContext(formation, sandbox);
    shaders[variant] = vm.runInContext(html.split('<script>')[1].split('(async () => {')[0] + '; RENDER_WGSL', sandbox);
  }
  const page = await browser.newPage(); await page.goto(origin + '/fawn-icon.svg');
  report.material = await page.evaluate(async shaders => {
    const adapter = await navigator.gpu.requestAdapter(), device = await adapter.requestDevice();
    const errors = []; device.addEventListener('uncapturederror', e => errors.push(e.error.message));
    const grid = 16, size = 64, format = 'rgba8unorm';
    const uniform = device.createBuffer({ size: 80, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    device.queue.writeBuffer(uniform, 0, new Float32Array([grid, grid, 1/grid, 1/grid, 0,0,0,0, 0,0,1,0, 0,0,0,0, 1,1,1,0]));
    const values = new Float32Array(grid * grid * 4);
    for (let y=0;y<grid;y++) for (let x=0;x<grid;x++) {
      const v = x < grid/2 ? 0.025 : 0.18; values.set([v, v*0.7, v*0.35, 1], (y*grid+x)*4);
    }
    const buffers = [values, new Float32Array(values.length), new Float32Array(values.length)].map(data => {
      const b=device.createBuffer({size:data.byteLength,usage:GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_DST}); device.queue.writeBuffer(b,0,data); return b;
    });
    const target=device.createTexture({size:[size,size],format,usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});
    const results={};
    for (const [variant, code] of Object.entries(shaders)) {
      const module=device.createShaderModule({code}); const info=await module.getCompilationInfo();
      if(info.messages.some(m=>m.type==='error')) throw Error(info.messages.map(m=>m.message).join('\n'));
      const pipeline=device.createRenderPipeline({layout:'auto',vertex:{module,entryPoint:'vs'},fragment:{module,entryPoint:'fs',targets:[{format}]},
        primitive:{topology:variant==='candidate'?'triangle-strip':'triangle-list'}});
      const transport = device.createBuffer({size:values.byteLength,usage:GPUBufferUsage.STORAGE|GPUBufferUsage.COPY_DST});
      const entries=[uniform,...buffers]; if(variant==='candidate') entries.push(transport);
      const group=device.createBindGroup({layout:pipeline.getBindGroupLayout(0),entries:entries.map((buffer,binding)=>({binding,resource:{buffer}}))});
      const read=device.createBuffer({size:size*256,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});
      const encoder=device.createCommandEncoder(), pass=encoder.beginRenderPass({colorAttachments:[{view:target.createView(),loadOp:'clear',storeOp:'store',clearValue:[0,0,0,1]}]});
      pass.setPipeline(pipeline);pass.setBindGroup(0,group);
      if(variant==='candidate') pass.draw(4); else pass.draw(3);pass.end();encoder.copyTextureToBuffer({texture:target},{buffer:read,bytesPerRow:256},[size,size]);
      device.queue.submit([encoder.finish()]);await read.mapAsync(GPUMapMode.READ);
      const bytes=new Uint8Array(read.getMappedRange());const pixel=x=>Array.from(bytes.slice((32*size+x)*4,(32*size+x)*4+3));
      results[variant]={dim:pixel(8),pigment:pixel(48),edge:pixel(32)};read.unmap();read.destroy();transport.destroy();
    }
    uniform.destroy();buffers.forEach(b=>b.destroy());target.destroy();device.destroy();return{results,errors};
  }, shaders);
  assert.deepEqual(report.material.errors, []);
  const { baseline: before, candidate: after } = report.material.results;
  assert.ok(after.dim.reduce((a,b)=>a+b) > before.dim.reduce((a,b)=>a+b));
  assert.ok(after.pigment.reduce((a,b)=>a+b) > before.pigment.reduce((a,b)=>a+b)*1.25);
  assert.ok(after.pigment.every(v=>v<240), 'Pigment stays colored without clipped white highlights');
  await page.close(); assert.deepEqual(report.errors, []); report.passed=true;
  console.log(JSON.stringify(report));
} finally {
  await writeFile(path.join(output,'report.json'), JSON.stringify(report,null,2)+'\n');
  await browser.close(); await new Promise(resolve=>server.close(resolve));
}
