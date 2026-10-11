// Source-bound GPU pixel diagnostics for a black magnetic fluid with spectral reflections.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import http from 'node:http';
import { resolve } from 'node:path';
import vm from 'node:vm';

const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const file = 'browser/chromium/resources/fawn-magnetic-fluids.html';
const baselineRef = '1bd50de46';
const sources = { baseline: execFileSync('git', ['show', `${baselineRef}:${file}`], { encoding: 'utf8' }),
  candidate: await readFile(file, 'utf8') };
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const shaders = Object.fromEntries(Object.entries(sources).map(([name, source]) => [name,
  vm.runInNewContext(source.split('<script>')[1].split('(async () => {')[0]
    + '; ({ compute: COMPUTE_WGSL, render: RENDER_WGSL })') ]));
assert.equal(shaders.candidate.compute, shaders.baseline.compute, 'simulation shader stays exact');
const output = resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-magnetic-material');
await mkdir(output, { recursive: true });
const report = { classification: 'Chrome WebGPU offscreen shader pixel diagnostics; no phone/performance claim',
  observedAt: new Date().toISOString(), baselineRef,
  sourceHashes: Object.fromEntries(Object.entries(sources).map(([name, source]) => [name, hash(source)])),
  computeHash: hash(shaders.candidate.compute), cases: [], passed: false };
const server = http.createServer((request, response) => response.end('<title>Magnetic material GPU check</title>'));
await new Promise(done => server.listen(0, '127.0.0.1', done));
const browser = await chromium.launch({ channel: 'chrome', headless: true,
  args: ['--enable-unsafe-webgpu', '--use-angle=vulkan', '--enable-features=Vulkan', '--disable-vulkan-surface'] });
try {
  report.browser = browser.version();
  const page = await browser.newPage(); await page.goto(`http://127.0.0.1:${server.address().port}`);
  for (const [name, grid, force, fixture, width, height] of [
    ['flat', 256, 0, 'flat', 512, 512],
    ['idle', 256, 0, 'simulation', 512, 512],
    ['pressed', 256, 1, 'simulation', 390, 844],
    ['default-grid', 1024, 0, 'simulation', 512, 512],
  ]) {
    const result = await page.evaluate(async ({ shaders, grid, force, fixture, width, height }) => {
      const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
      if (!adapter || adapter.info.isFallbackAdapter) throw Error('Physical WebGPU adapter required');
      const device = await adapter.requestDevice();
      const errors = []; device.addEventListener('uncapturederror', e => errors.push(e.error.message));
      const format = navigator.gpu.getPreferredCanvasFormat();
      const uniform = device.createBuffer({ size: 48, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      device.queue.writeBuffer(uniform, 0, new Float32Array([grid, grid, 1 / grid, 1 / grid,
        .65, .40, force, 0, 1 / 60 * 1.1, 4, 0, 0]));
      const fields = [0, 1].map(() => device.createBuffer({ size: grid * grid * 16,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC }));
      if (fixture === 'simulation') {
        const pipeline = device.createComputePipeline({ layout: 'auto', compute: {
          module: device.createShaderModule({ code: shaders.candidate.compute }), entryPoint: 'main' } });
        const groups = [0, 1].map(i => device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
          { binding: 0, resource: { buffer: uniform } }, { binding: 1, resource: { buffer: fields[i] } },
          { binding: 2, resource: { buffer: fields[1 - i] } },
        ] }));
        const encoder = device.createCommandEncoder(), pass = encoder.beginComputePass(); pass.setPipeline(pipeline);
        for (let i = 0; i < 120; i++) {
          pass.setBindGroup(0, groups[i % 2]); pass.dispatchWorkgroups(Math.ceil(grid / 16), Math.ceil(grid / 16));
        }
        pass.end(); device.queue.submit([encoder.finish()]); await device.queue.onSubmittedWorkDone();
      }
      const images = [];
      for (const [variant, code] of Object.entries(shaders)) {
        const module = device.createShaderModule({ code: code.render });
        const pipeline = device.createRenderPipeline({ layout: 'auto', vertex: { module, entryPoint: 'vs' },
          fragment: { module, entryPoint: 'fs', targets: [{ format }] }, primitive: { topology: 'triangle-list' } });
        const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
          { binding: 0, resource: { buffer: uniform } }, { binding: 1, resource: { buffer: fields[0] } },
        ] });
        const target = device.createTexture({ size: [width, height], format,
          usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
        const bytesPerRow = Math.ceil(width * 4 / 256) * 256;
        const read = device.createBuffer({ size: bytesPerRow * height,
          usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginRenderPass({ colorAttachments: [{ view: target.createView(),
          loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 } }] });
        pass.setPipeline(pipeline); pass.setBindGroup(0, group); pass.draw(3); pass.end();
        encoder.copyTextureToBuffer({ texture: target }, { buffer: read, bytesPerRow }, { width, height });
        device.queue.submit([encoder.finish()]); await read.mapAsync(GPUMapMode.READ);
        const raw = new Uint8Array(read.getMappedRange()), rgba = new Uint8ClampedArray(width * height * 4);
        let dark = 0, colored = 0, clipped = 0; const hues = [0, 0, 0];
        for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
          const input = y * bytesPerRow + x * 4, out = (y * width + x) * 4;
          const rgb = [raw[input + (format.startsWith('bgra') ? 2 : 0)], raw[input + 1],
            raw[input + (format.startsWith('bgra') ? 0 : 2)]];
          rgba.set([...rgb, 255], out);
          const high = Math.max(...rgb), low = Math.min(...rgb);
          if (high <= 25) dark++;
          if (high > 32 && high - low > 20) { colored++; hues[rgb.indexOf(high)]++; }
          if (high === 255) clipped++;
        }
        const canvas = document.createElement('canvas'); canvas.width = width; canvas.height = height;
        canvas.getContext('2d').putImageData(new ImageData(rgba, width, height), 0, 0);
        images.push({ variant, width, height, darkFraction: dark / (width * height),
          coloredFraction: colored / (width * height), hues, clippedFraction: clipped / (width * height),
          png: canvas.toDataURL('image/png').split(',')[1] });
        read.unmap(); read.destroy(); target.destroy();
      }
      await device.queue.onSubmittedWorkDone();
      const info = { vendor: adapter.info.vendor, architecture: adapter.info.architecture, format };
      device.destroy(); return { images, errors, adapter: info };
    }, { shaders, grid, force, fixture, width, height });
    assert.deepEqual(result.errors, []);
    for (const row of result.images) {
      const png = Buffer.from(row.png, 'base64'); delete row.png;
      const artifact = `${name}-${row.variant}.png`; row.artifact = artifact; row.sha256 = hash(png);
      await writeFile(`${output}/${artifact}`, png);
    }
    const candidate = result.images.find(row => row.variant === 'candidate');
    const baseline = result.images.find(row => row.variant === 'baseline');
    if (fixture === 'flat') {
      assert.equal(candidate.darkFraction, 1, 'flat fluid is black');
      assert.equal(baseline.darkFraction, 0, 'preceding shader paints the whole flat surface');
    } else {
      assert.ok(candidate.darkFraction > .60, 'the majority of the fluid stays dark');
      assert.ok(candidate.coloredFraction > .01, 'visible spectral reflection bands remain');
      assert.ok(candidate.hues.every(count => count > 100), 'rainbow includes red, green and blue regions');
      assert.ok(candidate.clippedFraction < .01, 'broad clipped highlights are removed');
    }
    report.cases.push({ name, grid, force, fixture, ...result });
  }
  report.passed = true; console.log('Black flat surface, spectral gradients, dark fluid body and exact simulation shader identity passed');
} catch (error) { report.error = error.stack; throw error; }
finally {
  await writeFile(`${output}/results.json`, JSON.stringify(report, null, 2) + '\n');
  await browser.close(); await new Promise(done => server.close(done));
}
