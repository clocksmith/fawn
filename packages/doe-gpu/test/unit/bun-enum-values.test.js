// The Bun FFI backend writes webgpu.h structs itself, so its name-to-value
// tables must hold the pinned webgpu.h values, translated where the Zig
// drop-in (the only provider bun-ffi loads) numbers an enum differently:
// the RGBA16/RGBA32 texture formats and stencil operations (see
// g_zig_enum_order in the addon).
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const BUN_FFI = new URL('../../src/vendor/webgpu/bun-ffi.js', import.meta.url);
const HEADER = new URL('../../../../runtime/zig/vendor/webgpu-headers/webgpu.h', import.meta.url);

const zigTextureFormat = (value) => {
  if (value < 0x24 || value > 0x2B) return value;
  return value <= 0x25 ? value + 6 : value - 2;
};
const zigStencilOperation = (value) => value - 1;

// Table -> [webgpu.h enum, provider translation].
const TABLES = [
  ['VERTEX_FORMAT_MAP', 'WGPUVertexFormat'],
  ['VERTEX_STEP_MODE_MAP', 'WGPUVertexStepMode'],
  ['INDEX_FORMAT_MAP', 'WGPUIndexFormat'],
  ['FILTER_MODE_MAP', 'WGPUFilterMode'],
  ['ADDRESS_MODE_MAP', 'WGPUAddressMode'],
  ['COMPARE_FUNC_MAP', 'WGPUCompareFunction'],
  ['TEXTURE_ASPECT_MAP', 'WGPUTextureAspect'],
  ['LOAD_OP_CODES', 'WGPULoadOp'],
  ['STORE_OP_CODES', 'WGPUStoreOp'],
  ['STENCIL_OPERATION_MAP', 'WGPUStencilOperation', zigStencilOperation],
  ['TEXTURE_FORMAT_MAP', 'WGPUTextureFormat', zigTextureFormat],
];

const normalize = (name) => name.replace(/[-_]/g, '').toLowerCase();
const header = await readFile(HEADER, 'utf8');
const enums = new Map();
for (const match of header.matchAll(/\b(WGPU[A-Za-z0-9]+)_(\w+) = (0x[0-9A-Fa-f]+)/g)) {
  if (!enums.has(match[1])) enums.set(match[1], new Map());
  enums.get(match[1]).set(normalize(match[2]), Number(match[3]));
}
const source = await readFile(BUN_FFI, 'utf8');

for (const [table, enumName, translate = (value) => value] of TABLES) {
  const body = new RegExp(`const ${table} = (?:Object\\.freeze\\()?\\{([\\s\\S]*?)\\}`).exec(source)?.[1];
  assert(body, `bun-ffi.js defines ${table}`);
  const entries = [...body.matchAll(/["']?([\w-]+)["']?\s*:\s*(0x[0-9A-Fa-f]+|\d+)/g)];
  assert(entries.length > 0, `${table} has entries`);
  const values = enums.get(enumName);
  for (const [, name, value] of entries) {
    const standard = values.get(normalize(name));
    assert.notEqual(standard, undefined, `${table}.${name} names a ${enumName} value`);
    assert.equal(Number(value), translate(standard), `${table}.${name} must be ${translate(standard)} (${enumName})`);
  }
}
console.log(`ok: ${TABLES.length} bun-ffi enum tables match webgpu.h`);
