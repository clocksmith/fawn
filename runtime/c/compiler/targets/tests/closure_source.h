/* SPDX-License-Identifier: MIT */
#ifndef DOE_TARGET_CLOSURE_SOURCE_H
#define DOE_TARGET_CLOSURE_SOURCE_H
static const char target_closure_source[] =
    "struct Pair{key:u32,value:f32,}"
    "@group(0) @binding(0) var<storage,read_write> a:array<u32>;"
    "@group(0) @binding(1) var<storage,read> b:array<Pair>;"
    "@group(0) @binding(2) var<storage,read_write> c:array<f32>;"
    "fn write(){a[0]=7u;}"
    "fn read()->f32{let p=b[0];return p.value;}"
    "@compute @workgroup_size(1) fn alpha(){write();}"
    "@compute @workgroup_size(1) fn beta(){c[0]=read();}";
#endif
