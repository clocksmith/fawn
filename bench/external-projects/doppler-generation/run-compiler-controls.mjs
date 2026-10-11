import { readFile, writeFile } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import { createHash } from 'node:crypto';
import { pathToFileURL, fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const [providersPath, lane, output] = process.argv.slice(2);
assert(['doe', 'dawn'].includes(lane));
const hash = async (path) => {
  const digest = createHash('sha256');
  for await (const bytes of createReadStream(path)) digest.update(bytes);
  return digest.digest('hex');
};
const manifest = JSON.parse(await readFile(providersPath));
for (const file of [...manifest.archives, ...manifest.files]) assert.equal(await hash(file.path), file.sha256);
if (lane === 'doe') process.env.DOE_WEBGPU_LIB = manifest.doeLibrary;
const provider = await import(pathToFileURL(manifest[`${lane}Module`]).href);
Object.assign(globalThis, provider.globals);
const gpu = provider.create(['backend=vulkan']);
const adapter = await gpu.requestAdapter();
const identity = { vendor: adapter.info?.vendor, device: adapter.info?.device,
  architecture: adapter.info?.architecture, description: adapter.info?.description };
assert(/amd|radeon/i.test(JSON.stringify(identity)));
const device = await adapter.requestDevice();
const values = [0, 1, 2, 3, 7, 8, 0x7fffffff, 0x80000000, 0x80000001, 0xfffffffe, 0xffffffff];
const expected = values.flatMap((value) => {
  const signedBits = value & 0x80000000 ? (~value) >>> 0 : value;
  return [(31 - Math.clz32(value)) >>> 0, (31 - Math.clz32(signedBits)) >>> 0,
    Math.clz32(value), value % 4 === 1 ? 11 : value % 4 === 2 ? 13 : value % 4 === 3 ? 17 : 7,
    value === 0 ? 19 : 23];
});
const shader = `
@group(0) @binding(0) var<storage, read> input: array<u32>;
@group(0) @binding(1) var<storage, read_write> output: array<u32>;
fn returning(x: u32) -> u32 {
  switch x & 3u {
    case 1u: { return 11u; }
    case 2u: { return 13u; }
    case 3u: { return 17u; }
    default: { return 7u; }
  }
}
fn breaking(x: u32) -> u32 {
  switch x {
    case 0u: {
      switch x { case 0u: { break; } default: { return 29u; } }
      break;
    }
    default: { return 23u; }
  }
  return 19u;
}
@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  let x = input[id.x];
  output[id.x * 5u] = firstLeadingBit(x);
  output[id.x * 5u + 1u] = u32(firstLeadingBit(bitcast<i32>(x)));
  output[id.x * 5u + 2u] = countLeadingZeros(x);
  output[id.x * 5u + 3u] = returning(x);
  output[id.x * 5u + 4u] = breaking(x);
}`;
const buffers = [];
let observed = null, failure = null;
try {
  const input = device.createBuffer({ size: values.length * 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
  buffers.push(input);
  device.queue.writeBuffer(input, 0, new Uint32Array(values));
  const result = device.createBuffer({ size: expected.length * 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
  buffers.push(result);
  const readback = device.createBuffer({ size: expected.length * 4, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  buffers.push(readback);
  const pipeline = await device.createComputePipelineAsync({ layout: 'auto', compute: { module: device.createShaderModule({ code: shader }), entryPoint: 'main' } });
  const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
    { binding: 0, resource: { buffer: input } }, { binding: 1, resource: { buffer: result } }] });
  const encoder = device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline); pass.setBindGroup(0, group); pass.dispatchWorkgroups(values.length); pass.end();
  encoder.copyBufferToBuffer(result, 0, readback, 0, expected.length * 4);
  device.queue.submit([encoder.finish()]);
  await readback.mapAsync(GPUMapMode.READ);
  observed = [...new Uint32Array(readback.getMappedRange())];
  readback.unmap();
  assert.deepEqual(observed, expected);
} catch (error) { failure = { message: error.message, stack: error.stack }; }
finally {
  await device.queue.onSubmittedWorkDone();
  for (const buffer of buffers) buffer.destroy();
  device.destroy();
  await writeFile(output, JSON.stringify({ schemaVersion: 1, classification: 'physical-compiler-regression', lane,
    providerManifestSha256: await hash(providersPath), runnerSha256: await hash(fileURLToPath(import.meta.url)),
    shaderSha256: createHash('sha256').update(shader).digest('hex'), identity, values, expected, observed, passed: !failure, failure }, null, 2) + '\n');
}
if (failure) { console.error(failure); process.exitCode = 1; }
