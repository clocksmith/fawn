import assert from 'node:assert/strict';

// Both package backends must retain pipeline specialization and bind-group changes.
export async function checkComputeStageBindings(device, globals, { unbindFirst = false } = {}) {
  const outputs = [];
  let readback;
  try {
    const module = device.createShaderModule({ code: `
      override scale: f32 = 1.0;
      override adjustment: f32 = 0.0;
      @group(0) @binding(0) var<storage, read_write> result: array<f32>;
      @compute @workgroup_size(1) fn main() { result[0] = scale * 4.0 + adjustment; }
    ` });
    const pipeline = device.createComputePipeline({
      layout: 'auto', compute: { module, entryPoint: 'main', constants: { scale: 2.5, adjustment: -3 } },
    });
    const defaultPipeline = device.createComputePipeline({
      layout: 'auto', compute: { module, entryPoint: 'main' },
    });
    const { GPUBufferUsage, GPUMapMode } = globals;
    readback = device.createBuffer({ size: 8, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginComputePass();
    if (unbindFirst) pass.setBindGroup(0, null);
    for (const current of [pipeline, defaultPipeline]) {
      const output = device.createBuffer({ size: 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
      outputs.push(output);
      const group = device.createBindGroup({
        layout: current.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: output } }],
      });
      // Bind repeatedly, then switch both resources in the same pass.
      pass.setPipeline(current);
      pass.setBindGroup(0, group);
      pass.setPipeline(current);
      pass.setBindGroup(0, group);
      pass.dispatchWorkgroups(1);
    }
    pass.end();
    for (const [index, output] of outputs.entries()) {
      encoder.copyBufferToBuffer(output, 0, readback, index * 4, 4);
    }
    device.queue.submit([encoder.finish()]);
    await readback.mapAsync(GPUMapMode.READ);
    assert.deepEqual(Array.from(new Float32Array(readback.getMappedRange())), [7, 4]);
    readback.unmap();
  } finally {
    readback?.destroy();
    for (const output of outputs) output.destroy();
  }
}
