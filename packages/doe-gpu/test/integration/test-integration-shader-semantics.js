#!/usr/bin/env node
// Ordinary package execution must preserve the complete shader and selected entry point.

import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { setupGlobals, providerInfo } from '../../src/index.js';
import { releaseOwnedResource } from '../../src/vendor/webgpu/shared/resource-lifecycle.js';

setupGlobals();
const adapter = await navigator.gpu.requestAdapter();
assert.ok(adapter, 'shader semantics regression requires a native adapter');
const device = await adapter.requestDevice();
console.log(JSON.stringify({ provider: providerInfo(), adapter: adapter.info }));

const CLEAR = `
@group(0) @binding(0) var<storage, read_write> data: array<u32>;
@compute @workgroup_size(1) fn main() {
  for (var i: u32 = 0u; i < arrayLength(&data); i = i + 1u) { data[i] = 0u; }
  data[0] = 7u;
}`;
const POINTER_ALIAS = `
@group(0) @binding(0) var<storage, read_write> data: array<u32>;
fn change(p: ptr<function, u32>) { *p += 5u; }
@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3u) {
  var x = id.x + 1u;
  data[id.x * 5u] = x + x;
  x += 3u;
  data[id.x * 5u + 1u] = x;
  let pointerAlias = &x;
  change(pointerAlias);
  data[id.x * 5u + 2u] = x;
  if ((id.x % 2u) == 0u) { x += 10u; } else { x += 20u; }
  data[id.x * 5u + 3u] = x;
  for (var j = 0u; j < 3u; j++) { x += j; }
  data[id.x * 5u + 4u] = x;
}`;
const POINTER_EXPECTED = [2, 4, 9, 19, 22, 4, 5, 10, 30, 33,
  6, 6, 11, 21, 24, 8, 7, 12, 32, 35];
const CASES = [
  {
    label: 'integer bit scans preserve signed edges, vectors, constants and saved effects',
    code: `@group(0) @binding(0) var<storage,read_write> data:array<u32>;
fn changed()->u32{data[49]+=1u;return 1u;}
@compute @workgroup_size(1) fn main(){
var u=vec4u(0u,1u,0x80000000u,0xffffffffu);
var s=bitcast<vec4i>(u);
let v0=reverseBits(u);
data[0]=bitcast<u32>(v0.x);
data[1]=bitcast<u32>(v0.y);
data[2]=bitcast<u32>(v0.z);
data[3]=bitcast<u32>(v0.w);
let v4=reverseBits(s);
data[4]=bitcast<u32>(v4.x);
data[5]=bitcast<u32>(v4.y);
data[6]=bitcast<u32>(v4.z);
data[7]=bitcast<u32>(v4.w);
let v8=countLeadingZeros(u);
data[8]=bitcast<u32>(v8.x);
data[9]=bitcast<u32>(v8.y);
data[10]=bitcast<u32>(v8.z);
data[11]=bitcast<u32>(v8.w);
let v12=countLeadingZeros(s);
data[12]=bitcast<u32>(v12.x);
data[13]=bitcast<u32>(v12.y);
data[14]=bitcast<u32>(v12.z);
data[15]=bitcast<u32>(v12.w);
let v16=countTrailingZeros(u);
data[16]=bitcast<u32>(v16.x);
data[17]=bitcast<u32>(v16.y);
data[18]=bitcast<u32>(v16.z);
data[19]=bitcast<u32>(v16.w);
let v20=countTrailingZeros(s);
data[20]=bitcast<u32>(v20.x);
data[21]=bitcast<u32>(v20.y);
data[22]=bitcast<u32>(v20.z);
data[23]=bitcast<u32>(v20.w);
let v24=firstLeadingBit(u);
data[24]=bitcast<u32>(v24.x);
data[25]=bitcast<u32>(v24.y);
data[26]=bitcast<u32>(v24.z);
data[27]=bitcast<u32>(v24.w);
let v28=firstLeadingBit(s);
data[28]=bitcast<u32>(v28.x);
data[29]=bitcast<u32>(v28.y);
data[30]=bitcast<u32>(v28.z);
data[31]=bitcast<u32>(v28.w);
let v32=firstTrailingBit(u);
data[32]=bitcast<u32>(v32.x);
data[33]=bitcast<u32>(v32.y);
data[34]=bitcast<u32>(v32.z);
data[35]=bitcast<u32>(v32.w);
let v36=firstTrailingBit(s);
data[36]=bitcast<u32>(v36.x);
data[37]=bitcast<u32>(v36.y);
data[38]=bitcast<u32>(v36.z);
data[39]=bitcast<u32>(v36.w);
var negative=vec4i(-1,-2,-3,-2147483648);
let n=firstLeadingBit(negative);
data[40]=bitcast<u32>(n.x);
data[41]=bitcast<u32>(n.y);
data[42]=bitcast<u32>(n.z);
data[43]=bitcast<u32>(n.w);
const c0=reverseBits(1u);data[44]=bitcast<u32>(c0);
const c1=countLeadingZeros(0i);data[45]=bitcast<u32>(c1);
const c2=countTrailingZeros(0u);data[46]=bitcast<u32>(c2);
const c3=firstLeadingBit(-1i);data[47]=bitcast<u32>(c3);
const c4=firstTrailingBit(0u);data[48]=bitcast<u32>(c4);
data[49]=0u;data[50]=reverseBits(changed());
}`,
    expected: [
      0, 2147483648, 1, 4294967295, 0, 2147483648, 1, 4294967295,
      32, 31, 0, 0, 32, 31, 0, 0,
      32, 0, 31, 0, 32, 0, 31, 0,
      4294967295, 0, 31, 31, 4294967295, 0, 30, 4294967295,
      4294967295, 0, 31, 0, 4294967295, 0, 31, 0,
      4294967295, 0, 1, 30, 2147483648, 32, 32, 4294967295,
      4294967295, 1, 2147483648,
    ],
  },
  {
    label: 'packed integer byte order, sign extension, clamps and argument effects',
    code: `requires packed_4x8_integer_dot_product;
const c0 = pack4xU8(vec4u(0u,255u,256u,0xffffffffu))==0xff00ff00u;
const c1 = pack4xI8(vec4i(-129,-128,127,128))==0x807f807fu;
const c2 = pack4xU8Clamp(vec4u(0u,255u,256u,0xffffffffu))==0xffffff00u;
const c3 = pack4xI8Clamp(vec4i(-129,-128,127,128))==0x7f7f8080u;
const c4 = all(unpack4xU8(0x807fff01u)==vec4u(1u,255u,127u,128u));
const c5 = all(unpack4xI8(0x807fff01u)==vec4i(1,-1,127,-128));
const c6 = dot4I8Packed(0x807fff01u,0x02ff0380u)==-514i;
const c7 = dot4U8Packed(0x807fff01u,0x02ff0380u)==33534u;
@group(0) @binding(0) var<storage,read_write> data:array<u32>;
fn left()->u32{data[14]=data[14]*10u+1u;
return 0x807fff01u;
}fn right()->u32{data[14]=data[14]*10u+2u;
return 0x02ff0380u;
}@compute @workgroup_size(1) fn main(){var u=vec4u(0u,255u,256u,0xffffffffu);
var s=vec4i(-129,-128,127,128);
data[0]=pack4xU8(u);
data[1]=pack4xI8(s);
data[2]=pack4xU8Clamp(u);
data[3]=pack4xI8Clamp(s);
var p=0x807fff01u;
var q=0x02ff0380u;
let a=unpack4xU8(p);
let b=unpack4xI8(p);
data[4]=a.x;
data[5]=a.y;
data[6]=a.z;
data[7]=a.w;
data[8]=bitcast<u32>(b.x);
data[9]=bitcast<u32>(b.y);
data[10]=bitcast<u32>(b.z);
data[11]=bitcast<u32>(b.w);
data[12]=bitcast<u32>(dot4I8Packed(p,q));
data[13]=dot4U8Packed(p,q);
data[14]=0u;
data[15]=dot4U8Packed(left(),right());
data[16]=u32(c0);
data[17]=u32(c1);
data[18]=u32(c2);
data[19]=u32(c3);
data[20]=u32(c4);
data[21]=u32(c5);
data[22]=u32(c6);
data[23]=u32(c7);
}`,
    expected: [0xff00ff00, 0x807f807f, 0xffffff00, 0x7f7f8080,
      1, 255, 127, 128, 1, 0xffffffff, 127, 0xffffff80, 0xfffffdfe, 33534, 12, 33534,
      1, 1, 1, 1, 1, 1, 1, 1],
  },
  {
    label: 'texture gather preserves selected components and four-texel order', texture: 'gather',
    code: `@group(0) @binding(0) var image: texture_2d<f32>;
      @group(0) @binding(1) var sampleState: sampler;
      @group(1) @binding(0) var<storage, read_write> data: array<u32>;
      @compute @workgroup_size(1) fn main() {
        let red = vec4u(round(textureGather(0u, image, sampleState, vec2f(0.5)) * 255.0));
        let blue = vec4u(round(textureGather(2u, image, sampleState, vec2f(0.5)) * 255.0));
        data[0] = red.x; data[1] = red.y; data[2] = red.z; data[3] = red.w;
        data[4] = blue.x; data[5] = blue.y; data[6] = blue.z; data[7] = blue.w;
      }`,
    expected: [3, 4, 2, 1, 30, 40, 20, 10],
  },
  {
    label: 'uniform workgroup loads snapshot aggregate and atomic values before following writes',
    code: `@group(0) @binding(0) var<storage, read_write> data: array<u32>;
      var<workgroup> values: array<vec2u, 2>;
      var<workgroup> atom: atomic<u32>;
      @compute @workgroup_size(4) fn main(@builtin(local_invocation_index) lane: u32) {
        if (lane == 0u) { values[1] = vec2u(7u, 11u); atomicStore(&atom, 13u); }
        let pair = workgroupUniformLoad(&values[1]);
        if (lane == 0u) { values[1] = vec2u(99u); }
        let number = workgroupUniformLoad(&atom);
        if (lane == 0u) { atomicStore(&atom, 101u); }
        if (pair.x == 7u && number == 13u) { workgroupBarrier(); }
        data[lane * 3u] = pair.x; data[lane * 3u + 1u] = pair.y;
        data[lane * 3u + 2u] = number;
      }`,
    expected: [7, 11, 13, 7, 11, 13, 7, 11, 13, 7, 11, 13],
  },
  {
    label: 'texture barrier publishes neighboring texels within each workgroup', texture: 'storage',
    code: `@group(0) @binding(0) var image: texture_storage_2d<r32uint, read_write>;
      @group(1) @binding(0) var<storage, read_write> data: array<u32>;
      @compute @workgroup_size(4) fn main(@builtin(local_invocation_index) lane: u32,
                                         @builtin(workgroup_id) group: vec3u) {
        let base = group.x * 4u;
        textureStore(image, vec2u(base + lane, 0u), vec4u(30u + base + lane));
        textureBarrier();
        data[base + lane] = textureLoad(image, vec2u(base + ((lane + 1u) % 4u), 0u)).x;
      }`,
    dispatch: 2,
    expected: [31, 32, 33, 30, 35, 36, 37, 34],
  },
  {
    label: 'storage barrier publishes neighboring invocation writes within each workgroup',
    code: `@group(0) @binding(0) var<storage, read_write> data: array<u32>;
      fn publish() { storageBarrier(); }
      @compute @workgroup_size(4) fn main(@builtin(local_invocation_index) lane: u32,
                                         @builtin(workgroup_id) group: vec3u) {
        let base = group.x * 8u;
        data[base + lane] = 20u + group.x * 4u + lane;
        publish();
        data[base + 4u + lane] = data[base + ((lane + 1u) % 4u)];
      }`,
    dispatch: 2,
    expected: [20, 21, 22, 23, 21, 22, 23, 20, 24, 25, 26, 27, 25, 26, 27, 24],
  },
  {
    label: 'mix preserves constant and runtime scalar/vector factors and ordered effects',
    code: `const CONSTANT: vec2f = mix(vec2f(2.0, 4.0), vec2f(6.0, 8.0), 0.25);
      @group(0) @binding(0) var<storage, read_write> data: array<u32>;
      var<private> calls: u32;
      fn next() -> f32 { calls++; return f32(calls) * 2.0; }
      @compute @workgroup_size(1) fn main() {
        let scalar = mix(next(), next(), next());
        let vector = mix(vec2f(2.0, 4.0), vec2f(6.0, 12.0), vec2f(0.25, 0.75));
        let factor = 0.5;
        let broadcast = mix(vec2f(2.0, 4.0), vec2f(6.0, 12.0), factor);
        data[0] = u32(CONSTANT.x); data[1] = u32(CONSTANT.y);
        data[2] = u32(scalar); data[3] = calls;
        data[4] = u32(vector.x); data[5] = u32(vector.y);
        data[6] = u32(broadcast.x); data[7] = u32(broadcast.y);
      }`,
    expected: [3, 5, 14, 3, 3, 10, 4, 8],
  },
  {
    label: 'private aggregate initializers remain invocation-local beside shared storage',
    code: `const INITIAL: u32 = 13u;
      struct PrivateState { values: array<u32, 2>, flag: bool }
      @group(0) @binding(0) var<storage, read_write> data: array<u32>;
      var state = PrivateState(array<u32, 2>(11u, INITIAL), true);
      var<private> inferred = 3;
      var<workgroup> sharedValue: u32;
      fn mutate(value: u32) -> u32 { state.values[1] += value; return state.values[1]; }
      @compute @workgroup_size(4) fn main(@builtin(global_invocation_id) id: vec3u,
                                         @builtin(local_invocation_index) lane: u32) {
        if (lane == 0u) { sharedValue = 7u; }
        workgroupBarrier();
        let offset = id.x * 4u;
        data[offset] = state.values[0] + u32(state.flag);
        data[offset + 1u] = mutate(id.x);
        data[offset + 2u] = sharedValue;
        data[offset + 3u] = u32(inferred);
        inferred += 1; state.flag = false;
      }`,
    dispatch: 2,
    expected: [12, 13, 7, 3, 12, 14, 7, 3, 12, 15, 7, 3, 12, 16, 7, 3,
      12, 17, 7, 3, 12, 18, 7, 3, 12, 19, 7, 3, 12, 20, 7, 3],
  },
  {
    label: 'contextual member names and valid aliases remain executable',
    code: `alias Word = u32;
      struct Values { storage: Word, read: Word }
      @group(0) @binding(0) var<storage, read_write> data: array<u32>;
      @compute @workgroup_size(1) fn main() {
        let _value = Values(7u, 9u);
        data[0] = _value.storage + _value.read;
      }`,
    expected: [16],
  },
  {
    label: 'private default values and mutation are isolated per invocation',
    code: `struct State { vector: vec4f, matrix: mat2x2f, values: array<u32, 2> }
      @group(0) @binding(0) var<storage, read_write> data: array<u32>;
      var<private> state: State;
      var<private> counter: u32;
      var<private> flag: bool;
      var<private> initial: u32 = 17u;
      @compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3u) {
        let offset = id.x * 4u;
        data[offset] = counter + u32(flag);
        counter += id.x + 1u;
        data[offset + 1u] = counter;
        data[offset + 2u] = u32(state.vector.x + state.matrix[1][0]) + state.values[1];
        data[offset + 3u] = initial;
        state.values[1] = counter; flag = true;
      }`,
    dispatch: 4, expected: [0, 1, 0, 17, 0, 2, 0, 17, 0, 3, 0, 17, 0, 4, 0, 17],
  },
  {
    label: 'loop invariants preserve nested branches and modular overflow',
    code: `@group(0) @binding(0) var<storage, read_write> data: array<u32>;
      @compute @workgroup_size(1) fn main() {
        let base = data[0]; let stride = data[1]; var sum = 0u;
        for (var i = 0u; i < 4u; i++) {
          if (i != 1u) { sum += base * stride + i; }
          for (var j = 0u; j < 2u; j++) { sum += base * stride + j; }
        }
        data[0] = sum;
        data[1] = (base << 31u) * stride;
      }`,
    expected: [284, 0x80000000],
  },
  {
    label: 'loop calls and storage mutation retain evaluation order',
    code: `@group(0) @binding(0) var<storage, read_write> data: array<u32>;
      fn change(p: ptr<function, u32>) { *p += 2u; }
      @compute @workgroup_size(1) fn main() {
        var base = data[0]; let stride = data[1]; var localSum = 0u; var storageSum = 0u;
        let pointerAlias = &base;
        for (var i = 0u; i < 4u; i++) {
          localSum += base * stride;
          storageSum += data[0] * stride;
          change(pointerAlias); data[0]++;
        }
        data[0] = localSum; data[1] = storageSum; data[2] = base;
      }`,
    expected: [160, 130, 13],
  },
  {
    label: 'zero-iteration loops do not evaluate calls or division',
    code: `@group(0) @binding(0) var<storage, read_write> data: array<u32>;
      var<private> calls: u32;
      fn next() -> u32 { calls++; return calls; }
      @compute @workgroup_size(1) fn main() {
        let base = data[0]; let divisor = data[1]; var sum = 0u;
        for (var i = 0u; i < 0u; i++) { sum += next() + base / divisor; }
        data[0] = sum; data[1] = calls;
      }`,
    expected: [0, 0],
  },
  { label: 'named pointer alias mutation', code: POINTER_ALIAS, dispatch: 4,
    expected: POINTER_EXPECTED },
  { label: 'direct pointer mutation',
    code: POINTER_ALIAS.replace('let pointerAlias = &x;\n  change(pointerAlias);', 'change(&x);'),
    dispatch: 4, expected: POINTER_EXPECTED },
  {
    label: 'pointer member and captured index',
    code: `struct Pair { a: u32, b: u32 }
      @group(0) @binding(0) var<storage, read_write> data: array<u32>;
      @compute @workgroup_size(1) fn main() {
        var pair = Pair(3u, 4u);
        let pairPtr = &pair;
        (*pairPtr).b += 2u;
        var values = array<u32, 3>(5u, 6u, 7u);
        var index = 1u;
        let elementPtr = &values[index];
        index = 2u;
        *elementPtr += 4u;
        data[0] = values[1u];
        data[1] = values[2u];
        data[2] = pair.b;
      }`,
    expected: [10, 7, 6],
  },
  {
    label: 'unsigned byte unpack preserves order, zero extension, and single evaluation',
    code: `@group(0) @binding(0) var<storage, read_write> data: array<u32>;
      var<private> calls: u32;
      fn nextWord() -> u32 { calls += 1u; return 0x80ff017fu; }
      @compute @workgroup_size(1) fn main() {
        let mixed = unpack4xU8(nextWord());
        let zero = unpack4xU8(0u);
        let full = unpack4xU8(0xffffffffu);
        let low = unpack4xU8(0xf1e2d3c4u & 0x0f0f0f0fu);
        let high = unpack4xU8((0xf1e2d3c4u >> 4u) & 0x0f0f0f0fu);
        for (var i = 0u; i < 4u; i++) {
          data[i] = mixed[i]; data[4u + i] = zero[i]; data[8u + i] = full[i];
          data[12u + i] = low[i]; data[16u + i] = high[i];
        }
        data[20] = calls;
      }`,
    expected: [127, 1, 255, 128, 0, 0, 0, 0, 255, 255, 255, 255,
      4, 3, 2, 1, 12, 13, 14, 15, 1],
  },
  { label: 'clear followed by write', code: CLEAR, expected: [7, 0, 0, 0] },
  {
    label: 'additional writes', code: CLEAR.replace('data[0] = 7u;',
      'data[0] = 7u; data[3] = 19u;'), expected: [7, 0, 0, 19],
  },
  {
    label: 'selected alternate entry point', entryPoint: 'other',
    code: `${CLEAR}\n@compute @workgroup_size(1) fn other() { data[0] = 31u; }`,
    expected: [31, 5, 5, 5],
  },
  { label: 'zero dispatch', code: CLEAR, dispatch: 0, expected: [5, 5, 5, 5] },
  {
    label: 'comment match is not executable',
    code: `@group(0) @binding(0) var<storage, read_write> data: array<u32>;
      // arrayLength(&data) for (var i = 0u; i < 4u; i++) { data[i] = 0u; }
      @compute @workgroup_size(1) fn main() { data[0] = 23u; }`,
    expected: [23, 5, 5, 5],
  },
  {
    label: 'generated fill preserves extra operation',
    code: `struct OutputBuffer { value: array<u32> }
      @group(0) @binding(0) var<storage, read_write> dst : OutputBuffer;
      @compute @workgroup_size(1) fn main(@builtin(global_invocation_id) GlobalInvocationID: vec3u) {
        var xExtent: u32 = 4u * 1u;
        var yExtent: u32 = 1u * 1u;
        var zExtent: u32 = 1u * 1u;
        if (GlobalInvocationID.x > xExtent || GlobalInvocationID.y > yExtent || GlobalInvocationID.z > zExtent) { return; }
        let index = GlobalInvocationID.x;
        var val: u32 = 11u;
        dst.value[index] = val;
        dst.value[index] = dst.value[index] + 2u;
      }`, dispatch: 4, expected: [13, 13, 13, 13],
  },
  {
    label: 'texture dimensions preserve arithmetic and partial dispatch', texture: true,
    code: `@group(0) @binding(0) var texture: texture_2d<f32>;
      @group(1) @binding(0) var<storage, read_write> results: array<vec4<u32>>;
      fn getValue() -> vec4u { return vec4u(textureDimensions(texture, 0), 7u, 9u); }
      @compute @workgroup_size(1) fn main() { results[0] = getValue() + vec4u(1u); }`,
    expected: [9, 5, 8, 10, 5, 5, 5, 5],
  },
];

async function runIdentifierAdmissionCase() {
  for (const declaration of [
    'const value = mix(1u, 2u, 0u);',
    'const value = mix(vec3f(1.0), vec3f(2.0), vec2f(0.5));',
    'const value = mix(1.0, 2.0, true);',
    'var<private> value: atomic<u32>;',
    'override N: u32 = 1u; var<private> value: u32 = N;',
    'var<private> first: u32; var<private> second: u32 = first;',
    'var<workgroup> value: u32 = 0u;',
    'var<private, read_write> value: u32;',
    'var<private> value: array<u32>;',
    'var<private> value: u32 = -1i;',
  ]) {
    const module = device.createShaderModule({ code: `${declaration} @compute @workgroup_size(1) fn main() {}` });
    try {
      assert((await module.getCompilationInfo()).messages.some(message => message.type === 'error'), declaration);
    } finally { releaseOwnedResource(module); }
  }
  const declarations = [
    name => `var<private> ${name}: u32;`,
    name => `override ${name}: u32 = 1u;`,
    name => `const ${name}: u32 = 1u;`,
    name => `alias ${name} = u32;`,
    name => `struct ${name} { value: u32, }`,
    name => `struct Values { ${name}: u32, }`,
    name => `fn ${name}() {}`,
    name => `fn helper(${name}: u32) {}`,
    name => `fn main() { var ${name}: u32; }`,
    name => `fn main() { let ${name} = 1u; }`,
    name => `fn main() { const ${name} = 1u; }`,
  ];
  for (const declaration of declarations) {
    for (const name of ['alias', 'shared', 'target', '_', '__hidden']) {
      const code = `// identifier admission\n${declaration(name)}`;
      device.pushErrorScope('validation');
      const module = device.createShaderModule({ code });
      try {
        const info = await module.getCompilationInfo();
        const diagnostic = info.messages.find(message => message.type === 'error');
        assert.ok(diagnostic, `accepted invalid name: ${code}`);
        assert.equal(diagnostic.lineNum, 2);
        assert.equal(diagnostic.linePos, code.lastIndexOf(name) - code.indexOf('\n'));
        console.log(JSON.stringify({ label: 'identifier rejection', code, diagnostic }));
      } finally {
        releaseOwnedResource(module);
        await device.popErrorScope();
      }
    }
  }
}

async function runCase({ label, code, entryPoint = 'main', dispatch = 1, expected, texture }, path) {
  const size = expected.length * Uint32Array.BYTES_PER_ELEMENT;
  const output = device.createBuffer({
    size, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
  });
  const staging = device.createBuffer({
    size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
  });
  const inputTexture = texture ? device.createTexture({
    size: texture === 'gather' ? [2, 2] : [8, 4], format: texture === 'storage' ? 'r32uint' : 'rgba8unorm',
    usage: texture === 'storage' ? GPUTextureUsage.STORAGE_BINDING
      : GPUTextureUsage.TEXTURE_BINDING | (texture === 'gather' ? GPUTextureUsage.COPY_DST : 0),
  }) : null;
  const resources = [];
  try {
    device.queue.writeBuffer(output, 0, new Uint32Array(expected.length).fill(5));
    if (texture === 'gather') device.queue.writeTexture({ texture: inputTexture },
      new Uint8Array([1, 5, 10, 255, 2, 6, 20, 255, 3, 7, 30, 255, 4, 8, 40, 255]),
      { bytesPerRow: 8 }, [2, 2]);
    const module = device.createShaderModule({ code });
    resources.push(module);
    const pipeline = device.createComputePipeline({
      layout: 'auto', compute: { module, entryPoint },
    });
    resources.push(pipeline);
    const layout = pipeline.getBindGroupLayout(texture ? 1 : 0);
    resources.push(layout);
    const bindings = device.createBindGroup({
      layout,
      entries: [{ binding: 0, resource: { buffer: output } }],
    });
    resources.push(bindings);
    const encoder = device.createCommandEncoder();
    const pass = path === 'native' ? encoder.beginComputePass({ label }) : encoder.beginComputePass();
    pass.setPipeline(pipeline);
    pass.setBindGroup(texture ? 1 : 0, bindings);
    if (inputTexture) {
      const view = inputTexture.createView();
      const textureLayout = pipeline.getBindGroupLayout(0);
      const textureEntries = [{ binding: 0, resource: view }];
      if (texture === 'gather') {
        const sampler = device.createSampler();
        resources.push(sampler);
        textureEntries.push({ binding: 1, resource: sampler });
      }
      const textureBindings = device.createBindGroup({ layout: textureLayout, entries: textureEntries });
      resources.push(view, textureLayout, textureBindings);
      releaseOwnedResource(textureLayout);
      pass.setBindGroup(0, textureBindings);
    }
    pass.dispatchWorkgroups(dispatch);
    pass.end();
    const copy = path === 'dispatch-copy' ? encoder : device.createCommandEncoder();
    copy.copyBufferToBuffer(output, 0, staging, 0, size);
    if (path === 'dispatch-copy') {
      device.queue.submit([encoder.finish()]);
    } else if (path === 'multi-buffer') {
      device.queue.submit([encoder.finish(), copy.finish()]);
    } else {
      // A separate submission exercises the former clear/fill substitution path.
      device.queue.submit([encoder.finish()]);
      device.queue.submit([copy.finish()]);
    }
    await staging.mapAsync(GPUMapMode.READ);
    const actual = Array.from(new Uint32Array(staging.getMappedRange().slice(0)));
    staging.unmap();
    console.log(JSON.stringify({
      label, path, sourceSha256: createHash('sha256').update(code).digest('hex'),
      entryPoint, dispatch, actual, expected,
    }));
    assert.deepEqual(actual, expected, label);
  } finally {
    for (const resource of resources.reverse()) releaseOwnedResource(resource);
    inputTexture?.destroy();
    staging.destroy();
    output.destroy();
  }
}

async function runUniformLoopCase() {
  const code = `struct Params { base: u32, stride: u32, count: u32, padding: u32 }
    @group(0) @binding(0) var<uniform> params: Params;
    @group(0) @binding(1) var<storage, read_write> data: array<u32>;
    @compute @workgroup_size(1) fn main() {
      var sum = 0u;
      for (var i = 0u; i < params.count; i++) { sum += params.base * params.stride + i; }
      data[0] = sum; data[1] = params.count;
    }`;
  const uniform = device.createBuffer({
    size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  const output = device.createBuffer({
    size: 8, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
  });
  const staging = device.createBuffer({
    size: 8, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
  });
  const resources = [];
  try {
    const module = device.createShaderModule({ code });
    resources.push(module);
    const pipeline = device.createComputePipeline({
      layout: 'auto', compute: { module, entryPoint: 'main' },
    });
    resources.push(pipeline);
    const layout = pipeline.getBindGroupLayout(0);
    resources.push(layout);
    const group = device.createBindGroup({ layout, entries: [
      { binding: 0, resource: { buffer: uniform } },
      { binding: 1, resource: { buffer: output } },
    ] });
    resources.push(group);
    for (const { input, expected } of [
      { input: [2, 5, 3, 0], expected: [33, 3] },
      { input: [7, 9, 4, 0], expected: [258, 4] },
      { input: [0xffffffff, 2, 2, 0], expected: [0xfffffffd, 2] },
      { input: [7, 9, 0, 0], expected: [0, 0] },
    ]) {
      device.queue.writeBuffer(uniform, 0, new Uint32Array(input));
      const encoder = device.createCommandEncoder();
      const pass = encoder.beginComputePass();
      pass.setPipeline(pipeline);
      pass.setBindGroup(0, group);
      pass.dispatchWorkgroups(1);
      pass.end();
      encoder.copyBufferToBuffer(output, 0, staging, 0, 8);
      device.queue.submit([encoder.finish()]);
      await staging.mapAsync(GPUMapMode.READ);
      const actual = Array.from(new Uint32Array(staging.getMappedRange().slice(0)));
      staging.unmap();
      console.log(JSON.stringify({ label: 'uniform loop inputs', input, actual, expected,
        sourceSha256: createHash('sha256').update(code).digest('hex') }));
      assert.deepEqual(actual, expected, 'changed uniform input on reused pipeline');
    }
  } finally {
    for (const resource of resources.reverse()) releaseOwnedResource(resource);
    uniform.destroy();
    output.destroy();
    staging.destroy();
  }
}

async function runMappedWriteCase() {
  const source = device.createBuffer({
    size: 16, usage: GPUBufferUsage.MAP_WRITE | GPUBufferUsage.COPY_SRC, mappedAtCreation: true,
  });
  const staging = device.createBuffer({
    size: 16, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
  });
  try {
    new Uint32Array(source.getMappedRange()).set([2, 3, 5, 7]);
    source.unmap();
    await source.mapAsync(GPUMapMode.WRITE);
    const mapped = new Uint32Array(source.getMappedRange());
    assert.deepEqual(Array.from(mapped), [2, 3, 5, 7], 'write mapping preserves native contents');
    mapped[1] = 13;
    source.unmap();
    const encoder = device.createCommandEncoder();
    encoder.copyBufferToBuffer(source, 0, staging, 0, 16);
    device.queue.submit([encoder.finish()]);
    await staging.mapAsync(GPUMapMode.READ);
    assert.deepEqual(Array.from(new Uint32Array(staging.getMappedRange())), [2, 13, 5, 7]);
    staging.unmap();
  } finally {
    source.destroy();
    staging.destroy();
  }
}

async function runTextureRenderCase() {
  const rowBytes = 256;
  const source = device.createTexture({
    size: [8, 4], format: 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING,
  });
  const target = device.createTexture({
    size: [1, 1], format: 'rgba32uint',
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
  });
  const staging = device.createBuffer({
    size: rowBytes, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
  });
  const resources = [];
  try {
    const code = `@group(0) @binding(0) var texture: texture_2d<f32>;
      fn getValue() -> vec4u { return vec4u(textureDimensions(texture, 0), 7u, 9u); }
      @vertex fn vertexMain(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
        var x = -1.0; var y = -1.0;
        if (index == 1u) { x = 3.0; }
        if (index == 2u) { y = 3.0; }
        return vec4f(x, y, 0.0, 1.0);
      }
      @fragment fn fragmentMain() -> @location(0) vec4u { return getValue() + vec4u(1u); }`;
    const module = device.createShaderModule({ code });
    resources.push(module);
    const pipeline = device.createRenderPipeline({
      layout: 'auto', vertex: { module, entryPoint: 'vertexMain' },
      fragment: { module, entryPoint: 'fragmentMain', targets: [{ format: 'rgba32uint' }] },
      primitive: { topology: 'triangle-list' },
    });
    resources.push(pipeline);
    const layout = pipeline.getBindGroupLayout(0);
    const sourceView = source.createView();
    const targetView = target.createView();
    const group = device.createBindGroup({
      layout, entries: [{ binding: 0, resource: sourceView }],
    });
    resources.push(layout, sourceView, targetView, group);
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({ colorAttachments: [{
      view: targetView, loadOp: 'clear', storeOp: 'store', clearValue: [0, 0, 0, 0],
    }] });
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, group);
    pass.draw(3);
    pass.end();
    encoder.copyTextureToBuffer({ texture: target }, {
      buffer: staging, bytesPerRow: rowBytes, rowsPerImage: 1,
    }, [1, 1, 1]);
    device.queue.submit([encoder.finish()]);
    await staging.mapAsync(GPUMapMode.READ);
    const actual = Array.from(new Uint32Array(staging.getMappedRange(), 0, 4));
    staging.unmap();
    console.log(JSON.stringify({
      label: 'texture dimensions in rendering',
      sourceSha256: createHash('sha256').update(code).digest('hex'), actual,
    }));
    assert.deepEqual(actual, [9, 5, 8, 10]);
  } finally {
    for (const resource of resources.reverse()) releaseOwnedResource(resource);
    source.destroy();
    target.destroy();
    staging.destroy();
  }
}

async function runUintRenderClearCase() {
  const target = device.createTexture({
    size: [1, 1], format: 'rgba32uint',
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
  });
  const staging = device.createBuffer({
    size: 256, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
  });
  const view = target.createView();
  try {
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({ colorAttachments: [{
      view, loadOp: 'clear', storeOp: 'store', clearValue: [2, 3, 4, 5],
    }] });
    pass.end();
    encoder.copyTextureToBuffer({ texture: target }, {
      buffer: staging, bytesPerRow: 256, rowsPerImage: 1,
    }, [1, 1, 1]);
    device.queue.submit([encoder.finish()]);
    await staging.mapAsync(GPUMapMode.READ);
    const actual = Array.from(new Uint32Array(staging.getMappedRange(), 0, 4));
    staging.unmap();
    console.log(JSON.stringify({ label: 'integer render clear', actual, expected: [2, 3, 4, 5] }));
    assert.deepEqual(actual, [2, 3, 4, 5]);
  } finally {
    releaseOwnedResource(view);
    target.destroy();
    staging.destroy();
  }
}

let failures = 0;
try {
  const checks = [
    { label: 'declaration identifier diagnostics', run: runIdentifierAdmissionCase },
    ...CASES.flatMap(testCase => ['batched', 'native', 'dispatch-copy', 'multi-buffer'].map(path => ({
      label: `${testCase.label} / ${path}`, run: () => runCase(testCase, path),
    }))),
    { label: 'changed uniform loop inputs', run: runUniformLoopCase },
    { label: 'mapped writes use native contents', run: runMappedWriteCase },
    { label: 'integer render clear', run: runUintRenderClearCase },
    { label: 'texture dimensions in rendering', run: runTextureRenderCase },
  ];
  for (const testCase of checks) {
    try {
      await testCase.run();
      console.log(`ok: ${testCase.label}`);
    } catch (error) {
      failures += 1;
      console.error(`FAIL: ${testCase.label}: ${error.stack}`);
    }
  }
} finally {
  device.destroy();
  adapter.destroy?.();
}
assert.equal(failures, 0, 'ordinary native shader semantics');
