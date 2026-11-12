#!/usr/bin/env node
// onSubmittedWorkDone and mapAsync wait for submitted work however long it
// runs: WebGPU defines no deadline, and a valid multi-second queue (a long LLM
// prefill) is not a failure. Without DOE_TIMEOUT_MS or setNativeTimeoutMs the
// addon must not reject after its 2 s request deadline. The queue here holds
// several submissions whose total GPU time is calibrated to exceed 2.5 s;
// each command buffer stays short. The wait must also yield to the event loop
// and leave work submitted meanwhile pending. Results are checked on the CPU.

import { requestNativeDeviceOrSkip } from './native-device-test-helper.js';

if (process.env.DOE_TIMEOUT_MS) {
  console.log('skip: DOE_TIMEOUT_MS sets an explicit deadline');
  process.exit(0);
}

const device = await requestNativeDeviceOrSkip('long submitted work');
const THREADS = 64 * 256;
const SUBMITS = 8;
const TARGET_MS = 3200;

const module = device.createShaderModule({
  code: `
    @group(0) @binding(0) var<storage, read_write> state: array<u32>;
    @group(0) @binding(1) var<uniform> iterations: u32;
    @compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
      var x = state[id.x];
      for (var i = 0u; i < iterations; i++) {
        x ^= x << 13u; x ^= x >> 17u; x ^= x << 5u;
      }
      state[id.x] = x;
    }
  `,
});
const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' } });
const state = device.createBuffer({
  size: THREADS * 4,
  usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC,
});
const iterationsBuffer = device.createBuffer({ size: 4, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
const bindGroup = device.createBindGroup({
  layout: pipeline.getBindGroupLayout(0),
  entries: [{ binding: 0, resource: { buffer: state } }, { binding: 1, resource: { buffer: iterationsBuffer } }],
});
const seeds = Uint32Array.from({ length: THREADS }, (_, i) => i + 1);

function submit(count) {
  for (let i = 0; i < count; i += 1) {
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginComputePass();
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, bindGroup);
    pass.dispatchWorkgroups(THREADS / 64);
    pass.end();
    device.queue.submit([encoder.finish()]);
  }
}

async function run(iterations, count) {
  device.queue.writeBuffer(state, 0, seeds);
  device.queue.writeBuffer(iterationsBuffer, 0, new Uint32Array([iterations]));
  await device.queue.onSubmittedWorkDone();
  const started = performance.now();
  submit(count);
  await device.queue.onSubmittedWorkDone();
  return performance.now() - started;
}

function xorshift(seed, steps) {
  let x = seed >>> 0;
  for (let i = 0; i < steps; i += 1) {
    x = (x ^ (x << 13)) >>> 0;
    x = (x ^ (x >>> 17)) >>> 0;
    x = (x ^ (x << 5)) >>> 0;
  }
  return x;
}

// Calibrate one submission, then size the queue to exceed the old deadline.
let iterations = 1 << 16;
let elapsed = await run(iterations, 1);
while (elapsed < 50 && iterations < (1 << 30)) {
  iterations *= 4;
  elapsed = await run(iterations, 1);
}
iterations = Math.min(2 ** 31, Math.ceil(iterations * (TARGET_MS / SUBMITS) / Math.max(elapsed, 1)));
const waited = await run(iterations, SUBMITS);
if (waited < 2100) {
  console.log(`skip: calibrated queue ran ${waited.toFixed(0)} ms, under the 2 s deadline being tested`);
  process.exit(0);
}

// The wait yields to the event loop, and work submitted while it waits stays
// pending: the next onSubmittedWorkDone still waits for it.
device.queue.writeBuffer(iterationsBuffer, 0, new Uint32Array([iterations]));
submit(SUBMITS / 2);
let turns = 0;
let lateSubmitted = false;
const ticker = setInterval(() => {
  turns += 1;
  if (!lateSubmitted) { lateSubmitted = true; submit(SUBMITS / 2); }
}, 1);
await device.queue.onSubmittedWorkDone();
clearInterval(ticker);
if (!lateSubmitted || turns === 0) {
  throw new Error(`onSubmittedWorkDone blocked the event loop (${turns} timer turns during a ${(TARGET_MS / 2).toFixed(0)} ms wait)`);
}
const lateStarted = performance.now();
await device.queue.onSubmittedWorkDone();
const lateWaited = performance.now() - lateStarted;
if (lateWaited < TARGET_MS / 8) {
  throw new Error(`work submitted during a wait was marked done early (second wait ${lateWaited.toFixed(0)} ms)`);
}
const totalSteps = iterations * SUBMITS * 2;

const readback = device.createBuffer({ size: THREADS * 4, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
const encoder = device.createCommandEncoder();
encoder.copyBufferToBuffer(state, 0, readback, 0, THREADS * 4);
device.queue.submit([encoder.finish()]);
await readback.mapAsync(GPUMapMode.READ);
const actual = new Uint32Array(readback.getMappedRange().slice(0));
readback.unmap();
for (const index of [0, 777, THREADS - 1]) {
  const expected = xorshift(seeds[index], totalSteps);
  if (actual[index] !== expected) {
    throw new Error(`thread ${index}: ${actual[index]} != ${expected}`);
  }
}
for (const buffer of [state, iterationsBuffer, readback]) buffer.destroy();
device.destroy();
console.log(`ok: ${SUBMITS} submissions of ${iterations} iterations waited ${waited.toFixed(0)} ms; `
  + `${turns} timer turns during a later wait, work submitted meanwhile waited ${lateWaited.toFixed(0)} ms; exact results`);
