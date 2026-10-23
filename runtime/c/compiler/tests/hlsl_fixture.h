/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_HLSL_FIXTURE_H
#define DOE_COMPILER_HLSL_FIXTURE_H
#include "../include/doe_compiler_hlsl.h"
#include <string.h>

static const char hlsl_source[] =
    "enable subgroups;"
    "@group(3) @binding(5) var<storage,read_write> output:array<u32>;"
    "@group(2) @binding(9) var<storage,read> input:array<u32>;"
    "@group(1) @binding(2) var<uniform> params:vec4u;"
    "@group(0) @binding(4) var tex:texture_2d<f32>;"
    "@group(0) @binding(7) var smp:sampler;"
    "@group(2) @binding(10) var image:texture_storage_2d<r32uint,read>;"
    "@group(3) @binding(11) var<storage,read> unused:array<u32>;"
    "@compute @workgroup_size(1) fn first(){output[0]=9u;}"
    "@compute @workgroup_size(4) fn selected(@builtin(num_workgroups) groups:vec3u){"
    "output[0]=input[0]+params.x+groups.x+subgroupAdd(1u)+"
    "u32(textureSampleLevel(tex,smp,vec2f(0.5f),0.0f).x)+textureLoad(image,vec2i(0)).x;}"
    "@compute @workgroup_size(1) fn last(){}";
typedef struct HlslFixture {
    char text[65536], names[2048];
    DoeCompilerTypeV4 types[128];
    DoeCompilerMemberV4 members[64];
    DoeCompilerResourceV4 resources[16];
    DoeCompilerWorkgroupV5 workgroups[8];
    DoeCompilerEntryV5 entries[8];
    uint32_t references[64];
    DoeCompilerHandleV6 handles[16];
    DoeCompilerStageInterfaceV7 interfaces[64];
    DoeCompilerHlslBindingV11 bindings[16];
    DoeCompilerHlslEntryV11 target_entries[8];
} HlslFixture;
static inline DoeCompilerTextCapacities hlsl_capacities(void) {
    DoeCompilerTextCapacities caps;
    memset(&caps, 0, sizeof(caps));
    DoeCompilerTypedCapacities *typed = &caps.stages.module.typed;
    typed->source_bytes = 8192;
    typed->tokens = 2048;
    typed->statements = 128;
    typed->expressions = 1024;
    typed->types = 128;
    typed->members = 64;
    typed->resources = 16;
    typed->access_steps = 256;
    typed->constants = 256;
    typed->constant_edges = 512;
    typed->ir_instructions = 2048;
    typed->ir_operands = 2048;
    typed->traversal_items = 128;
    typed->expression_depth = 32;
    typed->control_depth = 8;
    typed->type_syntax_depth = 32;
    caps.stages.module.entries = 8;
    caps.stages.module.entry_inputs = 32;
    caps.stages.module.entry_references = 64;
    caps.stages.interface_attributes = 64;
    caps.stages.stage_interfaces = 64;
    caps.text_bytes = 65536;
    return caps;
}
static inline DoeCompilerHlslOutputs hlsl_outputs(HlslFixture *storage) {
    DoeCompilerHlslOutputs out;
    memset(&out, 0, sizeof(out));
    DoeCompilerModuleOutputs *module = &out.stages.resources.module;
    module->type_record_size = sizeof(DoeCompilerTypeV4);
    module->member_record_size = sizeof(DoeCompilerMemberV4);
    module->resource_record_size = sizeof(DoeCompilerResourceV4);
    module->workgroup_record_size = sizeof(DoeCompilerWorkgroupV5);
    module->entry_record_size = sizeof(DoeCompilerEntryV5);
    out.stages.resources.handle_record_size = sizeof(DoeCompilerHandleV6);
    out.stages.interface_record_size = sizeof(DoeCompilerStageInterfaceV7);
    out.binding_record_size = sizeof(DoeCompilerHlslBindingV11);
    out.entry_record_size = sizeof(DoeCompilerHlslEntryV11);
    if (storage == NULL) return out;
    module->types = storage->types;
    module->type_capacity = 128;
    module->members = storage->members;
    module->member_capacity = 64;
    module->resources = storage->resources;
    module->resource_capacity = 16;
    module->workgroups = storage->workgroups;
    module->workgroup_capacity = 8;
    module->entries = storage->entries;
    module->entry_capacity = 8;
    module->entry_references = storage->references;
    module->entry_reference_capacity = 64;
    module->names = storage->names;
    module->name_capacity = sizeof(storage->names);
    out.stages.resources.handles = storage->handles;
    out.stages.resources.handle_capacity = 16;
    out.stages.interfaces = storage->interfaces;
    out.stages.interface_capacity = 64;
    out.text = storage->text;
    out.text_capacity = sizeof(storage->text);
    out.bindings = storage->bindings;
    out.binding_capacity = 16;
    out.entries = storage->target_entries;
    out.entry_capacity = 8;
    return out;
}
#endif
