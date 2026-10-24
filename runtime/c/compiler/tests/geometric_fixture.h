/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_GEOMETRIC_FIXTURE_H
#define DOE_COMPILER_GEOMETRIC_FIXTURE_H
#include <stdint.h>

static const char geometric_source[] =
    "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
    "fn ray(slot:u32)->vec3f{out[slot]=out[slot]*10u+1u;return vec3f(4,1,2);}"
    "fn normal(slot:u32)->vec3f{out[slot]=out[slot]*10u+2u;return vec3f(0,1,0);}"
    "fn ratio(slot:u32)->f32{out[slot]=out[slot]*10u+3u;return 0.5;}"
    "fn change()->f32{out[8]=bitcast<u32>(9.0f);out[9]=bitcast<u32>(9.0f);"
    "out[10]=bitcast<u32>(9.0f);out[11]=bitcast<u32>(9.0f);return 0.5;}"
    "@compute @workgroup_size(1) fn main(){out[3]=0u;"
    "let r=refract(ray(3u),normal(3u),ratio(3u));"
    "out[0]=bitcast<u32>(r.x);out[1]=bitcast<u32>(r.y);out[2]=bitcast<u32>(r.z);"
    "out[7]=0u;let f=faceForward(ray(7u),normal(7u),ray(7u));"
    "out[4]=bitcast<u32>(f.x);out[5]=bitcast<u32>(f.y);out[6]=bitcast<u32>(f.z);"
    "out[8]=bitcast<u32>(4.0f);out[9]=bitcast<u32>(1.0f);out[10]=bitcast<u32>(2.0f);"
    "let saved=refract(vec3f(bitcast<f32>(out[8]),bitcast<f32>(out[9]),bitcast<f32>(out[10])),vec3f(0,1,0),change());"
    "out[8]=bitcast<u32>(saved.x);out[9]=bitcast<u32>(saved.y);out[10]=bitcast<u32>(saved.z);"
    "var eta=2.0f;let tir=refract(vec3f(1,0,0),vec3f(0,1,0),eta);"
    "out[12]=bitcast<u32>(tir.x);out[13]=bitcast<u32>(tir.y);out[14]=bitcast<u32>(tir.z);"
    "var perpendicular=vec3f(1,0,0);let zero=faceForward(vec3f(1,-2,3),perpendicular,vec3f(0,1,0));"
    "out[15]=bitcast<u32>(zero.x);out[16]=bitcast<u32>(zero.y);out[17]=bitcast<u32>(zero.z);"
    "var wide_normal=vec3f(0,2,0);let wide=refract(vec3f(0,0.5,0),wide_normal,0.5f);"
    "out[18]=bitcast<u32>(wide.x);out[19]=bitcast<u32>(wide.y);out[20]=bitcast<u32>(wide.z);"
    "const small=refract(vec2(4,1),vec2(0,1),0.5);"
    "out[21]=bitcast<u32>(f32(small.x));out[22]=bitcast<u32>(f32(small.y));"
    "const four=faceForward(vec4(1,-2,3,-4),vec4(0,1,0,0),vec4(0,-2,0,0));"
    "out[23]=bitcast<u32>(f32(four.x));out[24]=bitcast<u32>(f32(four.y));"
    "out[25]=bitcast<u32>(f32(four.z));out[26]=bitcast<u32>(f32(four.w));"
    "const rejected=refract(vec3(1,0,0),vec3(0,1,0),2);"
    "out[27]=bitcast<u32>(f32(rejected.x));out[28]=bitcast<u32>(f32(rejected.y));out[29]=bitcast<u32>(f32(rejected.z));"
    "var negative=-1.0f;let reverse=refract(vec3f(3,-4,1),vec3f(0,1,0),negative);"
    "out[30]=bitcast<u32>(reverse.x);out[31]=bitcast<u32>(reverse.y);out[32]=bitcast<u32>(reverse.z);"
    "var unit=vec3f(0,1,0);let swapped=refract(unit,vec3f(4,1,2),0.5f);"
    "out[33]=bitcast<u32>(swapped.x);out[34]=bitcast<u32>(swapped.y);out[35]=bitcast<u32>(swapped.z);"
    "var opposite=vec3f(0,-2,0);let face=faceForward(vec3f(1,-2,3),vec3f(0,1,0),opposite);"
    "out[36]=bitcast<u32>(face.x);out[37]=bitcast<u32>(face.y);out[38]=bitcast<u32>(face.z);}";

/* Fixed independent binary32 values and observable effect receipts. No shader
 * formula or instruction interpreter produces the expected values. */
static const uint32_t geometric_expected[] = {
    0x40000000,0xbf800000,0x3f800000,123,
    0xc0800000,0xbf800000,0xc0000000,121,
    0x40000000,0xbf800000,0x3f800000,0x41100000,
    0,0,0,0xbf800000,0x40000000,0xc0400000,
    0,0xc0300000,0,0x40000000,0xbf800000,
    0x3f800000,0xc0000000,0x40400000,0xc0800000,
    0,0,0,0xc0400000,0xc0800000,0xbf800000,
    0xc0c00000,0xbf800000,0xc0400000,
    0x3f800000,0xc0000000,0x40400000
};
#endif
