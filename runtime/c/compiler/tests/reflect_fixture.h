/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_REFLECT_FIXTURE_H
#define DOE_COMPILER_REFLECT_FIXTURE_H
#include <stdint.h>

static const char reflect_source[] =
    "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
    "fn incident(slot:u32)->vec3f{out[slot]=out[slot]*10u+1u;return vec3f(1,-2,3);}"
    "fn normal(slot:u32)->vec3f{out[slot]=out[slot]*10u+2u;return vec3f(0,1,0);}"
    "fn change()->vec3f{out[8]=bitcast<u32>(9.0f);out[9]=bitcast<u32>(9.0f);"
    "out[10]=bitcast<u32>(9.0f);out[11]=bitcast<u32>(9.0f);return vec3f(0,1,0);}"
    "@compute @workgroup_size(1) fn main(){out[3]=0u;"
    "let r=reflect(incident(3u),normal(3u));"
    "out[0]=bitcast<u32>(r.x);out[1]=bitcast<u32>(r.y);out[2]=bitcast<u32>(r.z);"
    "out[7]=0u;let s=reflect(normal(7u),incident(7u));"
    "out[4]=bitcast<u32>(s.x);out[5]=bitcast<u32>(s.y);out[6]=bitcast<u32>(s.z);"
    "out[8]=bitcast<u32>(1.0f);out[9]=bitcast<u32>(-2.0f);out[10]=bitcast<u32>(3.0f);"
    "let saved=reflect(vec3f(bitcast<f32>(out[8]),bitcast<f32>(out[9]),bitcast<f32>(out[10])),change());"
    "out[8]=bitcast<u32>(saved.x);out[9]=bitcast<u32>(saved.y);out[10]=bitcast<u32>(saved.z);"
    "var surface=vec3f(0,2,0);let wide=reflect(vec3f(1,-2,3),surface);"
    "out[12]=bitcast<u32>(wide.x);out[13]=bitcast<u32>(wide.y);out[14]=bitcast<u32>(wide.z);"
    "const small=reflect(vec2(1,-2),vec2(0,1));out[15]=bitcast<u32>(f32(small.y));"
    "const four=reflect(vec4(1,-2,3,-4),vec4(0,1,0,0));"
    "out[16]=bitcast<u32>(f32(four.x));out[17]=bitcast<u32>(f32(four.y));"
    "out[18]=bitcast<u32>(f32(four.z));out[19]=bitcast<u32>(f32(four.w));}";

/* Independent exact binary32 values; neither a reflection formula nor target
 * instruction interpreter computes this oracle. */
static const uint32_t reflect_expected[] = {
    0x3f800000,0x40000000,0x40400000,12,
    0x40800000,0xc0e00000,0x41400000,21,
    0x3f800000,0x40000000,0x40400000,0x41100000,
    0x3f800000,0x41600000,0x40400000,0x40000000,
    0x3f800000,0x40000000,0x40400000,0xc0800000
};
#endif
