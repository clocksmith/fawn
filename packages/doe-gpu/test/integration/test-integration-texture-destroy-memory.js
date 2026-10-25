#!/usr/bin/env node
// GPUTexture.destroy() frees the texture's memory at once, even while its views
// stay reachable (https://gpuweb.github.io/gpuweb/#dom-gputexture-destroy), and
// objects that become unreachable release their native references. 10k
// render-target textures, each cleared through a view that is then held, keep
// process memory and per-iteration time bounded; a held view of a destroyed
// texture fails validation when used.
//
// Process memory is RSS plus, on amdgpu, the process's resident VRAM and GTT
// (/proc/self/fdinfo), so device memory counts on CPU and physical adapters.

import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import { setFlagsFromString } from 'node:v8';
import { runInNewContext } from 'node:vm';
import { setupGlobals } from '../../src/index.js';

setFlagsFromString('--expose-gc');
const collectGarbage = runInNewContext('gc');

const target = {};
setupGlobals(target);
// A provider may offer its only adapter (lavapipe for the C runtime) as the fallback.
const adapter = await target.navigator.gpu.requestAdapter()
  ?? await target.navigator.gpu.requestAdapter({ forceFallbackAdapter: true });
if (!adapter) {
  console.log('texture destroy memory: skipped (GPU adapter unavailable)');
  process.exit(0);
}
const device = await adapter.requestDevice();
const { GPUTextureUsage } = target;
console.log(`texture destroy memory: adapter ${adapter.info?.description || adapter.info?.device || 'unknown'}`);

const TEXTURES = 10_000;
const WARMUP = 200;
const WINDOW = 1_000;
const SIZE = 64; // 16 KiB of rgba8unorm texels per texture
// A held view and its wrapper stay; the texture's memory does not.
const MAX_KIB_PER_TEXTURE = 6;
const MAX_SLOWDOWN = 3;

function processMemoryKiB() {
  let kib = Number(/VmRSS:\s+(\d+)/.exec(readFileSync('/proc/self/status', 'utf8'))?.[1] ?? 0);
  for (const fd of readdirSync('/proc/self/fd')) {
    let info;
    try {
      info = readFileSync(`/proc/self/fdinfo/${fd}`, 'utf8');
    } catch {
      continue;
    }
    if (!/drm-driver:\s*amdgpu/.test(info)) continue;
    kib += Number(/drm-resident-vram:\s*(\d+)/.exec(info)?.[1] ?? 0);
    kib += Number(/drm-resident-gtt:\s*(\d+)/.exec(info)?.[1] ?? 0);
  }
  return kib;
}

async function settle() {
  await device.queue.onSubmittedWorkDone();
  collectGarbage();
  await new Promise((resolve) => setTimeout(resolve, 0));
  collectGarbage();
}

const held = [];
function renderTargetOnce() {
  const texture = device.createTexture({
    size: [SIZE, SIZE],
    format: 'rgba8unorm',
    usage: GPUTextureUsage.RENDER_ATTACHMENT,
  });
  const view = texture.createView();
  const encoder = device.createCommandEncoder();
  encoder.beginRenderPass({ colorAttachments: [{ view, loadOp: 'clear', clearValue: [0, 1, 0, 1], storeOp: 'store' }] }).end();
  device.queue.submit([encoder.finish()]);
  held.push(view);
  texture.destroy();
}

for (let i = 0; i < WARMUP; i++) renderTargetOnce();
await settle();
const before = processMemoryKiB();
const times = [];
for (let i = 0; i < TEXTURES; i++) {
  const start = performance.now();
  renderTargetOnce();
  times.push(performance.now() - start);
  if (i % 500 === 499) await settle();
}
await settle();
const growthKiB = processMemoryKiB() - before;
const mean = (values) => values.reduce((sum, value) => sum + value, 0) / values.length;
const first = mean(times.slice(0, WINDOW));
const last = mean(times.slice(-WINDOW));
console.log(`texture destroy memory: ${(growthKiB / TEXTURES).toFixed(2)} KiB per destroyed texture with a held view; ` +
  `first ${first.toFixed(3)} ms, last ${last.toFixed(3)} ms per iteration`);
assert.ok(growthKiB / TEXTURES <= MAX_KIB_PER_TEXTURE,
  `process memory grew ${(growthKiB / TEXTURES).toFixed(2)} KiB per destroyed texture (limit ${MAX_KIB_PER_TEXTURE})`);
console.log('ok: destroy() frees texture memory while 10k views stay reachable');
assert.ok(last <= first * MAX_SLOWDOWN + 0.25,
  `iteration time grew from ${first.toFixed(3)} ms to ${last.toFixed(3)} ms`);
console.log('ok: per-iteration time stays bounded across 10k destroyed textures');

// Memory destroy() frees may back the next texture, which still reads as
// zero: a fresh texture loaded in a pass after a white one was destroyed.
const { GPUBufferUsage, GPUMapMode } = target;
const reuseShape = { size: [16, 16], format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC };
for (let round = 0; round < 4; round++) {
  const white = device.createTexture(reuseShape);
  const clear = device.createCommandEncoder();
  clear.beginRenderPass({ colorAttachments: [{ view: white.createView(), loadOp: 'clear', clearValue: [1, 1, 1, 1], storeOp: 'store' }] }).end();
  device.queue.submit([clear.finish()]);
  await device.queue.onSubmittedWorkDone();
  white.destroy();
  const fresh = device.createTexture(reuseShape);
  const readback = device.createBuffer({ size: 256 * 16, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const load = device.createCommandEncoder();
  load.beginRenderPass({ colorAttachments: [{ view: fresh.createView(), loadOp: 'load', storeOp: 'store' }] }).end();
  load.copyTextureToBuffer({ texture: fresh }, { buffer: readback, bytesPerRow: 256 }, [16, 16]);
  device.queue.submit([load.finish()]);
  await readback.mapAsync(GPUMapMode.READ);
  const texels = new Uint8Array(readback.getMappedRange());
  for (let row = 0; row < 16; row++) {
    assert.ok(texels.subarray(row * 256, row * 256 + 64).every((byte) => byte === 0), `round ${round}: a new texture reads as zero`);
  }
  readback.unmap();
  fresh.destroy();
}
console.log('ok: a texture in memory a destroyed texture freed reads as zero');

// A held view of a destroyed texture fails validation at submit.
device.pushErrorScope('validation');
const encoder = device.createCommandEncoder();
encoder.beginRenderPass({ colorAttachments: [{ view: held[0], loadOp: 'clear', storeOp: 'store' }] }).end();
device.queue.submit([encoder.finish()]);
const error = await device.popErrorScope();
assert.ok(error, 'a render pass on a destroyed texture\'s view fails validation');
console.log('ok: a held view of a destroyed texture fails validation when used');
device.destroy();
