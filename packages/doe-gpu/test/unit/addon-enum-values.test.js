// Every numeric name-to-value table in the native addon must use the pinned
// webgpu.h values. Provider-specific numbering (the Zig runtime's) is a
// translation at the provider boundary, never the table's own value.
import assert from 'node:assert/strict';
import { readdir, readFile } from 'node:fs/promises';

const ADDON = new URL('../../../../runtime/bridge/webgpu-addon/', import.meta.url);
const HEADER = new URL('../../../../runtime/zig/vendor/webgpu-headers/webgpu.h', import.meta.url);

// Converter-name fragment -> webgpu.h enum. A new numeric table must be listed.
const FAMILIES = [
  ['texture_format', 'WGPUTextureFormat'],
  ['primitive_topology', 'WGPUPrimitiveTopology'],
  ['front_face', 'WGPUFrontFace'],
  ['cull_mode', 'WGPUCullMode'],
  ['filter_mode', 'WGPUFilterMode'],
  ['address_mode', 'WGPUAddressMode'],
  ['compare_func', 'WGPUCompareFunction'],
  ['vertex_step_mode', 'WGPUVertexStepMode'],
  ['index_format', 'WGPUIndexFormat'],
  ['blend_factor', 'WGPUBlendFactor'],
  ['blend_operation', 'WGPUBlendOperation'],
  ['buffer_binding_type', 'WGPUBufferBindingType'],
  ['sampler_binding_type', 'WGPUSamplerBindingType'],
  ['sample_type', 'WGPUTextureSampleType'],
  ['view_dimension', 'WGPUTextureViewDimension'],
  ['storage_texture_access', 'WGPUStorageTextureAccess'],
  ['load_op', 'WGPULoadOp'],
  ['store_op', 'WGPUStoreOp'],
  ['stencil_operation', 'WGPUStencilOperation'],
];

const header = await readFile(HEADER, 'utf8');
const enums = new Map();
for (const match of header.matchAll(/\b(WGPU[A-Za-z0-9]+)_(\w+) = (0x[0-9A-Fa-f]+)/g)) {
  if (!enums.has(match[1])) enums.set(match[1], new Map());
  enums.get(match[1]).set(match[2].toLowerCase(), Number(match[3]));
}

const checked = [];
for (const file of (await readdir(ADDON)).filter((name) => name.endsWith('.c')).sort()) {
  const source = await readFile(new URL(file, ADDON), 'utf8');
  for (const fn of source.matchAll(/\n(?:static )?uint32_t (\w+)\([^)]*\) \{([\s\S]*?)\n\}/g)) {
    const [, name, body] = fn;
    const pairs = [...body.matchAll(/"([a-z0-9_-]+)"\)\s*==\s*0\)?(?:\s*\|\|[^)]*\)\s*==\s*0\))*\s*return\s*(0x[0-9A-Fa-f]+|\d+)u?;/g)];
    if (pairs.length === 0) continue;
    const family = FAMILIES.find(([fragment]) => name.includes(fragment));
    assert(family, `${file}:${name} returns numeric enum values; register its webgpu.h enum in FAMILIES`);
    const values = enums.get(family[1]);
    assert(values, `${family[1]} is missing from webgpu.h`);
    for (const [, text, literal] of pairs) {
      const expected = values.get(text.replace(/[-_]/g, ''));
      assert.equal(Number(literal), expected, `${file}:${name} maps "${text}" to ${literal}; webgpu.h ${family[1]} is ${expected}`);
    }
    checked.push(name);
  }
}
assert(checked.includes('texture_format_from_name'), 'texture format table was not found');
console.log(`ok: ${checked.length} addon enum tables match webgpu.h`);
