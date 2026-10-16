// Compare published limits with Metal and exercise a large storage binding.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { setupGlobals } from '../../src/index.js';

const LARGE_BUFFER_BYTES = 512 * 1024 * 1024;
const WORD_BYTES = Uint32Array.BYTES_PER_ELEMENT;
const EXPECTED = 0x12345678;

if (process.platform !== 'darwin') {
  console.log('SKIP: physical Metal buffer limits require macOS');
  process.exit(0);
}

const temporary = mkdtempSync(join(tmpdir(), 'doe-metal-limits-'));
let device;
try {
  const oracle = join(temporary, 'metal-limit');
  execFileSync('xcrun', ['clang', '-framework', 'Foundation', '-framework', 'Metal',
    fileURLToPath(new URL('./metal-buffer-limit.m', import.meta.url)), '-o', oracle]);
  const hardwareLimit = Number(execFileSync(oracle, { encoding: 'utf8' }).trim());
  assert.ok(Number.isSafeInteger(hardwareLimit) && hardwareLimit > 0);
  setupGlobals();
  const adapter = await navigator.gpu.requestAdapter();
  assert.ok(adapter, 'Metal adapter required');
  for (const key of ['maxBufferSize', 'maxStorageBufferBindingSize']) {
    assert.equal(adapter.limits[key], hardwareLimit, `adapter ${key}`);
  }
  device = await adapter.requestDevice({ requiredLimits: {
    maxBufferSize: hardwareLimit, maxStorageBufferBindingSize: hardwareLimit,
  } });
  for (const key of ['maxBufferSize', 'maxStorageBufferBindingSize']) {
    assert.equal(device.limits[key], hardwareLimit, `device ${key}`);
  }
  const size = Math.min(LARGE_BUFFER_BYTES, hardwareLimit);
  device.pushErrorScope('validation');
  const storage = device.createBuffer({ size,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
  const output = device.createBuffer({ size: WORD_BYTES,
    usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const module = device.createShaderModule({ code: `
    @group(0) @binding(0) var<storage, read_write> words: array<u32>;
    @compute @workgroup_size(1) fn main() {
      words[arrayLength(&words) - 1u] = words[0] + 1u;
    }` });
  const pipeline = device.createComputePipeline({ layout: 'auto',
    compute: { module, entryPoint: 'main' } });
  const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: storage, size } }] });
  for (let repeat = 0; repeat < 2; repeat++) {
    device.queue.writeBuffer(storage, 0, new Uint32Array([EXPECTED + repeat]));
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginComputePass();
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, group);
    pass.dispatchWorkgroups(1);
    pass.end();
    encoder.copyBufferToBuffer(storage, size - WORD_BYTES, output, 0, WORD_BYTES);
    device.queue.submit([encoder.finish()]);
    await output.mapAsync(GPUMapMode.READ);
    assert.equal(new Uint32Array(output.getMappedRange())[0], EXPECTED + repeat + 1);
    output.unmap();
  }
  assert.equal(await device.popErrorScope(), null);
  storage.destroy();
  output.destroy();
  console.log(JSON.stringify({ status: 'passed', hardwareLimit, storageBytes: size, repeats: 2 }));
} finally {
  device?.destroy();
  rmSync(temporary, { recursive: true, force: true });
}
