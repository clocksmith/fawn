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
/* Shared-callee branching is source syntax, not a flattened generated fixture. */
#include <stdio.h>
static inline bool target_dag_source(char *buffer, size_t capacity) {
    int count = snprintf(buffer, capacity,
        "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "fn f0(){data[0]=7u;}");
    if (count < 0 || (size_t)count >= capacity) return false;
    size_t used = (size_t)count;
    for (unsigned i = 1u; i <= 24u; ++i) {
        count = snprintf(buffer + used, capacity - used,
            "fn f%u(){f%u();f%u();}", i, i - 1u, i - 1u);
        if (count < 0 || (size_t)count >= capacity - used) return false;
        used += (size_t)count;
    }
    count = snprintf(buffer + used, capacity - used,
        "fn unrelated(){} @compute @workgroup_size(1) fn main(){f24();}");
    return count >= 0 && (size_t)count < capacity - used;
}
#endif
