// Check GPU particle fields against an independent CPU stream-function reference.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const file = 'browser/chromium/resources/fawn-start.html';
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-demo-optimization');
const source = {
  candidate: readFileSync(path.join(root, file), 'utf8'),
};
const shaders = Object.values(source).map(html => vm.runInNewContext(
  html.split('<script>')[1].split('(async () => {')[0] + '; COMPUTE_WGSL',
));
const server = http.createServer((request, response) => response.end('<title>GPU field oracle</title>'));
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const browser = await chromium.launch({ channel: 'chrome', headless: true, args: ['--enable-unsafe-webgpu', '--use-angle=vulkan', '--enable-features=Vulkan', '--disable-vulkan-surface'] });
try {
  const page = await browser.newPage();
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  const results = await page.evaluate(async shaders => {
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter || adapter.info.isFallbackAdapter) throw new Error('Physical WebGPU adapter required');
    const device = await adapter.requestDevice();
    const errors = [];
    device.addEventListener('uncapturederror', event => errors.push(event.error.message));
    const count = 257;
    const initial = new Float32Array(count * 8);
    for (let i = 0; i < count; i++) {
      initial[i * 8] = Math.sin(i * 17) * 1.65;
      initial[i * 8 + 1] = Math.cos(i * 11) * 1.03;
      initial[i * 8 + 2] = Math.sin(i) * 1.5;
      initial[i * 8 + 3] = Math.cos(i) * 1.5;
      initial.set([.3, .7, .9, .002], i * 8 + 4);
    }
    initial[0] = 0; initial[1] = 0;
    const input = device.createBuffer({ size: initial.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
    device.queue.writeBuffer(input, 0, initial);
    const uniform = device.createBuffer({ size: 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    const outputs = shaders.map(() => device.createBuffer({ size: initial.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC }));
    const reads = shaders.map(() => device.createBuffer({ size: initial.byteLength, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ }));
    const pipelines = shaders.map(code => device.createComputePipeline({ layout: 'auto', compute: { module: device.createShaderModule({ code }), entryPoint: 'main' } }));
    const groups = pipelines.map((pipeline, i) => device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
      { binding: 0, resource: { buffer: uniform } },
      { binding: 1, resource: { buffer: input } },
      { binding: 2, resource: { buffer: outputs[i] } },
    ] }));
    const normalize = (x, y) => {
      const length = Math.sqrt(Math.max(x * x + y * y, 1e-6));
      return [x / length, y / length];
    };
    const smooth = (a, b, x) => {
      const t = Math.max(0, Math.min(1, (x - a) / (b - a)));
      return t * t * (3 - 2 * t);
    };
    function field(phase, x, y, force, time) {
      const [mx, my] = normalize(Math.fround(.2) - x, Math.fround(-.3) - y);
      const dist = Math.max((Math.fround(.2) - x) ** 2 + (Math.fround(-.3) - y) ** 2, .018);
      const [ox, oy] = normalize(-y, x);
      const [ix, iy] = normalize(x + Math.fround(.0001), y);
      if (phase === 1) {
        // Evaluate derivatives independently by finite differences of the stream function.
        const stream = (x, y) => Math.cos(x * 1.5707963) * Math.cos(y * 1.5707963)
          * (1 + .35 * Math.sin(x * 2 + time * .2) * Math.sin(y * 2 - time * .16));
        const epsilon = 1e-5;
        return [
          (stream(x, y + epsilon) - stream(x, y - epsilon)) / (2 * epsilon) * .045
            + mx * force * .0014 / dist,
          -(stream(x + epsilon, y) - stream(x - epsilon, y)) / (2 * epsilon) * .045
            + my * force * .0014 / dist,
        ];
      }
      if (phase === 2) return [ox * .035 - ix * .025 - mx * force * .0018 / dist,
        oy * .035 - iy * .025 - my * force * .0018 / dist];
      const radius = Math.hypot(x, y);
      const orbit = .020 + .034 * (1 - smooth(.18, 1.05, radius));
      const inward = .006 + .014 * (1 - smooth(.24, 1.08, radius));
      return [ox * orbit - ix * inward + (mx * .0014 - my * .0006) * force / dist,
        oy * orbit - iy * inward + (my * .0014 + mx * .0006) * force / dist];
    }
    function reference(input, phase, force) {
      const output = input.slice();
      const dt = Math.fround(1 / 240), time = Math.fround(13.7);
      for (let i = 0; i < input.length; i += 8) {
        const x = input[i], y = input[i + 1];
        const a = field(Math.floor(phase) % 3, x, y, force, time);
        const b = field((Math.floor(phase) + 1) % 3, x, y, force, time);
        const blend = Math.fround(phase) % 1;
        const f = a.map((v, axis) => v * (1 - blend) + b[axis] * blend
          - Math.sign(input[i + axis]) * smooth(.80, .98, Math.abs(input[i + axis])) * .09
          - input[i + 2 + axis] * .010);
        const velocity = f.map((v, axis) => input[i + 2 + axis] * .986 + v * dt * 60);
        const speed = Math.hypot(...velocity);
        if (speed > 1.75) velocity.forEach((v, axis) => { velocity[axis] = v * 1.75 / speed; });
        for (let axis = 0; axis < 2; axis++) {
          let position = input[i + axis] + velocity[axis] * dt;
          if (Math.abs(position) > .98) {
            const side = Math.sign(position);
            position = side * (1.96 - Math.abs(position));
            velocity[axis] = -Math.sign(position) * Math.abs(velocity[axis]) * .92;
          }
          output[i + axis] = position;
          output[i + 2 + axis] = velocity[axis];
        }
        output[i + 7] = Math.max(.0016, Math.min(.004, .0016 + speed * .0018));
      }
      return output;
    }
    const cases = [];
    for (const phase of [0, .001, .25, .5, .999, 1, 1.001, 1.5, 1.999, 2, 2.5, 2.999]) {
      for (const force of [0, 4]) {
        for (const aspect of [320 / 844, 390 / 844, 1, 1.6, 844 / 390]) {
          device.queue.writeBuffer(uniform, 0, new Float32Array([1 / 240, 13.7, aspect, force, .2, -.3, 1, phase]));
          const encoder = device.createCommandEncoder();
          const pass = encoder.beginComputePass();
          pipelines.forEach((pipeline, i) => {
            pass.setPipeline(pipeline); pass.setBindGroup(0, groups[i]);
            pass.dispatchWorkgroups(2, 2);
          });
          pass.end();
          outputs.forEach((buffer, i) => encoder.copyBufferToBuffer(buffer, 0, reads[i], 0, initial.byteLength));
          device.queue.submit([encoder.finish()]);
          await Promise.all(reads.map(read => read.mapAsync(GPUMapMode.READ)));
          const values = [reference(initial, phase, force), new Float32Array(reads[0].getMappedRange())];
          let maxAbsoluteError = 0;
          for (let i = 0; i < initial.length; i++) {
            if (!Number.isFinite(values[1][i])) throw new Error('Non-finite particle output');
            maxAbsoluteError = Math.max(maxAbsoluteError, Math.abs(values[0][i] - values[1][i]));
          }
          reads.forEach(read => read.unmap());
          cases.push({ phase, force, aspect, maxAbsoluteError });
        }
      }
    }
    const result = { cases, errors, adapter: { vendor: adapter.info.vendor, architecture: adapter.info.architecture, isFallbackAdapter: adapter.info.isFallbackAdapter } };
    device.destroy();
    return result;
  }, shaders);
  assert.deepEqual(results.errors, []);
  for (const result of results.cases) assert.ok(result.maxAbsoluteError <= 1e-6, JSON.stringify(result));
  mkdirSync(output, { recursive: true });
  writeFileSync(path.join(output, 'particle-fields.json'), JSON.stringify({
    classification: 'Physical Chrome application-shader regression, not runtime qualification',
    oracle: 'CPU finite-difference stream function and independent integration', browser: browser.version(), observedAt: new Date().toISOString(),
    sourceHashes: Object.fromEntries(Object.entries(source).map(([variant, html]) => [variant, createHash('sha256').update(html).digest('hex')])),
    oracleSourceHash: createHash('sha256').update(readFileSync(fileURLToPath(import.meta.url))).digest('hex'),
    absoluteTolerance: 1e-6, ...results, passed: true,
  }, null, 2) + '\n');
  console.log('All normalized particle fields, blend boundaries, pointer forces, viewport aspects and 2D dispatch passed.');
} finally {
  await browser.close();
  await new Promise(resolve => server.close(resolve));
}
