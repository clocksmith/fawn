/* SPDX-License-Identifier: MIT */
#ifndef DOE_UNIFORM_STANDARD_FIXTURE_H
#define DOE_UNIFORM_STANDARD_FIXTURE_H
#include "hlsl_fixture.h"
#include <assert.h>
#include <stdlib.h>

typedef struct uniform_case { const char *name, *source; uint32_t bytes, expected; } uniform_case;
static const uniform_case uniform_cases[] = {
    {"tail4", "@group(0) @binding(0) var<uniform> u:array<u32,1>;"
        "@group(0) @binding(1) var<storage,read_write> out:array<u32,4>;"
        "@compute @workgroup_size(1) fn main(){out[0]=u[0];out[1]=u[i32(out[1])];out[2]=u[out[2]];out[3]=u[out[3]];}", 4, 3},
    {"tail8", "@group(0) @binding(0) var<uniform> u:array<u32,2>;"
        "@group(0) @binding(1) var<storage,read_write> out:array<u32,4>;"
        "@compute @workgroup_size(1) fn main(){out[0]=u[1];out[1]=u[i32(out[1])];out[2]=u[out[2]];out[3]=u[out[3]];}", 8, 5},
    {"tail12", "@group(0) @binding(0) var<uniform> u:array<u32,3>;"
        "@group(0) @binding(1) var<storage,read_write> out:array<u32,5>;"
        "@compute @workgroup_size(1) fn main(){out[0]=u[1];out[1]=u[i32(out[1])];out[2]=u[out[2]];out[3]=u[out[3]];out[4]=u[2];}", 12, 5},
    {"nested", "struct Tiny{a:u32} struct U{values:array<u32,3>,tiny:Tiny,after:u32,"
        "rows:array<array<u32,2>,2>,@align(8) gap:u32,@size(12) reserved:u32,end:u32}"
        "@group(0) @binding(0) var<uniform> u:U;"
        "@group(0) @binding(1) var<storage,read_write> out:array<u32,4>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3u){let s=u;"
        "out[0]=s.values[1]+s.tiny.a+s.after+s.rows[1][1]+s.gap+s.reserved+s.end;"
        "out[1]=u.values[i32(id.x)-1];out[2]=u.rows[1][0];out[3]=s.end;}", 64, 167},
    {"matrix_tail", "struct U{v:array<f32,2>,m:mat2x2f,last:u32}"
        "@group(0) @binding(0) var<uniform> u:U;"
        "@group(0) @binding(1) var<storage,read_write> out:array<u32,4>;"
        "@compute @workgroup_size(1) fn main(){out[0]=u32(u.v[1]+u.m[1][0])+u.last;}", 32, 58}
};
static inline DoeCompilerTextCapacities uniform_capacities(void) {
    DoeCompilerTextCapacities caps = hlsl_capacities();
    caps.stages.module.typed.words = 65536;
    caps.stages.module.typed.target_declarations = 8192;
    return caps;
}
static inline DoeCompilerStatus uniform_compile(void *workspace, size_t bytes, const char *source,
    uint32_t flags, HlslFixture *storage, uint32_t *words, DoeCompilerStageResult *result) {
    DoeCompilerTextCapacities caps = uniform_capacities();
    DoeCompilerHlslOutputs out = hlsl_outputs(storage);
    out.stages.resources.module.words = words;
    out.stages.resources.module.word_capacity = words ? 65536 : 0;
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_AUTO, 0, NULL, 0};
    return doe_compiler_compile_stages(9, &caps.stages, sizeof(caps.stages), workspace, bytes,
        source, strlen(source), &selection, sizeof(selection), flags,
        (flags & DOE_COMPILER_MODULE_MEASURE) ? NULL : &out.stages,
        (flags & DOE_COMPILER_MODULE_MEASURE) ? 0 : sizeof(out.stages), result, sizeof(*result));
}
#endif
