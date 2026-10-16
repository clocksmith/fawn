// Sibling lexical scopes must survive native shader lowering.
import assert from 'node:assert/strict';
import { requestNativeDeviceOrSkip } from './native-device-test-helper.js';

const device = await requestNativeDeviceOrSkip('nested shader blocks');
try {
  device.pushErrorScope('validation');
  const module = device.createShaderModule({ code: `
    @group(0) @binding(0) var<storage, read_write> output: array<u32>;
    var<workgroup> shared_values: array<u32, 4>;
    @compute @workgroup_size(1) fn main() {
      var sum = 0u;
      for (var i = 0u; i < 3u; i++) {
        { let load_base = i; sum += load_base; }
        { let load_base = i + 10u; sum += load_base; }
      }
      output[0] = sum;
      let value = 7u;
      { let value = 19u; output[1] = value; }
      output[2] = value;
      var index = 0u;
      loop {
        let next = index + 1u;
        continuing { index = next; break if index == 4u; }
      }
      output[3] = index;
      output[4] = first(5u);
      output[5] = bitcast<u32>(bitcast<f32>(output[0] ^ 0x80000000u));
      shared_values[0] = 9u;
      output[6] = shared_sum();
      output[7] = select(0u, 1u, i32(output[0]) >= -126);
    }
    fn first(x: u32) -> u32 { return second(x) + 1u; }
    fn second(x: u32) -> u32 { return x * 2u + output[0]; }
    fn shared_sum() -> u32 { shared_values[1] = 13u; return shared_values[0] + shared_values[1]; }
    ` });
  const pipeline = device.createComputePipeline({ layout: 'auto',
    compute: { module, entryPoint: 'main' } });
  const storage = device.createBuffer({ size: 32,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
  const readback = device.createBuffer({ size: 32,
    usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
  const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: storage } }] });
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, group);
  pass.dispatchWorkgroups(1);
  pass.end();
  encoder.copyBufferToBuffer(storage, 0, readback, 0, 32);
  device.queue.submit([encoder.finish()]);
  await readback.mapAsync(GPUMapMode.READ);
  const actual = Array.from(new Uint32Array(readback.getMappedRange()));
  assert.deepEqual(actual, [36, 19, 7, 4, 47, 0x80000024, 22, 1]);
  readback.unmap();
  assert.equal(await device.popErrorScope(), null);
  readback.destroy();
  storage.destroy();
  console.log(JSON.stringify({ status: 'passed', actual }));
} finally {
  device.destroy();
}
