// Physical packed-integer correctness controls; no performance verdicts.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';

const INPUT_STRIDE = 10;
const OUTPUT_STRIDE = 16;
const WORKGROUP_SIZE = 64;
const RANDOM_CASES = 1024;
const digest = (bytes) => createHash('sha256').update(bytes).digest('hex');
const [moduleArgument, libraryArgument, outputArgument] = process.argv.slice(2);
assert(moduleArgument && libraryArgument && outputArgument,
  'Usage: node run-packed-integer-controls.mjs <provider-module> <Doe-library-or-dawn> <output-directory>');
const outputRoot = resolve(outputArgument);
await mkdir(join(outputRoot, 'shaders'), { recursive: true });
const isDoe = libraryArgument !== 'dawn';
if (isDoe) {
  await readFile(resolve(libraryArgument));
  process.env.DOE_WEBGPU_LIB = resolve(libraryArgument);
}
const provider = await import(pathToFileURL(resolve(moduleArgument)).href);
Object.assign(globalThis, provider.globals);
if (isDoe) assert(provider.providerInfo().doeNative === true, 'The selected provider must load Doe');
const gpu = provider.create(['backend=vulkan']);
const adapter = await gpu.requestAdapter();
assert(adapter, 'The declared Vulkan provider must acquire a physical adapter');
const identity = Object.fromEntries(['vendor', 'device', 'architecture', 'description'].map(
  (key) => [key, adapter.info?.[key] ?? null]));
assert(/amd|radeon/i.test(JSON.stringify(identity)), 'This control requires AMD Vulkan');
const device = await adapter.requestDevice();
const results = [];

function unpack(word, signed) {
  return [0, 1, 2, 3].map((lane) => {
    const byte = Math.floor(word / 2 ** (lane * 8)) % 256;
    return signed && byte >= 128 ? byte - 256 : byte;
  });
}

function pack(values, minimum = null, maximum = null) {
  return values.reduce((word, value, lane) => {
    const bounded = minimum === null ? value : Math.max(minimum, Math.min(maximum, value));
    const byte = ((bounded % 256) + 256) % 256;
    return word + byte * 2 ** (lane * 8);
  }, 0) >>> 0;
}

function integerCase(row) {
  const unsigned = row.slice(0, 4);
  const signed = row.slice(4, 8).map((word) => word > 0x7fffffff ? word - 2 ** 32 : word);
  const left = row[8], right = row[9];
  const leftSigned = unpack(left, true), leftUnsigned = unpack(left, false);
  const rightSigned = unpack(right, true), rightUnsigned = unpack(right, false);
  return [...leftSigned, ...leftUnsigned, pack(signed), pack(unsigned),
    pack(signed, -128, 127), pack(unsigned, 0, 255),
    leftSigned.reduce((sum, value, lane) => sum + value * rightSigned[lane], 0),
    leftUnsigned.reduce((sum, value, lane) => sum + value * rightUnsigned[lane], 0),
    pack(leftSigned), pack(leftUnsigned)].map((value) => value >>> 0);
}

const rows = [];
// Exhaust every signed/unsigned byte pair, repeated in all four lanes.
for (let left = 0; left < 256; left++) {
  for (let right = 0; right < 256; right++) {
    rows.push([0, 255, 256, 0xffffffff, -129, -128, 127, 128,
      left * 0x01010101, right * 0x01010101].map((value) => value >>> 0));
  }
}
const edges = [0, 1, 127, 128, 255, 256, 0x7fffffff, 0x80000000, 0xffffff7f,
  0xffffff80, 0xffffff81, 0xffffffff, 0x80ff017f];
for (let index = 0; index < edges.length; index++) {
  rows.push(Array.from({ length: INPUT_STRIDE }, (_, lane) => edges[(index + lane) % edges.length]));
}
let state = 0x91e10da5;
const randomWord = () => {
  state ^= state << 13; state ^= state >>> 17; state ^= state << 5;
  return state >>> 0;
};
for (let index = 0; index < RANDOM_CASES; index++) rows.push(Array.from({ length: INPUT_STRIDE }, randomWord));
const integerInput = new Uint32Array(rows.flat());
const integerExpected = new Uint32Array(rows.flatMap(integerCase));
const integerShader = `requires packed_4x8_integer_dot_product;
@group(0) @binding(0) var<storage, read> input: array<u32>;
@group(0) @binding(1) var<storage, read_write> output: array<u32>;
@compute @workgroup_size(${WORKGROUP_SIZE}) fn main(@builtin(global_invocation_id) id: vec3u) {
  if (id.x >= ${rows.length}u) { return; }
  let p = id.x * ${INPUT_STRIDE}u;
  let q = id.x * ${OUTPUT_STRIDE}u;
  let unsigned = vec4u(input[p], input[p+1u], input[p+2u], input[p+3u]);
  let signed = bitcast<vec4i>(vec4u(input[p+4u], input[p+5u], input[p+6u], input[p+7u]));
  let a = input[p+8u]; let b = input[p+9u];
  let s = unpack4xI8(a); let u = unpack4xU8(a);
  output[q] = u32(s.x); output[q+1u] = u32(s.y);
  output[q+2u] = u32(s.z); output[q+3u] = u32(s.w);
  output[q+4u] = u.x; output[q+5u] = u.y; output[q+6u] = u.z; output[q+7u] = u.w;
  output[q+8u] = pack4xI8(signed); output[q+9u] = pack4xU8(unsigned);
  output[q+10u] = pack4xI8Clamp(signed); output[q+11u] = pack4xU8Clamp(unsigned);
  output[q+12u] = u32(dot4I8Packed(a, b)); output[q+13u] = dot4U8Packed(a, b);
  output[q+14u] = pack4xI8(s); output[q+15u] = pack4xU8(u);
}`;

async function runCase(name, shader, inputBytes, expected, dispatches, accepts = null) {
  const buffers = [];
  const shaderPath = join(outputRoot, 'shaders', `${name}.wgsl`);
  await writeFile(shaderPath, shader);
  await writeFile(join(outputRoot, `${name}-input.bin`), inputBytes);
  await writeFile(join(outputRoot, `${name}-expected.bin`), new Uint8Array(expected.buffer));
  let observed = null, failure = null;
  try {
    const input = device.createBuffer({ size: inputBytes.byteLength,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
    buffers.push(input);
    device.queue.writeBuffer(input, 0, inputBytes);
    const output = device.createBuffer({ size: expected.byteLength,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
    buffers.push(output);
    const readback = device.createBuffer({ size: expected.byteLength,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    buffers.push(readback);
    const module = device.createShaderModule({ code: shader });
    const compilation = await module.getCompilationInfo();
    assert(!compilation.messages.some((message) => message.type === 'error'),
      JSON.stringify(compilation.messages));
    const pipeline = await device.createComputePipelineAsync({ layout: 'auto',
      compute: { module, entryPoint: 'main' } });
    const bindings = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
      { binding: 0, resource: { buffer: input } }, { binding: 1, resource: { buffer: output } },
    ] });
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginComputePass();
    pass.setPipeline(pipeline); pass.setBindGroup(0, bindings);
    pass.dispatchWorkgroups(dispatches); pass.end();
    encoder.copyBufferToBuffer(output, 0, readback, 0, expected.byteLength);
    device.queue.submit([encoder.finish()]);
    await readback.mapAsync(GPUMapMode.READ);
    observed = new Uint32Array(readback.getMappedRange().slice(0));
    readback.unmap();
    await writeFile(join(outputRoot, `${name}-observed.bin`), new Uint8Array(observed.buffer));
    for (let index = 0; index < expected.length; index++) {
      if (accepts) assert(accepts(index, observed[index]), `${name}: output word ${index}: ${observed[index]}`);
      else assert.equal(observed[index], expected[index], `${name}: output word ${index}`);
    }
  } catch (error) {
    failure = { message: error.message, stack: error.stack };
  } finally {
    await device.queue.onSubmittedWorkDone();
    for (const buffer of buffers) buffer.destroy();
  }
  results.push({ name, shaderSha256: digest(shader), inputSha256: digest(inputBytes),
    expectedSha256: digest(new Uint8Array(expected.buffer)),
    observedSha256: observed ? digest(new Uint8Array(observed.buffer)) : null,
    outputWords: expected.length, dispatches, oracle: accepts ? 'exact-normalized-and-adjacent-binary16' : 'exact-u32', passed: failure === null, failure });
}

try {
  await runCase('integer-family', integerShader, new Uint8Array(integerInput.buffer),
    integerExpected, Math.ceil(rows.length / WORKGROUP_SIZE));
  const singleSource = `requires packed_4x8_integer_dot_product;
@group(0) @binding(0) var<storage, read> input: array<u32>;
@group(0) @binding(1) var<storage, read_write> output: array<u32>;
var<private> calls: u32;
fn word() -> u32 { calls += 1u; return input[calls - 1u]; }
fn negative_values() -> vec4i { calls += 1u; return vec4i(-129, -128, 127, 128); }
fn positive_values() -> vec4u { calls += 1u; return vec4u(0u, 255u, 256u, 0xffffffffu); }
@compute @workgroup_size(1) fn main() {
  output[0] = pack4xI8(negative_values()); output[1] = pack4xI8Clamp(negative_values());
  output[2] = pack4xU8(positive_values()); output[3] = pack4xU8Clamp(positive_values());
  output[4] = u32(unpack4xI8(word()).w); output[5] = unpack4xU8(word()).w;
  output[6] = u32(dot4I8Packed(word(), word())); output[7] = dot4U8Packed(word(), word());
  output[8] = calls;
}`;
  const sequence = new Uint32Array([0, 0, 0, 0, 0x80ff017f, 0x80ff017f,
    0x80808080, 0x7f7f7f7f, 0x01010101, 0x02020202]);
  await runCase('single-evaluation', singleSource, new Uint8Array(sequence.buffer),
    new Uint32Array([0x807f807f, 0x7f7f8080, 0xff00ff00, 0xffffff00,
      -128, 128, -65024, 8, 10]), 1);
  const normalizedValues = new Float32Array([-2, -1, -0.5, -0, 0, 0.25, 0.3,
    0.5, 0.75, 1, 2, 65504]);
  const normalizedExpected = new Uint32Array(normalizedValues.length * 3);
  // All frozen inputs except 0.3 are exactly representable in binary16.
  // WGSL permits either adjacent binary16 value for 0.3f; normalized results remain exact.
  const half = [0xc000, 0xbc00, 0xb800, 0x8000, 0, 0x3400, 0x34cd,
    0x3800, 0x3a00, 0x3c00, 0x4000, 0x7bff];
  for (let index = 0; index < normalizedValues.length; index++) {
    const values = Array.from({ length: 4 }, (_, lane) => normalizedValues[(index + lane) % normalizedValues.length]);
    normalizedExpected[index * 3] = pack(values.map((value) => Math.floor(0.5 + 255 * Math.max(0, Math.min(1, value)))));
    normalizedExpected[index * 3 + 1] = pack(values.map((value) => Math.floor(0.5 + 127 * Math.max(-1, Math.min(1, value)))));
    normalizedExpected[index * 3 + 2] = (half[index] + half[(index + 1) % half.length] * 65536) >>> 0;
  }
  await runCase('normalized-packing', `
@group(0) @binding(0) var<storage, read> input: array<f32>;
@group(0) @binding(1) var<storage, read_write> output: array<u32>;
@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3u) {
  let i = id.x; let v = vec4f(input[i], input[(i+1u)%12u], input[(i+2u)%12u], input[(i+3u)%12u]);
  output[i*3u] = pack4x8unorm(v); output[i*3u+1u] = pack4x8snorm(v);
  output[i*3u+2u] = pack2x16float(v.xy);
}`, new Uint8Array(normalizedValues.buffer), normalizedExpected, normalizedValues.length, (index, word) => {
    if (index % 3 !== 2) return word === normalizedExpected[index];
    const row = Math.floor(index / 3);
    const acceptsHalf = (lane, bits) => lane === 6 ? bits === 0x34cc || bits === 0x34cd : bits === half[lane];
    return acceptsHalf(row, word & 0xffff) && acceptsHalf((row + 1) % half.length, word >>> 16);
  });
  await runCase('module-constants', `requires packed_4x8_integer_dot_product;
const signed_word = pack4xI8(vec4i(-129, -128, 127, 128));
const unsigned_word = pack4xU8(vec4u(0u, 255u, 256u, 0xffffffffu));
const signed_clamp = pack4xI8Clamp(vec4i(-129, -128, 127, 128));
const unsigned_clamp = pack4xU8Clamp(vec4u(0u, 255u, 256u, 0xffffffffu));
const signed_bytes = unpack4xI8(0x80ff017f);
const unsigned_bytes = unpack4xU8(0x80ff017f);
const signed_dot = dot4I8Packed(0x80808080, 0x7f7f7f7f);
const unsigned_dot = dot4U8Packed(0xffffffff, 0xffffffff);
const splat = pack4xI8(vec4i(-1));
const joined = pack4xU8(vec4u(vec2u(1u, 2u), vec2u(3u, 4u)));
const inferred = pack4xU8(vec4(1, 2, 3, 4));
@group(0) @binding(0) var<storage, read> input: array<u32>;
@group(0) @binding(1) var<storage, read_write> output: array<u32>;
@compute @workgroup_size(1) fn main() {
  output[0] = signed_word; output[1] = unsigned_word;
  output[2] = signed_clamp; output[3] = unsigned_clamp;
  output[4] = u32(signed_bytes.x); output[5] = u32(signed_bytes.y);
  output[6] = u32(signed_bytes.z); output[7] = u32(signed_bytes.w);
  output[8] = unsigned_bytes.x; output[9] = unsigned_bytes.y;
  output[10] = unsigned_bytes.z; output[11] = unsigned_bytes.w;
  output[12] = u32(signed_dot); output[13] = unsigned_dot;
  output[14] = splat; output[15] = joined; output[16] = inferred;
  output[17] = pack4xU8(vec4(1, 2, 3, 4));
  output[18] = pack4xI8(vec4(-1, -2, 3, 4)); output[19] = input[0];
}`, new Uint8Array(new Uint32Array([42]).buffer), new Uint32Array([
    0x807f807f, 0xff00ff00, 0x7f7f8080, 0xffffff00,
    127, 1, -1, -128, 127, 1, 255, 128, -65024, 260100,
    0xffffffff, 0x04030201, 0x04030201, 0x04030201, 0x0403feff, 42]), 1);
  const normalized16Expected = new Uint32Array(normalizedValues.length * 4);
  for (let index = 0; index < normalizedValues.length; index++) {
    const values = [normalizedValues[index], normalizedValues[(index + 1) % normalizedValues.length]];
    const halves = (signed) => values.map((value) => {
      const rounded = Math.floor(0.5 + (signed ? 32767 : 65535) * Math.max(signed ? -1 : 0, Math.min(1, value)));
      return ((rounded % 65536) + 65536) % 65536;
    });
    const [u0, u1] = halves(false), [s0, s1] = halves(true);
    normalized16Expected.set([u0 + u1 * 65536, s0 + s1 * 65536,
      u0 + u1 * 65536, s0 + s1 * 65536], index * 4);
  }
  await runCase('normalized-16', `
@group(0) @binding(0) var<storage, read> input: array<f32>;
@group(0) @binding(1) var<storage, read_write> output: array<u32>;
@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3u) {
  let i = id.x; let v = vec2f(input[i], input[(i+1u)%12u]);
  let u = pack2x16unorm(v); let s = pack2x16snorm(v);
  output[i*4u] = u; output[i*4u+1u] = s;
  output[i*4u+2u] = pack2x16unorm(unpack2x16unorm(u));
  output[i*4u+3u] = pack2x16snorm(unpack2x16snorm(s));
}`, new Uint8Array(normalizedValues.buffer), normalized16Expected, normalizedValues.length);

} finally {
  await device.queue.onSubmittedWorkDone();
  device.destroy();
  const report = { schemaVersion: 1, classification: 'packed-integer-correctness',
    provider: isDoe ? 'doe' : 'dawn', identity,
    runnerSha256: digest(await readFile(fileURLToPath(import.meta.url))),
    providerModuleSha256: digest(await readFile(resolve(moduleArgument))),
    nativeLibrarySha256: isDoe ? digest(await readFile(resolve(libraryArgument))) : null,
    languageFeatures: [...(gpu.wgslLanguageFeatures ?? [])],
    providerInfo: provider.providerInfo ? provider.providerInfo() : null,
    loadedLibraryObserved: isDoe ? (await readFile('/proc/self/maps', 'utf8')).includes(resolve(libraryArgument)) : null,
    results,
    passed: results.length === 5 && results.every((result) => result.passed) };
  if (isDoe) report.passed &&= report.providerInfo?.doeNative === true && report.loadedLibraryObserved && report.languageFeatures.includes('packed_4x8_integer_dot_product');
  await writeFile(join(outputRoot, 'result.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report));
  if (!report.passed) process.exitCode = 1;
}
