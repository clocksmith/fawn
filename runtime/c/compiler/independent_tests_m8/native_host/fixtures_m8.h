/* SPDX-License-Identifier: MIT
 * Independently frozen before M8 candidate inspection, 2026-10-23. */
#ifndef NATIVE_FIXTURES_M8_H
#define NATIVE_FIXTURES_M8_H
static const char numeric_source[] =
    "const a:f32=1.000000059604644775390625;"
    "const b:f32=1.000000178813934326171875;"
    "const c:f32=0x1p-149;const d:f32=-0.0;"
    "const e=(1.0+0x1p-52)-1.0;"
    "const f:f32=(16777216f+1f)-16777216f;"
    "const g:f32=0x1p-126*0.5f;const h:f32=1f/10f;"
    "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
    "@compute @workgroup_size(1) fn main(){"
    "out[0u]=bitcast<u32>(a);out[1u]=bitcast<u32>(b);"
    "out[2u]=bitcast<u32>(c);out[3u]=bitcast<u32>(d);"
    "out[4u]=bitcast<u32>(f32(e));out[5u]=bitcast<u32>(f);"
    "out[6u]=bitcast<u32>(g);out[7u]=bitcast<u32>(h);}";
/* Mathematical expected final output payloads. Parent instruction oracle owns
 * execution; native suite also requires each payload in emitted OpConstant. */
static const uint32_t numeric_expected[8] = {
    UINT32_C(0x3f800000), UINT32_C(0x3f800002), UINT32_C(1),
    UINT32_C(0x80000000), UINT32_C(0x25800000), UINT32_C(0),
    UINT32_C(0x00400000), UINT32_C(0x3dcccccd)
};
static const char rejected_numeric_source[] =
    "const x=1.0/0.0;@compute @workgroup_size(1) fn main(){}";
static const char rejected_parse_source[] =
    "@compute @workgroup_size(1) fn main(){let x=;}";
static const char retained_source[] =
    "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
    "@compute @workgroup_size(1) fn main(){out[0u]=1u;}";
#endif
