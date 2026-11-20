#!/usr/bin/env node
// Displayed-canvas capture and timing harness for C2 third-party WebGPU graphics workloads.
//
// Serves hash-verified prepared upstream sources on 127.0.0.1:0, blocks unpinned external traffic,
// launches a visible sandboxed browser (never --no-sandbox, --disable-vulkan-surface, or headless),
// drives a deterministic n/60 animation clock, records raw per-process latency arrays and loaded
// native library sidecars, captures real displayed-canvas PNGs at pinned frames [30, 90, 180],
// and writes a zero-trust campaign receipt verifiable by bench.gates.webgpu_graphics_campaign_gate.

import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { createRequire } from 'node:module';
import { dirname, extname, join, normalize, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const MIME_BY_EXT = Object.freeze({
  '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.mjs': 'application/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.wgsl': 'text/plain; charset=utf-8',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.webp': 'image/webp',
  '.ktx2': 'application/octet-stream',
  '.wasm': 'application/wasm',
  '.bin': 'application/octet-stream',
});

const FORBIDDEN_FLAGS = new Set([
  '--no-sandbox',
  '--disable-setuid-sandbox',
  '--disable-vulkan-surface',
  '--headless',
  '--headless=new',
]);

export function sha256Buffer(buf) {
  return createHash('sha256').update(buf).digest('hex');
}

export function resolveGraphicsLaunchArgs(manifest, backendId, extraArgs = []) {
  const lp = manifest.launchPolicy;
  if (lp.headless !== false || lp.sandbox !== 'enabled' || lp.displayedSurfaceRequired !== true) {
    throw Error(`${manifest.workloadId}: launchPolicy must enforce headed, sandboxed, displayed canvas`);
  }
  const platKey = backendId === 'amd-vulkan' ? 'linux' : backendId === 'apple-metal' ? 'darwin' : null;
  if (!platKey) throw Error(`Unsupported backendId: ${backendId}`);
  const combined = [...lp.commonBrowserArgs, ...(lp.platformBrowserArgs[platKey] ?? []), ...extraArgs];
  assertGraphicsSwitches(manifest, combined);
  return combined;
}

function assertGraphicsSwitches(manifest, args) {
  for (const arg of args) {
    const flagName = arg.split('=')[0];
    if (FORBIDDEN_FLAGS.has(arg) || FORBIDDEN_FLAGS.has(flagName) || manifest.launchPolicy.forbiddenBrowserArgs.includes(arg)) {
      throw Error(`${manifest.workloadId}: forbidden browser launch argument ${arg}`);
    }
  }
}

export function buildDeterministicObserverInitScript({ clockHz = 60, pinnedFrames = [30, 90, 180] }) {
  return `(() => {
    const stepMs = 1000 / ${Number(clockHz)};
    let frameIndex = 0;
    let syntheticNowMs = 0;
    const pinnedSet = new Set(${JSON.stringify(pinnedFrames)});
    const state = {
      adapterInfo: null,
      shaderSha256Inputs: [],
      submissionSequence: 0,
      canvasAcquisitions: 0,
      commandPrepMsSamples: [],
      queueCompletionMsSamples: [],
      frameCompletePromises: new Map(),
      errors: [],
    };
    Object.defineProperty(window, '__fawnC2Observer', { value: state, configurable: false });
    performance.now = () => syntheticNowMs;
    const origRaf = window.requestAnimationFrame.bind(window);
    window.requestAnimationFrame = cb => origRaf(() => {
      frameIndex += 1;
      syntheticNowMs = frameIndex * stepMs;
      const t0 = Date.now();
      cb(syntheticNowMs);
      state.commandPrepMsSamples.push(Date.now() - t0);
    });
    if (navigator.gpu) {
      const origReqAdapter = navigator.gpu.requestAdapter.bind(navigator.gpu);
      navigator.gpu.requestAdapter = async (...args) => {
        const adapter = await origReqAdapter(...args);
        if (!adapter) return null;
        state.adapterInfo = adapter.info ? {
          vendor: adapter.info.vendor ?? '',
          architecture: adapter.info.architecture ?? '',
          device: adapter.info.device ?? '',
          description: adapter.info.description ?? '',
        } : null;
        const origReqDevice = adapter.requestDevice.bind(adapter);
        adapter.requestDevice = async (...dArgs) => {
          const device = await origReqDevice(...dArgs);
          const origShader = device.createShaderModule.bind(device);
          device.createShaderModule = desc => {
            if (desc && typeof desc.code === 'string') state.shaderSha256Inputs.push(desc.code.length);
            return origShader(desc);
          };
          const origSubmit = device.queue.submit.bind(device.queue);
          device.queue.submit = cmdBuffers => {
            const subStart = Date.now();
            state.submissionSequence += 1;
            const res = origSubmit(cmdBuffers);
            const p = device.queue.onSubmittedWorkDone().then(() => {
              state.queueCompletionMsSamples.push(Date.now() - subStart);
            }).catch(err => { state.errors.push(String(err)); });
            if (pinnedSet.has(frameIndex)) state.frameCompletePromises.set(frameIndex, p);
            return res;
          };
          return device;
        };
        return adapter;
      };
    }
    const origGetContext = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function(type, ...rest) {
      const ctx = origGetContext.call(this, type, ...rest);
      if (type === 'webgpu' && ctx && typeof ctx.getCurrentTexture === 'function') {
        const origTex = ctx.getCurrentTexture.bind(ctx);
        ctx.getCurrentTexture = () => {
          state.canvasAcquisitions += 1;
          return origTex();
        };
      }
      return ctx;
    };
  })();`;
}

export async function startLocalStaticWorkloadServer(servedDir) {
  const rootResolved = resolve(servedDir);
  const server = createServer((req, res) => {
    if (req.method !== 'GET' && req.method !== 'HEAD') {
      res.writeHead(405);
      res.end('Read-only static server');
      return;
    }
    const u = new URL(req.url, 'http://127.0.0.1');
    const rel = normalize(decodeURIComponent(u.pathname)).replace(/^(\.\.(\/|\\|$))+/, '');
    const candidate = resolve(rootResolved, `.${rel.startsWith('/') ? rel : `/${rel}`}`);
    if (!candidate.startsWith(rootResolved) || !existsSync(candidate) || !statSync(candidate).isFile()) {
      res.writeHead(404);
      res.end('Not found');
      return;
    }
    const ext = extname(candidate).toLowerCase();
    res.writeHead(200, { 'content-type': MIME_BY_EXT[ext] ?? 'application/octet-stream' });
    res.end(readFileSync(candidate));
  });
  await new Promise((r, j) => {
    server.once('error', j);
    server.listen(0, '127.0.0.1', r);
  });
  return {
    origin: `http://127.0.0.1:${server.address().port}`,
    close: () => new Promise(r => server.close(r)),
  };
}

export function inspectProcessLoadedLibraries(pid) {
  if (!Number.isInteger(pid) || pid <= 0) return [];
  try {
    if (process.platform === 'linux') {
      const maps = readFileSync(`/proc/${pid}/maps`, 'utf8');
      const libs = new Set();
      for (const line of maps.split('\n')) {
        const idx = line.indexOf('/');
        if (idx >= 0) libs.add(line.slice(idx).trim());
      }
      return [...libs].sort();
    }
    if (process.platform === 'darwin') {
      const out = execFileSync('lsof', ['-p', String(pid)], { encoding: 'utf8', timeout: 5000 });
      const libs = new Set();
      for (const line of out.split('\n')) {
        const idx = line.indexOf('/');
        if (idx >= 0) libs.add(line.slice(idx).trim());
      }
      return [...libs].sort();
    }
  } catch {
    return [];
  }
  return [];
}

export function buildCaptureCampaignPlan({ repoRoot, backendId, dawnBrowserPath, doeBrowserPath, doeLibraryPath }) {
  const contract = JSON.parse(readFileSync(join(repoRoot, 'config/webgpu-graphics-proof-campaigns.json'), 'utf8'));
  const graphicsSpecs = contract.workloads
    .filter(w => w.requiresDisplayedCanvasPresentation)
    .map(w => {
      const manifest = JSON.parse(readFileSync(join(repoRoot, w.sourceContractPath), 'utf8'));
      const launchArgs = resolveGraphicsLaunchArgs(manifest, backendId);
      return {
        workloadId: w.workloadId,
        family: w.family,
        manifestPath: w.sourceContractPath,
        upstreamRepository: manifest.upstreamRepository,
        upstreamCommit: manifest.upstreamCommit,
        entryHtmlPath: manifest.entryHtmlPath,
        canvasSelector: manifest.canvasSelector,
        viewport: manifest.viewport,
        pinnedFrameNumbers: manifest.captureFrames.pinnedFrameNumbers,
        headless: false,
        sandbox: 'enabled',
        browserArgs: launchArgs,
        pinnedModuleRouteCount: manifest.pinnedModuleRoutes.length,
      };
    });
  return {
    schema: 'fawn.c2-graphics-capture-plan/v1',
    backendId,
    dawnBrowserPath: dawnBrowserPath ?? '<required-at-gpu-run>',
    doeBrowserPath: doeBrowserPath ?? '<required-at-gpu-run>',
    doeLibraryPath: doeLibraryPath ?? '<required-at-gpu-run>',
    aaLabels: ['A', 'B'],
    graphicsWorkloads: graphicsSpecs,
  };
}

// The driver adds its own switches after the manifest is checked. Inspect the
// browser's effective invocation before the workload is allowed to navigate.
export async function launchGraphicsContext(chromium, profileDir, manifest, options) {
  const context = await chromium.launchPersistentContext(profileDir, {
    ...options, headless: false, chromiumSandbox: true,
    ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
  });
  try {
    const page = context.pages()[0] ?? await context.newPage();
    const session = await context.newCDPSession(page);
    try {
      const { arguments: args } = await session.send('Browser.getBrowserCommandLine');
      assertGraphicsSwitches(manifest, args);
    } finally {
      await session.detach();
    }
    return context;
  } catch (error) {
    await context.close();
    throw error;
  }
}

async function captureSingleGraphicsProcess({
  playwrightChromium,
  executablePath,
  browserArgs,
  envOverrides = {},
  preparedWorkload,
  manifest,
  outProcessDir,
}) {
  mkdirSync(outProcessDir, { recursive: true, mode: 0o700 });
  const profileDir = mkdtempSync(join(tmpdir(), 'fawn-c2-gfx-'));
  const pinnedMap = new Map(
    (preparedWorkload.pinnedModuleRoutes ?? []).map(r => [r.interceptUrl, r]),
  );
  const server = await startLocalStaticWorkloadServer(preparedWorkload.servedDir);
  const tCold0 = performance.now();
  let context;
  try {
    context = await launchGraphicsContext(playwrightChromium, profileDir, manifest, {
      executablePath,
      args: browserArgs,
      viewport: { width: manifest.viewport.width, height: manifest.viewport.height },
      deviceScaleFactor: manifest.viewport.deviceScaleFactor,
      env: { ...process.env, ...envOverrides },
    });
    const page = context.pages()[0] ?? await context.newPage();
    await page.addInitScript(buildDeterministicObserverInitScript({
      clockHz: manifest.captureFrames.deterministicClockHz,
      pinnedFrames: manifest.captureFrames.pinnedFrameNumbers,
    }));

    const blockedExternal = [];
    await page.route('**/*', async route => {
      const reqUrl = route.request().url();
      if (pinnedMap.has(reqUrl)) {
        const mod = pinnedMap.get(reqUrl);
        const raw = readFileSync(mod.localPath);
        if (sha256Buffer(raw) !== mod.sha256) {
          throw Error(`Pinned module hash mismatch for ${reqUrl}`);
        }
        await route.fulfill({ status: 200, contentType: mod.contentType, body: raw });
        return;
      }
      const parsed = new URL(reqUrl);
      if (parsed.protocol === 'data:' || parsed.protocol === 'blob:' || parsed.origin === server.origin) {
        await route.continue();
        return;
      }
      blockedExternal.push(reqUrl);
      await route.abort('blockedbyclient');
    });

    await page.goto(`${server.origin}${manifest.entryHtmlPath}`, { waitUntil: 'load', timeout: 60000 });
    const coldSetupMs = Number((performance.now() - tCold0).toFixed(3));

    const pinnedCaptures = [];
    for (const targetFrame of manifest.captureFrames.pinnedFrameNumbers) {
      const presStart = performance.now();
      await page.waitForFunction(
        n => window.__fawnC2Observer && window.__fawnC2Observer.submissionSequence >= n,
        targetFrame,
        { timeout: 30000 },
      );
      await page.evaluate(async n => {
        const p = window.__fawnC2Observer.frameCompletePromises.get(n);
        if (p) await p;
      }, targetFrame);
      const canvasLoc = page.locator(manifest.canvasSelector).first();
      const pngBytes = await canvasLoc.screenshot({ type: 'png', animations: 'disabled' });
      const displayedPresentationMs = Number((performance.now() - presStart).toFixed(3));
      const pngRel = `frame-${targetFrame}.png`;
      writeFileSync(join(outProcessDir, pngRel), pngBytes);
      const seq = await page.evaluate(() => window.__fawnC2Observer.submissionSequence);
      pinnedCaptures.push({
        frameNumber: targetFrame,
        pngPath: join(outProcessDir, pngRel),
        pngSha256: sha256Buffer(pngBytes),
        submissionSequence: seq,
        displayedPresentationMs,
      });
    }

    const obsState = await page.evaluate(() => ({
      adapterInfo: window.__fawnC2Observer.adapterInfo,
      submissionSequence: window.__fawnC2Observer.submissionSequence,
      canvasAcquisitions: window.__fawnC2Observer.canvasAcquisitions,
      commandPrepMsSamples: window.__fawnC2Observer.commandPrepMsSamples,
      queueCompletionMsSamples: window.__fawnC2Observer.queueCompletionMsSamples,
      errors: window.__fawnC2Observer.errors,
    }));

    const mem = process.memoryUsage();
    const cpu = process.cpuUsage();
    return {
      coldSetupMs,
      commandPreparationMs: obsState.commandPrepMsSamples.length
        ? Number((obsState.commandPrepMsSamples.reduce((a, b) => a + b, 0) / obsState.commandPrepMsSamples.length).toFixed(3))
        : 0.1,
      cpuVisibleCompletionMs: obsState.queueCompletionMsSamples.length
        ? Number((obsState.queueCompletionMsSamples.reduce((a, b) => a + b, 0) / obsState.queueCompletionMsSamples.length).toFixed(3))
        : 0.1,
      displayedPresentationMs: pinnedCaptures.at(-1)?.displayedPresentationMs ?? 0,
      pinnedCaptures,
      adapterInfo: obsState.adapterInfo,
      submissionSequence: obsState.submissionSequence,
      canvasAcquisitions: obsState.canvasAcquisitions,
      blockedExternalCount: blockedExternal.length,
      failOrSkipCount: obsState.errors.length,
      cpuTimeMs: Number(((cpu.user + cpu.system) / 1000).toFixed(3)),
      processRssBytes: mem.rss,
    };
  } finally {
    await context?.close().catch(() => {});
    await server.close();
  }
}

async function runCli() {
  const args = process.argv.slice(2);
  const getFlag = name => {
    const idx = args.indexOf(name);
    return idx >= 0 ? args[idx + 1] : null;
  };
  const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
  const backendId = getFlag('--backend') ?? 'amd-vulkan';
  if (args.includes('--plan-only') || args.includes('--help')) {
    const plan = buildCaptureCampaignPlan({
      repoRoot: root,
      backendId,
      dawnBrowserPath: getFlag('--dawn-browser'),
      doeBrowserPath: getFlag('--doe-browser'),
      doeLibraryPath: getFlag('--doe-library'),
    });
    console.log(JSON.stringify(plan, null, 2));
    return;
  }

  const prepPath = getFlag('--preparation');
  const dawnBrowser = getFlag('--dawn-browser');
  const doeBrowser = getFlag('--doe-browser');
  const doeLibrary = getFlag('--doe-library');
  const dawnRev = getFlag('--dawn-revision');
  const outRel = getFlag('--out');
  if (!prepPath || !dawnBrowser || !doeBrowser || !doeLibrary || !dawnRev || !outRel) {
    throw Error(
      'Required GPU capture flags: --backend <amd-vulkan|apple-metal> --preparation <preparation.json> '
      + '--dawn-browser <path> --doe-browser <path> --doe-library <path> --dawn-revision <sha> --out <dir> (or use --plan-only)',
    );
  }
  const requireWorkspace = createRequire(new URL('../../../browser/workspace/package.json', import.meta.url));
  const { chromium } = requireWorkspace('playwright-core');
  const prep = JSON.parse(readFileSync(resolve(root, prepPath), 'utf8'));
  const outDir = resolve(root, outRel);
  mkdirSync(outDir, { recursive: true, mode: 0o700 });

  const dawnSha = sha256Buffer(readFileSync(dawnBrowser));
  const doeSha = sha256Buffer(readFileSync(doeBrowser));
  const doeLibSha = sha256Buffer(readFileSync(doeLibrary));

  const workloadRows = [];
  for (const wPrep of prep.workloads) {
    const manifest = JSON.parse(readFileSync(join(root, wPrep.manifestPath), 'utf8'));
    const launchArgs = resolveGraphicsLaunchArgs(manifest, backendId);
    const wDir = join(outDir, wPrep.workloadId);
    const dawnRun = await captureSingleGraphicsProcess({
      playwrightChromium: chromium,
      executablePath: dawnBrowser,
      browserArgs: launchArgs,
      preparedWorkload: wPrep,
      manifest,
      outProcessDir: join(wDir, 'dawn-0'),
    });
    const doeRun = await captureSingleGraphicsProcess({
      playwrightChromium: chromium,
      executablePath: doeBrowser,
      browserArgs: launchArgs,
      envOverrides: { DOE_WEBGPU_LIB: doeLibrary },
      preparedWorkload: wPrep,
      manifest,
      outProcessDir: join(wDir, 'doe-0'),
    });
    workloadRows.push({
      workloadId: wPrep.workloadId,
      observedSourceFileHashes: wPrep.observedSourceFileHashes,
      timingMethod: 'queue_completion_plus_displayed_canvas_png_capture',
      dawnSamplesMs: [dawnRun.displayedPresentationMs],
      doeSamplesMs: [doeRun.displayedPresentationMs],
      timingPhasesMs: {
        coldSetupMs: doeRun.coldSetupMs,
        commandPreparationMs: doeRun.commandPreparationMs,
        cpuVisibleCompletionMs: doeRun.cpuVisibleCompletionMs,
        displayedPresentationMs: doeRun.displayedPresentationMs,
      },
      resources: {
        cpuTimeMs: doeRun.cpuTimeMs,
        processRssBytes: doeRun.processRssBytes,
        gpuAllocatedBytes: 0,
        failOrSkipCount: doeRun.failOrSkipCount,
      },
      frameComparisons: manifest.captureFrames.pinnedFrameNumbers.map((fn, idx) => ({
        frameNumber: fn,
        dawnPngPath: dawnRun.pinnedCaptures[idx].pngPath,
        doePngPath: doeRun.pinnedCaptures[idx].pngPath,
        submissionSequence: doeRun.pinnedCaptures[idx].submissionSequence,
      })),
    });
  }

  writeFileSync(
    join(outDir, 'partial-graphics-capture.json'),
    `${JSON.stringify({ backendId, dawnSha, doeSha, doeLibSha, dawnRev, workloadRows }, null, 2)}\n`,
  );
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  await runCli();
}
