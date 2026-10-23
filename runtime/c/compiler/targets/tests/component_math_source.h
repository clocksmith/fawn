/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPONENT_MATH_SOURCE_H
#define DOE_COMPONENT_MATH_SOURCE_H
#include <stdint.h>

static const char component_math_source[] =
    "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
    "fn read()->f32{data[31]+=1u;return bitcast<f32>(data[0]);}"
    "@compute @workgroup_size(1) fn component_math(){"
    "let x=bitcast<f32>(data[0]);let half=2.5f;let other=3.5f;"
    "let minimum=bitcast<i32>(2147483648u);let zero=-0.0f;let tiny=-0x1p-149f;"
    "data[1]=bitcast<u32>(fract(read()));data[2]=bitcast<u32>(floor(x));"
    "data[3]=bitcast<u32>(trunc(x));data[4]=bitcast<u32>(round(half));"
    "data[5]=bitcast<u32>(round(other));data[6]=bitcast<u32>(abs(minimum));"
    "data[7]=bitcast<u32>(sign(x));data[8]=bitcast<u32>(sign(zero));"
    "data[9]=bitcast<u32>(abs(zero));data[10]=bitcast<u32>(abs(read()));"
    "data[11]=bitcast<u32>(fract(tiny));data[12]=bitcast<u32>(sin(zero));"
    "data[13]=bitcast<u32>(cos(zero));data[14]=bitcast<u32>(exp2(other-half));"
    "data[15]=bitcast<u32>(log2(half+other+2.0));data[16]=bitcast<u32>(inverseSqrt(4.0f+x-x));"
    "let negative=-3i;let positive=7i;let unsigned_value=4294967295u;"
    "data[17]=bitcast<u32>(sign(negative));data[18]=bitcast<u32>(sign(positive));"
    "data[19]=abs(unsigned_value);data[20]=bitcast<u32>(radians(half-half));"
    "data[21]=bitcast<u32>(degrees(half-half));}"
    ;
/* Exact, independently written exercised values. Transcendentals use only
 * exact elementary inputs here; no universal accuracy claim follows. */
static const uint32_t component_math_expected[] = {
    0xbe800000u, 0x3f400000u, 0xbf800000u, 0x80000000u, 0x40000000u,
    0x40800000u, 0x80000000u, 0xbf800000u, 0u, 0u, 0x3e800000u,
    0x3f800000u, 0x80000000u, 0x3f800000u, 0x40000000u, 0x40400000u,
    0x3f000000u, 0xffffffffu, 1u, 0xffffffffu, 0u, 0u
};
#endif
