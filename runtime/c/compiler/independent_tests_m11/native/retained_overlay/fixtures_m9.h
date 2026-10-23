/* SPDX-License-Identifier: MIT; independently frozen M9 fixtures. */
#ifndef NATIVE_FIXTURES_M9_H
#define NATIVE_FIXTURES_M9_H
static const char numeric_source[] = "const a:f32=exp(0f);const b:f32=sqrt(4f);const c:f32=tanh(0f);const d:f32=sqrt(-0f);const e:f32=exp(-200f);const f=f32(exp(-1000.0));const g:f32=0x1p-126*0.5f;const h:f32=exp(-100f);@group(0) @binding(0) var<storage,read_write> out:array<u32>;fn ident(x:f32)->f32{return x;}@compute @workgroup_size(1) fn main(){out[0u]=bitcast<u32>(ident(a));out[1u]=bitcast<u32>(b);out[2u]=bitcast<u32>(c);out[3u]=bitcast<u32>(d);out[4u]=bitcast<u32>(e);out[5u]=bitcast<u32>(f);out[6u]=bitcast<u32>(g);out[7u]=bitcast<u32>(h);}";
/* Only exact elementary results below; exp(-100f) accuracy is parent-owned. */
static const uint32_t numeric_expected[8] = {0x3f800000u,0x40000000u,0u,0x80000000u,0u,0u,0x00400000u,0x3f800000u};
static const char rejected_numeric_source[] = "const x=exp(1000.0);@compute @workgroup_size(1) fn main(){}";
static const char rejected_parse_source[] = "@compute @workgroup_size(1) fn main(){let x=;}";
static const char retained_source[] = "@group(0) @binding(0) var<storage,read_write> out:array<u32>;@compute @workgroup_size(1) fn main(){out[0u]=1u;}";
#endif
