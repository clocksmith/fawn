/* SPDX-License-Identifier: MIT */
#ifndef DOE_METAL12_FIXTURE_H
#define DOE_METAL12_FIXTURE_H
#include "hlsl_fixture.h"
#include "../include/doe_compiler_metal.h"

typedef struct Metal12Fixture {
    HlslFixture results;
    DoeCompilerMetalBindingV12 bindings[512];
    DoeCompilerMetalEntryV12 entries[8];
} Metal12Fixture;
static const char metal12_buffers[] =
    "@group(2) @binding(999) var<storage,read> prefix:array<u32>;"
    "@group(3) @binding(999) var<storage,read_write> results:array<u32>;"
    "fn length()->u32{return arrayLength(&results);}"
    "@compute @workgroup_size(1) fn first(){results[0]=prefix[0]+length();}"
    "@compute @workgroup_size(1) fn second(){results[0]=length();}";
static const char metal12_mixed[] =
    "@group(2) @binding(999) var<storage,read> prefix:array<u32>;"
    "@group(3) @binding(999) var<storage,read_write> results:array<u32>;"
    "@group(3) @binding(998) var tex:texture_2d<f32>;"
    "@group(1) @binding(999) var smp:sampler;"
    "fn length()->u32{return arrayLength(&results);}"
    "@compute @workgroup_size(1) fn first(){results[0]=prefix[0]+length()+u32(textureSampleLevel(tex,smp,vec2f(0.5),0.0).x);}"
    "@compute @workgroup_size(1) fn second(){results[0]=length();}";
static inline DoeCompilerTextCapacities metal12_capacities(void) {
    DoeCompilerTextCapacities caps = hlsl_capacities();
    caps.stages.module.typed.source_bytes = 65536;
    caps.stages.module.typed.tokens = 8192;
    caps.stages.module.typed.statements = 512;
    caps.stages.module.typed.expressions = 4096;
    caps.stages.module.typed.resources = 128;
    caps.stages.module.typed.constants = 1024;
    caps.stages.module.typed.constant_edges = 2048;
    caps.stages.module.typed.ir_instructions = 8192;
    caps.stages.module.typed.ir_operands = 8192;
    caps.stages.module.entry_references = 512;
    return caps;
}
static inline DoeCompilerMetalOutputs metal12_outputs(Metal12Fixture *fixture) {
    DoeCompilerHlslOutputs base = hlsl_outputs(fixture ? &fixture->results : NULL);
    DoeCompilerMetalOutputs out;
    memset(&out, 0, sizeof(out));
    out.stages = base.stages;
    out.text = base.text;
    out.text_capacity = base.text_capacity;
    out.binding_record_size = sizeof(DoeCompilerMetalBindingV12);
    out.entry_record_size = sizeof(DoeCompilerMetalEntryV12);
    if (fixture) {
        out.bindings = fixture->bindings;
        out.binding_capacity = 512;
        out.entries = fixture->entries;
        out.entry_capacity = 8;
    }
    return out;
}
#endif
