// Exercise the delivery artifact independently of the shipped package default.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { renderWGSL } from '../../../../packages/doe-gpu/examples/browser-compiler/particles.js';

const bytes = readFileSync(process.argv[2]);
const module = new WebAssembly.Module(bytes);
assert.deepEqual(WebAssembly.Module.imports(module), []);
const { exports: wasm } = new WebAssembly.Instance(module);
wasm._initialize();
function compile(source, enabled = true) {
  const input = new TextEncoder().encode(source);
  const pointer = wasm.reserve_source(input.length);
  assert(pointer);
  new Uint8Array(wasm.memory.buffer, pointer, input.length).set(input);
  const output = wasm.compile(input.length, Number(enabled));
  assert(output);
  const result = JSON.parse(new TextDecoder().decode(
    new Uint8Array(wasm.memory.buffer, output, wasm.output_length())));
  wasm.release_job();
  return result;
}
for (const [input, count] of [
  ['fn f(x:u32)->u32{return x/8u+x%4u;}', 2],
  ['const x:u32=32u;fn f()->u32{return x/8u;}', 1],
  ['fn f(x:i32)->i32{return x/8i;}', 0],
  ['fn f(x:vec2u)->vec2u{return x/8u;}', 0],
  ['fn f(x:u32)->u32{return (x)/8u+x/(8u)+x/ /*kept*/ 8u;}', 0],
  ['struct S{x:u32} fn f(s:S)->u32{return s.x/0x80000000u;}', 1],
]) {
  const result = compile(input);
  assert.equal(result.ok, true, JSON.stringify(result));
  assert.equal(result.rewrites, count);
  assert.equal(compile(input, false).wgsl, input);
  assert.equal(compile(result.wgsl, false).ok, true, result.wgsl);
}
for (const source of ['fn alias(){}', 'fn f(x:u32)->u32{return x/0u;}',
  'const bad=3.4028235677973366164e38f;', 'const bad=pow(-2.0,2.0);']) {
  assert.equal(compile(source).ok, false);
  assert.equal(compile(source, false).ok, false);
}
for (const source of ['const n=-3.4028234663852886e+38f;', 'const n=0x1p-149f;',
  'const n=sin(1.0);', 'const n=countOneBits(0xffffffffu);',
  'const n=unpack4xU8(0x12345678u);', ' '.repeat(65536)]) {
  assert.equal(compile(source).ok, true, source);
}
const particleWGSL = readFileSync(new URL(
  '../../../../packages/doe-gpu/examples/browser-compiler/particles.wgsl', import.meta.url), 'utf8');
for (const source of [particleWGSL, renderWGSL]) {
  const result = compile(source);
  assert.equal(result.ok, true, JSON.stringify(result));
  assert.equal(compile(source, false).wgsl, source);
}
for (let i = 0; i < 300; ++i) assert.equal(compile('fn f(x:u32)->u32{return x/8u;}').rewrites, 1);
assert.throws(() => wasm.memory.grow(1024));
assert.equal(wasm.reserve_source(65537), 0);
assert.equal(wasm.output_length(), 0);
console.log('C Wasm compiler: typed rewrites, numeric boundaries, particle shader admission, bounded ABI PASS');
