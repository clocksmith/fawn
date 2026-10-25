// Exercise native reflection failures across the actual addon boundary.
import assert from 'node:assert/strict';
import { requestAdapter, globals } from '../../src/native.js';
import { loadNativeAddon } from './native-addon-test-helper.js';

const { GPUBufferUsage: U, GPUMapMode: M } = globals;
const RESULT_BYTES = 64;
const adapter = await requestAdapter({ backend: process.platform === 'darwin' ? 'metal' : 'vulkan' });
const device = await adapter.requestDevice();
const addon = await loadNativeAddon();
const valid = device.createShaderModule({ code: '@compute @workgroup_size(1) fn main() {}' });
const invalid = device.createShaderModule({ code: 'not WGSL' });
try {
  assert.deepEqual(addon.shaderModuleGetBindings(valid._native), []);
  assert.throws(() => addon.shaderModuleGetBindingsForEntryPoint(valid._native, 'missing'),
    { code: 'DOE_SHADER_REFLECTION_ERROR' });
  assert.throws(() => addon.shaderModuleGetBindings(invalid._native),
    { code: 'DOE_SHADER_REFLECTION_ERROR' });
  assert.deepEqual(addon.shaderModuleGetBindingsForEntryPoint(valid._native, 'main'), []);
  assert((await invalid.getCompilationInfo()).messages.some((message) => message.type === 'error'));
  assert.deepEqual((await valid.getCompilationInfo()).messages, []);
  console.log('ok: native reflection rejects failures, preserves empty success, and keeps module diagnostics');

  const shader = device.createShaderModule({ code: `
    const COUNT: u32 = countOneBits(3u);
    const SIGNED: i32 = countOneBits(-1i);
    const COUNTS: vec2<u32> = countOneBits(vec2<u32>(0u, 0x55555555u));
    const BYTES: vec4<u32> = unpack4xU8(0xff800100u);
    const ARRAY: array<u32, 3> = array<u32, 3>(5u, 7u, 11u);
    @group(0) @binding(0) var<storage, read_write> output: array<u32>;
    fn packed() -> u32 { output[15] += 1u; return 0x44332211u; }
    @compute @workgroup_size(1) fn main() {
      output[0] = COUNT; output[1] = u32(SIGNED);
      output[2] = COUNTS.x; output[3] = COUNTS.y;
      output[4] = COUNTS[output[0] - 1u]; output[5] = ARRAY[2];
      output[6] = countOneBits(output[0]);
      output[7] = u32(countOneBits(bitcast<i32>(output[5])));
      output[8] = BYTES.w;
      let bytes = unpack4xU8(packed());
      output[9] = bytes.x; output[10] = bytes.y; output[11] = bytes.z; output[12] = bytes.w;
      let counts = countOneBits(vec2<u32>(output[0], output[5]));
      output[13] = counts.x; output[14] = counts.y;
    }` });
  const buffer = device.createBuffer({ size: RESULT_BYTES, usage: U.STORAGE | U.COPY_SRC });
  const readback = device.createBuffer({ size: RESULT_BYTES, usage: U.MAP_READ | U.COPY_DST });
  let pipeline;
  let group;
  let layout;
  try {
    assert.deepEqual((await shader.getCompilationInfo()).messages, []);
    pipeline = device.createComputePipeline({ layout: 'auto', compute: { module: shader, entryPoint: 'main' } });
    layout = pipeline.getBindGroupLayout(0);
    group = device.createBindGroup({ layout, entries: [{ binding: 0, resource: { buffer } }] });
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginComputePass();
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, group);
    pass.dispatchWorkgroups(1);
    pass.end();
    encoder.copyBufferToBuffer(buffer, 0, readback, 0, RESULT_BYTES);
    device.queue.submit([encoder.finish()]);
    await readback.mapAsync(M.READ);
    assert.deepEqual(Array.from(new Uint32Array(readback.getMappedRange())), [2, 32, 0, 16, 16, 11, 1, 3, 255, 17, 34, 51, 68, 1, 3, 1]);
    readback.unmap();
    console.log('ok: constant builtin initializers execute their computed scalar and vector values');
  } finally {
    group?.destroy?.();
    layout?.destroy?.();
    pipeline?.destroy?.();
    shader.destroy?.();
    buffer.destroy();
    readback.destroy();
  }
  const supported = device.createShaderModule({ code: 'const VALUE: f32 = sin(1.0); @compute @workgroup_size(1) fn main() {}' });
  const unsupported = device.createShaderModule({ code: 'const VALUE: f32 = pow(-1.0, 2.0); @compute @workgroup_size(1) fn main() {}' });
  try {
    assert.deepEqual((await supported.getCompilationInfo()).messages, []);
    assert((await unsupported.getCompilationInfo()).messages.some((message) => message.type === 'error'));
    assert.deepEqual((await valid.getCompilationInfo()).messages, []);
    console.log('ok: supported constant math compiles, invalid domains fail, and earlier diagnostics remain intact');
  } finally { supported.destroy?.(); unsupported.destroy?.(); }
} finally {
  valid.destroy?.();
  invalid.destroy?.();
  device.destroy();
  adapter.destroy();
}
