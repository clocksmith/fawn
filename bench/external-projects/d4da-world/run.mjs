// Run the existing D4DA page with observer hooks and an independent material oracle.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { createServer } from 'node:http';
import { readFile, writeFile, mkdir, realpath } from 'node:fs/promises';
import { dirname, resolve, sep, extname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';
import { observeWorld } from './observe.mjs';
import { captureTerrainOracle, validateTerrainOracle } from './oracle.mjs';
import { processSnapshot, summarizeFrames } from './process-metrics.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, '../../..');
const { values } = parseArgs({ options: {
  'source-root': { type: 'string' }, out: { type: 'string' },
  browser: { type: 'string', default: '/opt/google/chrome/chrome' },
  provider: { type: 'string', default: 'dawn' },
  'doe-library': { type: 'string' },
  help: { type: 'boolean', default: false },
} });
if (values.help) {
  console.log('Usage: graphics --source-root /path/to/d4da --out bench/out/graphics/run-id [--browser PATH] [--provider dawn|doe --doe-library PATH]\nRequires a graphical display; runs a visible Chromium window.');
  process.exit(0);
}
assert(values['source-root'] && values.out, '--source-root and --out are required');
assert(['dawn', 'doe'].includes(values.provider), '--provider must be dawn or doe');
const source = await realpath(values['source-root']);
const webRoot = await realpath(resolve(source, 'sites/d4da-com'));
const out = resolve(values.out);
await mkdir(out, { recursive: false });
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const writeJSON = (name, value) => writeFile(resolve(out, name), JSON.stringify(value, null, 2) + '\n');
const plan = JSON.parse(await readFile(resolve(here, 'plan.json')));
const result = { schemaVersion: 1, status: 'running', claimStatus: 'diagnostic',
  requestedProvider: values.provider, pageRuntimeOwnershipVerified: false,
  plan, sourceRoot: source, sourceCommit: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: source, encoding: 'utf8' }).trim(),
  sourceDirty: execFileSync('git', ['status', '--porcelain'], { cwd: source, encoding: 'utf8' }),
  cases: [], errors: [], console: [], blockedRequests: [], assets: [], shaders: [] };
const assets = new Map();
const mime = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
  '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.woff2': 'font/woff2' };
let browser, server, page;
try {
  result.browserPath = await realpath(values.browser);
  result.browserSha256 = sha256(await readFile(result.browserPath));
  result.harness = await Promise.all(['run.mjs', 'observe.mjs', 'oracle.mjs', 'process-metrics.mjs', 'plan.json'].map(async name =>
    ({ name, sha256: sha256(await readFile(resolve(here, name))) })));
  const args = [...plan.browserArgs, `--use-webgpu-runtime=${values.provider}`];
  if (values.provider === 'doe') {
    assert(values['doe-library'], 'Forced Doe requires --doe-library');
    const library = await realpath(values['doe-library']);
    result.doeLibrary = { path: library, sha256: sha256(await readFile(library)) };
    throw new Error('Forced-Doe page ownership cannot yet be verified by this harness; refusing to treat requested browser switches as runtime evidence');
  }
  result.browserArgs = args;
  result.headless = false;
  await mkdir(resolve(out, 'assets'));
  await mkdir(resolve(out, 'shaders'));
  server = createServer(async (request, response) => {
    try {
      const pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
      if (pathname === '/__oracle.html') {
        response.writeHead(200, { 'Content-Type': 'text/html' });
        response.end('<!doctype html><script src="/js/d4da-route-config.js"></script>');
        return;
      }
      if (pathname === '/favicon.ico') { response.writeHead(204).end(); return; }
      const path = pathname === '/' ? '/index.html' : pathname;
      if (!assets.has(path)) {
        const target = await realpath(resolve(webRoot, '.' + path));
        assert(target.startsWith(webRoot + sep), `Outside source root: ${path}`);
        const bytes = await readFile(target), hash = sha256(bytes);
        await writeFile(resolve(out, 'assets', hash), bytes);
        assets.set(path, { path, sha256: hash, bytes });
      }
      response.writeHead(200, { 'Content-Type': mime[extname(path)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
      response.end(assets.get(path).bytes);
    } catch (error) { response.writeHead(404).end(String(error)); }
  });
  await new Promise(done => server.listen(0, '127.0.0.1', done));
  const origin = `http://127.0.0.1:${server.address().port}`;
  const playwright = process.env.DOE_PLAYWRIGHT_MODULE || pathToFileURL(resolve(source, 'node_modules/playwright/index.mjs')).href;
  const { chromium } = await import(playwright);
  browser = await chromium.launch({ executablePath: result.browserPath, headless: result.headless, args });
  result.browserVersion = browser.version();
  const cdp = await browser.newBrowserCDPSession();
  result.systemInfo = await cdp.send('SystemInfo.getInfo');
  const context = await browser.newContext({ viewport: plan.viewport, deviceScaleFactor: plan.deviceScaleFactor });
  await context.route('**/*', route => {
    if (new URL(route.request().url()).origin === origin) return route.continue();
    result.blockedRequests.push(route.request().url());
    return route.abort();
  });
  await context.addInitScript(observeWorld);
  page = await context.newPage();
  page.on('pageerror', error => result.errors.push(error.message));
  page.on('console', message => {
    if (['error', 'warning'].includes(message.type())) result.console.push({ type: message.type(), text: message.text() });
  });
  await page.goto(`${origin}/__oracle.html`);
  result.oraclePixels = await page.evaluate(captureTerrainOracle, plan.oracle);
  result.oracle = validateTerrainOracle(result.oraclePixels, plan.oracle);
  assert.deepEqual(await page.evaluate(() => doeGraphics.errors), []);
  assert.deepEqual(result.errors, []);
  await writeJSON('oracle.json', { policy: plan.oracle, result: result.oracle, pixels: result.oraclePixels });
  delete result.oraclePixels;
  console.log('Independent terrain pixel oracle passed');
  await page.goto(origin);
  await page.waitForFunction(() => window.SimulatteWorldRuntime?.isStarted(), null, { timeout: plan.timeoutMs });
  result.preparationMs = await page.evaluate(() => performance.now());
  result.preparationScope = 'Navigation to scene readiness after the terrain oracle in this browser; not a cold-cache measurement';
  result.renderer = await page.evaluate(() => SimulatteWorldRuntime.getRendererProfile());
  assert.equal(result.renderer.capability, 'webgpu', 'Application must not fall back');
  assert.equal(result.renderer.implementation, 'three-tsl');
  result.adapters = await page.evaluate(() => doeGraphics.adapters);
  assert(result.adapters.length > 0, 'Missing physical adapter evidence');
  assert(!result.adapters.some(info => /swiftshader|llvmpipe|software/i.test(JSON.stringify(info))), 'Software adapter rejected');
  result.before = await processSnapshot(cdp);
  for (const item of plan.cases) {
    await page.evaluate(alpha => {
      setTileAlphaMode(alpha);
      doeGraphics.frames = []; doeGraphics.pending = []; doeGraphics.active = true;
    }, item.alpha);
    await page.waitForFunction(count => doeGraphics.frames.length >= count, plan.warmupFrames + plan.sampleFrames,
      { timeout: plan.timeoutMs });
    const measured = await page.evaluate(async ({ warmupFrames, sampleFrames }) => {
      doeGraphics.active = false;
      await Promise.all(doeGraphics.pending);
      return { frames: doeGraphics.frames.slice(warmupFrames, warmupFrames + sampleFrames),
        profile: SimulatteWorldRuntime.getRendererProfile(), errors: doeGraphics.errors,
        canvas: { width: canvas.width, height: canvas.height, connected: canvas.isConnected },
        actualTileAlpha: renderSettings.tileAlpha };
    }, plan);
    const caseResult = { ...item, ...measured };
    result.cases.push(caseResult);
    assert.deepEqual(measured.errors, []);
    assert.equal(measured.profile.capability, 'webgpu');
    assert.equal(measured.profile.deviceLost, false);
    assert(measured.canvas.connected && measured.canvas.width > 0 && measured.canvas.height > 0);
    assert(measured.frames.every(row => row.submissions > 0 && row.canvasAcquires > 0
      && Number.isFinite(row.completedMs) && row.completedMs >= row.cpuWorkMs), 'Missing rendered/completed work');
    await page.locator('#field').screenshot({ path: resolve(out, `${item.id}.png`) });
    caseResult.summary = summarizeFrames(measured.frames);
    caseResult.processSnapshot = await processSnapshot(cdp);
    console.log(`${item.id}: ${measured.frames.length} completed canvas frames`);
    await writeJSON('result.json', result);
  }
  assert.deepEqual(result.errors, []);
  result.lifecycle = await page.evaluate(async () => {
    doeGraphics.active = false;
    const device = sceneRenderer.renderer.backend.device;
    await device.queue.onSubmittedWorkDone();
    await sceneRenderer.dispose();
    const loss = await device.lost;
    return { createdDevices: doeGraphics.createdDevices, destroyedDevices: doeGraphics.destroyedDevices,
      sceneDeviceLossReason: loss.reason };
  });
  assert.equal(result.lifecycle.sceneDeviceLossReason, 'destroyed');
  assert.equal(result.lifecycle.createdDevices, result.lifecycle.destroyedDevices);
  result.status = 'pass';
} catch (error) {
  result.status = 'failed'; result.failure = { message: error.message, stack: error.stack };
  process.exitCode = 1;
  console.error(error.message);
} finally {
  if (page && !page.isClosed()) {
    try {
      const captured = await page.evaluate(() => window.doeGraphics?.shaders ?? []);
      const unique = new Map(captured.map(shader => [sha256(shader.code), shader]));
      for (const [hash, shader] of unique) {
        await writeFile(resolve(out, 'shaders', `${hash}.wgsl`), shader.code);
        result.shaders.push({ sha256: hash, label: shader.label });
      }
      result.gpuErrors = await page.evaluate(() => window.doeGraphics?.errors ?? []);
    } catch (error) { result.captureFailure = error.message; }
  }
  await browser?.close();
  if (server) await new Promise(done => server.close(done));
  result.assets = [...assets.values()].map(({ path, sha256, bytes }) => ({ path, sha256, byteLength: bytes.length }));
  result.finishedAt = new Date().toISOString();
  await writeJSON('result.json', result);
  console.log(resolve(out, 'result.json'));
}
