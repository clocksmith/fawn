#!/usr/bin/env node
// Release scoreboard row 5: one upstream three.js WebGPURenderer scene,
// rendered headless in Node into an offscreen render target. three.js comes
// from the pinned npm package in bench/package.json; the only intended
// difference between runs is the WebGPU provider. Frame time covers
// renderer.render() through queue completion; the final frame is read back
// outside timing and its digest must equal the recorded reference.
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { gunzipSync } from 'node:zlib';

import {
  ApiObserver,
  OutputMismatchError,
  REPO_ROOT,
  installGpuGlobals,
  loadProvider,
  loadScenario,
  percentile,
  repoPath,
  runExecutor,
  sha256,
  sha256File,
} from './common.js';

const USAGE = 'node bench/executors/release-scoreboard/three-scene.js '
  + '--provider doe|dawn --backend metal|vulkan|d3d12 --scenario <path> '
  + '--trace-meta <path> --trace-jsonl <path> --workload <id>';
const SCENARIO_KIND = 'release-scoreboard-three-scene';

// WebGPURenderer asks its canvas for a context lazily and only presents to it
// when rendering to the canvas. This shim satisfies that interface with an
// ordinary texture on the same device; the scene renders to a RenderTarget,
// so presentation never happens and both providers see the same calls.
class OffscreenCanvas {
  constructor(width, height) {
    this.width = width;
    this.height = height;
    this.style = {};
    this.clientWidth = width;
    this.clientHeight = height;
  }

  getContext(kind) {
    if (kind !== 'webgpu') throw new Error(`offscreen canvas supports only webgpu, got ${kind}`);
    const canvas = this;
    let configuration = null;
    let texture = null;
    return {
      canvas,
      configure(value) { configuration = value; texture = null; },
      unconfigure() { configuration = null; texture = null; },
      getConfiguration() { return configuration; },
      getCurrentTexture() {
        if (!configuration) throw new Error('offscreen context is not configured');
        texture ??= configuration.device.createTexture({
          size: { width: canvas.width, height: canvas.height },
          format: configuration.format,
          usage: configuration.usage ?? GPUTextureUsage.RENDER_ATTACHMENT,
        });
        return texture;
      },
    };
  }

  addEventListener() {}
  removeEventListener() {}
  setAttribute() {}
  getBoundingClientRect() { return { left: 0, top: 0, width: this.width, height: this.height }; }
}

function threeIdentity(require) {
  const packageJson = join(REPO_ROOT, 'bench/node_modules/three/package.json');
  return {
    packageName: 'three',
    version: JSON.parse(readFileSync(packageJson, 'utf8')).version,
    packageJsonSha256: sha256File(packageJson),
    webgpuBuildSha256: sha256File(require.resolve('three/webgpu')),
  };
}

function buildScene(THREE, spec) {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x20232a);
  const camera = new THREE.PerspectiveCamera(50, spec.width / spec.height, 0.1, 100);
  camera.position.set(0, 3.2, 9);
  camera.lookAt(0, 0.5, 0);
  scene.add(new THREE.AmbientLight(0xffffff, 0.35));
  const sun = new THREE.DirectionalLight(0xffffff, 2.2);
  sun.position.set(4, 8, 5);
  scene.add(sun);
  const ground = new THREE.Mesh(
    new THREE.PlaneGeometry(30, 30),
    new THREE.MeshStandardMaterial({ color: 0x556b2f, roughness: 0.9, metalness: 0.0 }),
  );
  ground.rotation.x = -Math.PI / 2;
  scene.add(ground);
  const geometries = [
    new THREE.TorusKnotGeometry(0.55, 0.18, 128, 16),
    new THREE.SphereGeometry(0.6, 48, 24),
    new THREE.BoxGeometry(0.9, 0.9, 0.9),
    new THREE.IcosahedronGeometry(0.6, 1),
  ];
  const movers = [];
  for (let index = 0; index < spec.objects; index += 1) {
    const material = new THREE.MeshStandardMaterial({
      color: new THREE.Color().setHSL((index * 0.137) % 1, 0.6, 0.5),
      roughness: 0.25 + (index % 4) * 0.2,
      metalness: index % 3 === 0 ? 0.7 : 0.1,
    });
    const mesh = new THREE.Mesh(geometries[index % geometries.length], material);
    const column = index % 8;
    const row = Math.floor(index / 8);
    mesh.position.set((column - 3.5) * 1.4, 0.7 + (row % 2) * 0.2, -row * 1.6 + 2);
    scene.add(mesh);
    movers.push(mesh);
  }
  // Deterministic animation: object transforms depend only on the frame index.
  const pose = (frame) => {
    movers.forEach((mesh, index) => {
      mesh.rotation.set(frame * 0.02 + index * 0.3, frame * 0.03 + index * 0.17, 0);
    });
  };
  return { scene, camera, pose };
}

// Shaders compiled by different WGSL translators may round a lit pixel
// differently (measured: 1 pixel, 1 LSB of 518,400 on Apple M3 / Metal), so
// exact cross-provider digests are not a valid oracle for a lit scene. Each
// provider's own readback is checked against the backend's recorded frame
// under the declared tolerance; exact digests are retained beside it.
function compareFrame(pixels, reference) {
  if (!reference) return { status: 'no-reference' };
  const path = repoPath(reference.framePath);
  if (!existsSync(path)) return { status: 'reference-file-missing', path: reference.framePath };
  const expected = gunzipSync(readFileSync(path));
  if (sha256(expected) !== reference.frameSha256) {
    return { status: 'reference-file-digest-mismatch', path: reference.framePath };
  }
  if (expected.byteLength !== pixels.byteLength) {
    return { status: 'reference-size-mismatch', expected: expected.byteLength, actual: pixels.byteLength };
  }
  let differingPixels = 0;
  let maxChannelDifference = 0;
  for (let offset = 0; offset < pixels.byteLength; offset += 4) {
    let pixelDifference = 0;
    for (let channel = 0; channel < 4; channel += 1) {
      pixelDifference = Math.max(pixelDifference, Math.abs(pixels[offset + channel] - expected[offset + channel]));
    }
    if (pixelDifference > 0) differingPixels += 1;
    maxChannelDifference = Math.max(maxChannelDifference, pixelDifference);
  }
  const differingPixelFraction = differingPixels / (pixels.byteLength / 4);
  const tolerance = reference.tolerance;
  const passed = maxChannelDifference <= tolerance.maxChannelDifference
    && differingPixelFraction <= tolerance.maxDifferingPixelFraction;
  return {
    status: passed ? 'within-tolerance' : 'outside-tolerance',
    differingPixels, differingPixelFraction, maxChannelDifference, tolerance,
    exact: differingPixels === 0,
  };
}

async function run(args, onTeardown) {
  const { scenario, workload, scenarioSha256 } = loadScenario(args.scenarioPath, SCENARIO_KIND, args.workloadId);
  const require = createRequire(`${REPO_ROOT}/bench/package.json`);
  const observer = new ApiObserver();
  observer.enter('setup');
  const provider = await loadProvider(args.provider, args.backend);
  const gpu = observer.wrapGpu(provider.module.create(provider.contract.createOptions), provider.contract);
  installGpuGlobals(provider.module, gpu, provider.contract.createOptions);
  gpu.getPreferredCanvasFormat ??= () => 'bgra8unorm';
  globalThis.self ??= globalThis;
  // Frames are driven explicitly; the renderer's own animation loop never
  // schedules work, so nothing runs between or during timed frames.
  globalThis.requestAnimationFrame ??= () => 0;
  globalThis.cancelAnimationFrame ??= () => {};
  // require.resolve returns a filesystem path; on Windows a bare `C:\...` path
  // is not an ESM specifier, so it becomes a file: URL.
  const THREE = await import(pathToFileURL(require.resolve('three/webgpu')).href);
  const three = threeIdentity(require);
  if (three.version !== scenario.three.version) {
    throw new Error(`three ${three.version} loaded, scenario pins ${scenario.three.version}`);
  }
  const spec = workload.scene;
  const canvas = new OffscreenCanvas(spec.width, spec.height);
  const renderer = new THREE.WebGPURenderer({ canvas, antialias: false, powerPreference: 'high-performance' });
  await renderer.init();
  onTeardown(() => renderer.dispose());
  renderer.setPixelRatio(1);
  renderer.setSize(spec.width, spec.height, false);
  const target = new THREE.RenderTarget(spec.width, spec.height, {
    type: THREE.UnsignedByteType, format: THREE.RGBAFormat, depthBuffer: true, samples: 0,
  });
  renderer.setRenderTarget(target);
  const { scene, camera, pose } = buildScene(THREE, spec);
  const device = observer.device;

  // Slot i runs from frame i's start to frame i+1's start (or window end).
  const frameSlotsMs = (frameSamples, windowEnd) => frameSamples.map((sample, index) =>
    (frameSamples[index + 1]?.startedAt ?? windowEnd) - sample.startedAt);

  const frame = async (index) => {
    const started = performance.now();
    pose(index);
    renderer.render(scene, camera);
    const encoded = performance.now();
    await device.queue.onSubmittedWorkDone();
    return { frameMs: performance.now() - started, renderCallMs: encoded - started, startedAt: started };
  };

  // The first frame compiles the scene's pipelines; it is attributed separately.
  observer.enter('first-frame');
  const firstFrame = await frame(0);
  observer.enter('warmup');
  for (let index = 1; index <= scenario.frames.warmup; index += 1) await frame(index);
  observer.enter('timed');
  const samples = [];
  const timedStarted = performance.now();
  const firstTimed = scenario.frames.warmup + 1;
  for (let index = firstTimed; index < firstTimed + scenario.frames.measured; index += 1) {
    samples.push(await frame(index));
  }
  const timedWindowMs = performance.now() - timedStarted;
  observer.enter('readback');
  const pixels = new Uint8Array(await renderer.readRenderTargetPixelsAsync(target, 0, 0, spec.width, spec.height));

  // Diagnostic only: retain the raw frame for mismatch investigation.
  const dumpDir = process.env.FAWN_SCOREBOARD_FRAME_DUMP_DIR?.trim();
  if (dumpDir) {
    mkdirSync(dumpDir, { recursive: true });
    writeFileSync(join(dumpDir, `${args.workloadId}-${args.provider}-${spec.width}x${spec.height}.rgba`), pixels);
  }
  // Equal blank frames would pass a digest comparison; require real content.
  const distinctColors = new Set(new Uint32Array(pixels.buffer, pixels.byteOffset, pixels.byteLength / 4)).size;
  if (distinctColors < workload.minimumDistinctColors) {
    throw new OutputMismatchError(`final frame has ${distinctColors} distinct colors, `
      + `fewer than the declared minimum ${workload.minimumDistinctColors}`, { distinctColors });
  }
  const frameMs = samples.map((sample) => sample.frameMs);
  const renderCallMs = samples.map((sample) => sample.renderCallMs);
  const digest = sha256(pixels);
  const reference = workload.references?.[args.backend] ?? null;
  const comparison = compareFrame(pixels, reference);
  const passed = comparison.status === 'within-tolerance';
  const fields = {
    timingMs: percentile(frameMs, 0.5),
    timingSource: 'release-scoreboard-frame-p50-ms',
    // Median wall of one timed frame slot (frame start to next frame start).
    // Each slot contains its frame, so this bounds the selected p50 from above.
    processWallMs: percentile(frameSlotsMs(samples, timedStarted + timedWindowMs), 0.5),
    timedWindowMs,
    processWallScope: 'workload-unit',
    ...observer.executionShape(['timed']),
    scenarioPath: args.scenarioPath,
    scenarioSha256,
    adapterInfo: observer.adapterInfo,
    backendSelection: observer.backendSelection,
    packageProviderIdentity: provider.identity,
    engine: three,
    ...observer.shaderReceipt(),
    metrics: {
      scene: spec,
      firstFrameMs: firstFrame.frameMs,
      warmupFrames: scenario.frames.warmup,
      measuredFrames: scenario.frames.measured,
      frameP50Ms: percentile(frameMs, 0.5),
      frameP95Ms: percentile(frameMs, 0.95),
      renderCallP50Ms: percentile(renderCallMs, 0.5),
      renderCallP95Ms: percentile(renderCallMs, 0.95),
      frameBoundary: 'pose + renderer.render() through onSubmittedWorkDone',
      asyncPipelineCreation: observer.asyncPipelineSummary(),
      apiPhases: observer.phases,
    },
    resultSummary: {
      outputKind: 'rgba8unorm-final-frame',
      frameSha256: digest,
      frameByteLength: pixels.byteLength,
      distinctColors,
      finalFrameIndex: firstTimed + scenario.frames.measured - 1,
      referenceClass: reference?.referenceClass ?? null,
      referenceFrameSha256: reference?.frameSha256 ?? null,
      referenceComparison: comparison,
    },
    // The comparator admits this row through same-execution declared oracles:
    // each side's own timed-process readback checked against one reference.
    outputOracleCount: 1,
    outputOracleMatchedCount: passed ? 1 : 0,
    outputOracleFailedCount: passed ? 0 : 1,
    outputOracleScope: 'same-execution-readback',
  };
  if (!passed) {
    const error = new OutputMismatchError(
      `final frame check failed (${comparison.status}) against the recorded ${args.backend} reference`, comparison);
    error.fields = fields;
    throw error;
  }
  return fields;
}

await runExecutor(USAGE, 'three', run);
