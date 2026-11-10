// Browser boundary regression: Dawn owns zero-size mappings; the inner provider must not keep one.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdir, readFile, realpath, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: {
  browser: { type: 'string' }, provider: { type: 'string' },
  'doe-library': { type: 'string' }, out: { type: 'string' },
} });
assert(values.browser && values.out, '--browser and --out are required');
assert(['dawn', 'doe'].includes(values.provider), '--provider must be dawn or doe');
assert(values.provider !== 'doe' || values['doe-library'], '--doe-library is required for Doe');
const here = dirname(fileURLToPath(import.meta.url));
const require = createRequire(resolve(here, '../package.json'));
const { chromium } = require('playwright-core');
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const out = resolve(values.out);
await mkdir(out, { recursive: false });
const browserPath = await realpath(values.browser);
const args = ['--enable-unsafe-webgpu', '--ignore-gpu-blocklist', `--use-webgpu-runtime=${values.provider}`];
const result = { claimStatus: 'diagnostic', requestedProvider: values.provider,
  pageRuntimeOwnershipVerified: false, browserPath, args,
  browserSha256: digest(await readFile(browserPath)),
  harnessSha256: digest(await readFile(fileURLToPath(import.meta.url))), passed: false };
if (values['doe-library']) {
  const path = await realpath(values['doe-library']);
  result.library = { path, sha256: digest(await readFile(path)) };
  args.push(`--doe-webgpu-library-path=${path}`);
}
let browser;
try {
  browser = await chromium.launch({ executablePath: browserPath, headless: true,
    chromiumSandbox: true, ignoreDefaultArgs: ['--enable-unsafe-swiftshader'], args });
  result.browserVersion = browser.version();
  const cdp = await browser.newBrowserCDPSession();
  result.actualCommandLine = await cdp.send('Browser.getBrowserCommandLine');
  result.systemInfo = await cdp.send('SystemInfo.getInfo');
  assert(!result.systemInfo.gpu.devices.some(device => /swiftshader|llvmpipe|software/i.test(
    JSON.stringify(device))), 'Software adapter rejected');
  const page = await browser.newPage();
  // A loopback origin is required for WebGPU. No application or network resources are loaded.
  await page.route('http://127.0.0.1/**', route => route.fulfill({ body: '<!doctype html>' }));
  await page.goto('http://127.0.0.1/zero-size-buffer');
  result.controls = await page.evaluate(async () => {
    const bounded = async promise => {
      let timer;
      try {
        return await Promise.race([promise, new Promise((_, reject) => {
          timer = setTimeout(() => reject(Error('GPU completion unknown after 5000 ms')), 5000);
        })]);
      } finally { clearTimeout(timer); }
    };
    const adapter = await bounded(navigator.gpu.requestAdapter());
    if (!adapter) throw Error('Adapter unavailable');
    const device = await bounded(adapter.requestDevice());
    const errors = [];
    device.addEventListener('uncapturederror', event => errors.push(event.error.message));
    const resources = [];
    const own = resource => { resources.push(resource); return resource; };
    try {
      const shader = device.createShaderModule({ code: `
        @vertex fn vertex(@builtin(vertex_index) i:u32)->@builtin(position) vec4f {
          return vec4f(f32((i<<1u)&2u)*2.0-1.0,f32(i&2u)*2.0-1.0,0.0,1.0);
        }
        @fragment fn fragment()->@location(0) vec4f {return vec4f(1.0,0.0,0.0,1.0);}` });
      const pipeline = device.createRenderPipeline({ layout: 'auto',
        vertex: { module: shader, entryPoint: 'vertex' },
        fragment: { module: shader, entryPoint: 'fragment', targets: [{ format: 'rgba8unorm' }] } });
      const cases = [];
      for (const [size, mappedAtCreation, unmap] of [[0, true, true], [0, false, false],
        [8, true, true], [0, true, false], [8, true, false]]) {
        const buffer = own(device.createBuffer({ size, usage: GPUBufferUsage.INDEX, mappedAtCreation }));
        const before = buffer.mapState;
        const mappedBytes = mappedAtCreation ? buffer.getMappedRange().byteLength : null;
        if (unmap) buffer.unmap();
        const after = buffer.mapState;
        const texture = own(device.createTexture({ size: [2, 2], format: 'rgba8unorm',
          usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC }));
        const readback = own(device.createBuffer({ size: 512,
          usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ }));
        device.pushErrorScope('validation');
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginRenderPass({ colorAttachments: [{ view: texture.createView(),
          loadOp: 'clear', storeOp: 'store', clearValue: [0, 0, 0, 0] }] });
        pass.setPipeline(pipeline);
        pass.setIndexBuffer(buffer, 'uint16');
        pass.drawIndexed(0);
        pass.draw(3);
        pass.end();
        encoder.copyTextureToBuffer({ texture }, { buffer: readback, bytesPerRow: 256 }, [2, 2]);
        device.queue.submit([encoder.finish()]);
        await bounded(device.queue.onSubmittedWorkDone());
        const error = await bounded(device.popErrorScope());
        const expectError = mappedAtCreation && !unmap;
        let pixels = null;
        if (!error && !expectError) {
          await bounded(readback.mapAsync(GPUMapMode.READ));
          const data = new Uint8Array(readback.getMappedRange());
          pixels = [0, 4, 256, 260].map(offset => [...data.slice(offset, offset + 4)]);
          readback.unmap();
        }
        cases.push({ size, mappedAtCreation, unmap, before, after, mappedBytes,
          expectError, error: error?.message ?? null, pixels });
      }
      return { cases, errors };
    } finally {
      for (const resource of resources) resource.destroy();
      device.destroy();
    }
  });
  assert.deepEqual(result.controls.errors, []);
  for (const row of result.controls.cases) {
    assert.equal(row.before, row.mappedAtCreation ? 'mapped' : 'unmapped');
    assert.equal(row.after, row.mappedAtCreation && !row.unmap ? 'mapped' : 'unmapped');
    if (row.mappedAtCreation) assert.equal(row.mappedBytes, row.size);
    assert.equal(Boolean(row.error), row.expectError, JSON.stringify(row));
    if (!row.expectError) assert.deepEqual(row.pixels, Array.from({ length: 4 }, () => [255, 0, 0, 255]));
  }
  result.passed = true;
} catch (error) {
  result.failure = { message: error.message, stack: error.stack };
  process.exitCode = 1;
} finally {
  await browser?.close();
  result.browserClosed = browser ? !browser.isConnected() : null;
  await writeFile(resolve(out, 'result.json'), JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify({ passed: result.passed, out, failure: result.failure?.message }));
}
