#!/usr/bin/env node
// Release scoreboard row 4: draw-call and encoding overhead. Each frame
// clears a 512x512 offscreen target and issues N pixel-aligned 4x4 quad draws,
// either with unchanged state or with the pipeline and bind group changing per
// draw. Frame time covers encoder creation through queue completion. The
// final frame is read back outside timing and must equal a CPU rasterization
// of the same rectangles and a recorded digest.
import {
  ApiObserver,
  OutputMismatchError,
  installGpuGlobals,
  loadProvider,
  loadScenario,
  percentile,
  runExecutor,
  sha256,
} from './common.js';

const USAGE = 'node bench/executors/release-scoreboard/draw-overhead.js '
  + '--provider doe|dawn --backend metal|vulkan|d3d12 --scenario <path> '
  + '--trace-meta <path> --trace-jsonl <path> --workload <id>';
const SCENARIO_KIND = 'release-scoreboard-draw-overhead';
const WIDTH = 512;
const HEIGHT = 512;
const CELL = 4;
const COLUMNS = WIDTH / CELL;
const MAX_DRAWS = COLUMNS * (HEIGHT / CELL);
const BIND_GROUP_COUNT = 64;
const FORMAT = 'rgba8unorm';

const SHADER = /* wgsl */ `
struct Params { seed: u32, pad0: u32, pad1: u32, pad2: u32 };
@group(0) @binding(0) var<uniform> params: Params;
struct VOut {
  @builtin(position) position: vec4f,
  @location(0) @interpolate(flat) instance: u32,
};
@vertex fn vs(@builtin(vertex_index) v: u32, @builtin(instance_index) i: u32) -> VOut {
  var corners = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0),
                                vec2f(0.0, 1.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0));
  let c = corners[v];
  let px = (f32(i % ${COLUMNS}u) + c.x) * ${CELL}.0;
  let py = (f32(i / ${COLUMNS}u) + c.y) * ${CELL}.0;
  var out: VOut;
  out.position = vec4f(px / ${WIDTH}.0 * 2.0 - 1.0, 1.0 - py / ${HEIGHT}.0 * 2.0, 0.0, 1.0);
  out.instance = i;
  return out;
}
fn channel(x: u32) -> f32 { return f32(x % 256u) / 255.0; }
@fragment fn fs_a(in: VOut) -> @location(0) vec4f {
  let i = in.instance;
  let s = params.seed;
  return vec4f(channel(i * 37u + s * 11u + 1u), channel(i * 91u + s * 5u + 2u),
               channel(i * 17u + s * 3u + 3u), 1.0);
}
@fragment fn fs_b(in: VOut) -> @location(0) vec4f {
  let i = in.instance;
  let s = params.seed;
  return vec4f(channel(i * 13u + s * 7u + 101u), channel(i * 29u + s * 19u + 59u),
               channel(i * 53u + s * 23u + 7u), 1.0);
}
`;

// Independent CPU reference of the same rectangles and colors.
function cpuReference(drawCount, changing) {
  const pixels = new Uint8Array(WIDTH * HEIGHT * 4);
  for (let p = 3; p < pixels.length; p += 4) pixels[p] = 0;
  for (let i = 0; i < drawCount; i += 1) {
    const variantB = changing && i % 2 === 1;
    const seed = changing ? i % BIND_GROUP_COUNT : 0;
    const rgb = variantB
      ? [i * 13 + seed * 7 + 101, i * 29 + seed * 19 + 59, i * 53 + seed * 23 + 7]
      : [i * 37 + seed * 11 + 1, i * 91 + seed * 5 + 2, i * 17 + seed * 3 + 3];
    const x0 = (i % COLUMNS) * CELL;
    const y0 = Math.floor(i / COLUMNS) * CELL;
    for (let y = y0; y < y0 + CELL; y += 1) {
      for (let x = x0; x < x0 + CELL; x += 1) {
        const offset = (y * WIDTH + x) * 4;
        pixels[offset] = rgb[0] % 256;
        pixels[offset + 1] = rgb[1] % 256;
        pixels[offset + 2] = rgb[2] % 256;
        pixels[offset + 3] = 255;
      }
    }
  }
  return pixels;
}

async function readTexture(device, texture) {
  const bytesPerRow = WIDTH * 4;
  const buffer = device.createBuffer({
    size: bytesPerRow * HEIGHT, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
  });
  const encoder = device.createCommandEncoder();
  encoder.copyTextureToBuffer({ texture }, { buffer, bytesPerRow, rowsPerImage: HEIGHT },
    { width: WIDTH, height: HEIGHT, depthOrArrayLayers: 1 });
  device.queue.submit([encoder.finish()]);
  await buffer.mapAsync(GPUMapMode.READ);
  const bytes = new Uint8Array(buffer.getMappedRange()).slice();
  buffer.unmap();
  buffer.destroy();
  return bytes;
}

function firstDifference(actual, expected) {
  for (let index = 0; index < expected.length; index += 1) {
    if (actual[index] !== expected[index]) {
      const pixel = Math.floor(index / 4);
      return { pixel, x: pixel % WIDTH, y: Math.floor(pixel / WIDTH), actual: actual[index], expected: expected[index] };
    }
  }
  return null;
}

async function run(args, onTeardown) {
  const { workload, scenario, scenarioSha256 } = loadScenario(args.scenarioPath, SCENARIO_KIND, args.workloadId);
  const drawCount = workload.drawsPerFrame;
  const changing = workload.stateMode === 'pipeline-and-bind-group-per-draw';
  if (!changing && workload.stateMode !== 'unchanged') {
    throw new Error(`unsupported stateMode ${workload.stateMode}`);
  }
  if (!Number.isInteger(drawCount) || drawCount < 1 || drawCount > MAX_DRAWS) {
    throw new Error(`drawsPerFrame must be 1..${MAX_DRAWS}`);
  }
  const frames = scenario.frames;

  const observer = new ApiObserver({ countPassCommands: false });
  observer.enter('setup');
  const provider = await loadProvider(args.provider, args.backend);
  const gpu = observer.wrapGpu(provider.module.create(provider.contract.createOptions), provider.contract);
  installGpuGlobals(provider.module, gpu, provider.contract.createOptions);
  const adapter = await gpu.requestAdapter({ powerPreference: 'high-performance' });
  const device = await adapter.requestDevice();
  onTeardown(() => device.destroy());

  const module = device.createShaderModule({ code: SHADER });
  const bindGroupLayout = device.createBindGroupLayout({
    entries: [{ binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } }],
  });
  const layout = device.createPipelineLayout({ bindGroupLayouts: [bindGroupLayout] });
  const pipeline = (entryPoint) => device.createRenderPipeline({
    layout,
    vertex: { module, entryPoint: 'vs' },
    fragment: { module, entryPoint, targets: [{ format: FORMAT }] },
    primitive: { topology: 'triangle-list' },
  });
  const pipelines = [pipeline('fs_a'), pipeline('fs_b')];
  const bindGroups = [];
  for (let seed = 0; seed < BIND_GROUP_COUNT; seed += 1) {
    const buffer = device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    device.queue.writeBuffer(buffer, 0, new Uint32Array([seed, 0, 0, 0]));
    bindGroups.push(device.createBindGroup({ layout: bindGroupLayout, entries: [{ binding: 0, resource: { buffer } }] }));
  }
  const target = device.createTexture({
    size: { width: WIDTH, height: HEIGHT },
    format: FORMAT,
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
  });
  const view = target.createView();
  await device.queue.onSubmittedWorkDone();

  // Slot i runs from frame i's start to frame i+1's start (or window end).
  const frameSlotsMs = (frameSamples, windowEnd) => frameSamples.map((sample, index) =>
    (frameSamples[index + 1]?.startedAt ?? windowEnd) - sample.startedAt);

  const frame = async () => {
    const started = performance.now();
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
      colorAttachments: [{ view, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
    });
    if (changing) {
      for (let i = 0; i < drawCount; i += 1) {
        pass.setPipeline(pipelines[i % 2]);
        pass.setBindGroup(0, bindGroups[i % BIND_GROUP_COUNT]);
        pass.draw(6, 1, 0, i);
      }
    } else {
      pass.setPipeline(pipelines[0]);
      pass.setBindGroup(0, bindGroups[0]);
      for (let i = 0; i < drawCount; i += 1) pass.draw(6, 1, 0, i);
    }
    pass.end();
    const commands = encoder.finish();
    const encoded = performance.now();
    device.queue.submit([commands]);
    await device.queue.onSubmittedWorkDone();
    const done = performance.now();
    return { frameMs: done - started, encodeMs: encoded - started, startedAt: started };
  };

  observer.enter('warmup');
  for (let index = 0; index < frames.warmup; index += 1) await frame();
  observer.enter('timed');
  const samples = [];
  const timedStarted = performance.now();
  for (let index = 0; index < frames.measured; index += 1) samples.push(await frame());
  const timedWindowMs = performance.now() - timedStarted;
  observer.enter('readback');
  const pixels = await readTexture(device, target);

  const frameMs = samples.map((sample) => sample.frameMs);
  const encodeMs = samples.map((sample) => sample.encodeMs);
  const digest = sha256(pixels);
  const reference = cpuReference(drawCount, changing);
  const referenceDigest = sha256(reference);
  const fields = {
    timingMs: percentile(frameMs, 0.5),
    timingSource: 'release-scoreboard-frame-p50-ms',
    // Median wall of one timed frame slot (frame start to next frame start).
    // Each slot contains its frame, so this bounds the selected p50 from above.
    processWallMs: percentile(frameSlotsMs(samples, timedStarted + timedWindowMs), 0.5),
    timedWindowMs,
    processWallScope: 'workload-unit',
    // Counts come from the workload: the encode path is not wrapped here.
    executionDispatchCount: drawCount * frames.measured,
    executionSubmitCount: observer.phases.timed?.submitCalls ?? 0,
    executionShapePhases: ['timed'],
    executionDispatchKinds: { dispatch: 0, draw: drawCount * frames.measured },
    scenarioPath: args.scenarioPath,
    scenarioSha256,
    adapterInfo: observer.adapterInfo,
    backendSelection: observer.backendSelection,
    packageProviderIdentity: provider.identity,
    ...observer.shaderReceipt(),
    metrics: {
      drawsPerFrame: drawCount,
      stateMode: workload.stateMode,
      warmupFrames: frames.warmup,
      measuredFrames: frames.measured,
      frameP50Ms: percentile(frameMs, 0.5),
      frameP95Ms: percentile(frameMs, 0.95),
      encodeP50Ms: percentile(encodeMs, 0.5),
      encodeP95Ms: percentile(encodeMs, 0.95),
      frameBoundary: 'createCommandEncoder through onSubmittedWorkDone of the frame submit',
      apiPhases: observer.phases,
    },
    resultSummary: {
      outputKind: 'rgba8unorm-final-frame',
      outputSha256: digest,
      outputByteLength: pixels.byteLength,
      cpuReferenceSha256: referenceDigest,
      recordedReferenceSha256: workload.referenceSha256 ?? null,
      referenceClass: 'independent-cpu-rasterization',
    },
  };
  const mismatch = digest !== referenceDigest
    ? { against: 'cpu-reference', firstDifference: firstDifference(pixels, reference) }
    : workload.referenceSha256 !== referenceDigest
      ? { against: 'recorded-reference', recorded: workload.referenceSha256 ?? null }
      : null;
  if (mismatch) {
    const error = new OutputMismatchError(`final frame does not match the ${mismatch.against}`, mismatch);
    error.fields = fields;
    throw error;
  }
  fields.resultSummary.referenceMatched = true;
  return fields;
}

await runExecutor(USAGE, 'draw', run);
