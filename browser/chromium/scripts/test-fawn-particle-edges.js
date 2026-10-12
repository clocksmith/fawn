// Actual particle and trail shader readbacks; application graphics checks only.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import http from 'node:http';
import vm from 'node:vm';

const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const file = 'browser/chromium/resources/fawn-start.html';
const baselineRef = '52dbb2c8f';
function shaders(html) {
  return vm.runInNewContext(html.slice(html.indexOf('<script>') + 8, html.indexOf('(async () => {'))
    + `; ({ particle: PARTICLE_WGSL, fade: FADE_WGSL,
      blend: typeof PARTICLE_BLEND === 'undefined' ? {
        color: { srcFactor: 'one', dstFactor: 'one', operation: 'add' },
        alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
      } : PARTICLE_BLEND })`);
}
const variants = {
  baseline: shaders(execFileSync('git', ['show', `${baselineRef}:${file}`], { encoding: 'utf8' })),
  candidate: shaders(await readFile(file, 'utf8')),
};
const server = http.createServer((request, response) => response.end('<title>Particle edge checks</title>'));
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
let browser;
try {
  browser = await chromium.launch({ channel: 'chrome', headless: true, args: ['--enable-unsafe-webgpu'] });
  const page = await browser.newPage();
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  const result = await page.evaluate(async variants => {
    const adapter = await navigator.gpu.requestAdapter();
    const device = await adapter.requestDevice();
    const errors = [];
    device.addEventListener('uncapturederror', event => errors.push(event.error.message));
    const size = 256, count = 1024, bytesPerRow = size * 4;
    const target = device.createTexture({ size: [size, size], format: 'rgba8unorm',
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
    const history = device.createTexture({ size: [size, size], format: 'rgba8unorm',
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
    device.queue.writeTexture({ texture: history }, new Uint8Array(size * size * 4).fill(255),
      { bytesPerRow }, [size, size]);
    const uniforms = device.createBuffer({ size: 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    device.queue.writeBuffer(uniforms, 0, new Float32Array([0, 0, 1, 0, 0, 0, 1, 0]));
    const particles = device.createBuffer({ size: count * 32, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
    async function pixels(encoder) {
      const read = device.createBuffer({ size: size * bytesPerRow,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
      encoder.copyTextureToBuffer({ texture: target }, { buffer: read, bytesPerRow }, [size, size]);
      device.queue.submit([encoder.finish()]);
      await read.mapAsync(GPUMapMode.READ);
      const data = new Uint8Array(read.getMappedRange()).slice();
      read.unmap(); read.destroy();
      return data;
    }
    const output = {};
    for (const [name, code] of Object.entries(variants)) {
      const shader = device.createShaderModule({ code: code.particle });
      const pipeline = device.createRenderPipeline({ layout: 'auto',
        vertex: { module: shader, entryPoint: 'vs' },
        fragment: { module: shader, entryPoint: 'fs', targets: [{ format: 'rgba8unorm', blend: code.blend }] }, primitive: { topology: 'triangle-strip' } });
      const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
        { binding: 0, resource: { buffer: uniforms } }, { binding: 1, resource: { buffer: particles } },
      ] });
      const peaks = [];
      for (const [x, y] of [[0, 0], [-.98, 0], [.98, 0], [0, -.98], [0, .98], [.98, .98]]) {
        const data = new Float32Array(count * 8);
        for (let i = 0; i < count; i++) data.set([x, y, 1, 1, .8, .7, .6, .012], i * 8);
        device.queue.writeBuffer(particles, 0, data);
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginRenderPass({ colorAttachments: [{ view: target.createView(),
          clearValue: [0, 0, 0, 0], loadOp: 'clear', storeOp: 'store' }] });
        pass.setPipeline(pipeline); pass.setBindGroup(0, group); pass.draw(4, count); pass.end();
        const image = await pixels(encoder);
        peaks.push(image.reduce((peak, value, i) => i % 4 === 3 ? peak : Math.max(peak, value), 0));
      }
      const fade = device.createShaderModule({ code: code.fade });
      const fadePipeline = device.createRenderPipeline({ layout: 'auto',
        vertex: { module: fade, entryPoint: 'vs' },
        fragment: { module: fade, entryPoint: 'fs', targets: [{ format: 'rgba8unorm' }] } });
      const fadeGroup = device.createBindGroup({ layout: fadePipeline.getBindGroupLayout(0), entries: [
        { binding: 0, resource: device.createSampler({ minFilter: 'linear', magFilter: 'linear' }) },
        { binding: 1, resource: history.createView() },
      ] });
      const encoder = device.createCommandEncoder();
      const pass = encoder.beginRenderPass({ colorAttachments: [{ view: target.createView(),
        clearValue: [0, 0, 0, 0], loadOp: 'clear', storeOp: 'store' }] });
      pass.setPipeline(fadePipeline); pass.setBindGroup(0, fadeGroup); pass.draw(3); pass.end();
      const image = await pixels(encoder);
      const historyEdge = image.slice((128 * size + 254) * 4, (128 * size + 254) * 4 + 3);
      const historyCenter = image.slice((128 * size + 128) * 4, (128 * size + 128) * 4 + 3);
      output[name] = { peaks, historyEdge: [...historyEdge], historyCenter: [...historyCenter] };
    }
    target.destroy(); history.destroy(); uniforms.destroy(); particles.destroy(); device.destroy();
    return { output, errors };
  }, variants);
  assert.deepEqual(result.errors, []);
  const { baseline, candidate } = result.output;
  assert.ok(baseline.peaks.every(value => value > 200), 'Frozen shaders reproduce saturated edge particles');
  assert.ok(candidate.peaks[0] > 40 && candidate.peaks[0] < 140,
    'Dense particle clouds stay visible without saturating into a white highlight');
  assert.ok(candidate.peaks.slice(1).every(value => value === 0), 'No bright particle contribution at any edge');
  assert.ok(candidate.historyEdge.every(value => value < 8), 'Old bright border trails are removed');
  assert.deepEqual(candidate.historyCenter, baseline.historyCenter, 'Interior trail persistence stays intact');
  console.log('GPU edge and corner particles suppressed; border history removed; dense cloud brightness stays bounded');
  console.log(JSON.stringify(result.output));
} finally {
  await browser?.close();
  server.closeAllConnections();
  await new Promise(resolve => server.close(resolve));
}
