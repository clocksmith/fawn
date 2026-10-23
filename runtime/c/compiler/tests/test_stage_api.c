/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_stages.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DoeCompilerStageCapacities capacities = {
    .module = {.typed = {.source_bytes = 8192u, .tokens = 2048u, .statements = 64u, .expressions = 512u,
        .types = 64u, .members = 32u, .resources = 8u, .access_steps = 128u, .constants = 128u,
        .constant_edges = 256u, .ir_instructions = 1024u, .ir_operands = 1024u,
        .target_declarations = 1024u, .traversal_items = 128u, .words = 8192u,
        .expression_depth = 32u, .control_depth = 8u, .type_syntax_depth = 32u},
        .entries = 4u, .entry_inputs = 16u, .entry_references = 32u},
    .interface_attributes = 32u, .stage_interfaces = 32u
};
static const char source[] =
    "struct O{@builtin(position) p:vec4f,@location(2) color:vec4f,@location(3) @interpolate(flat) lane:u32}"
    "@group(0) @binding(0) var<storage,read> data:array<f32>;"
    "@group(0) @binding(1) var tex:texture_2d<f32>;"
    "@vertex fn vertex(@location(0) p:vec4f,@builtin(vertex_index) i:u32)->O{return O(p,vec4f(data[i]),i);}"
    "@fragment fn fragment(input:O)->@location(0) vec4f{let skipped=false&&(textureDimensions(tex).x>0u);return input.color;}"
    "@compute @workgroup_size(1) fn compute(){}";
static DoeCompilerStatus compile(const DoeCompilerStageCapacities *caps, void *workspace, size_t bytes,
    const char *text, uint32_t flags, const DoeCompilerStageOutputs *outputs, DoeCompilerStageResult *result) {
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NAME, 0u, "fragment", 8u};
    return doe_compiler_compile_stages(7u, caps, sizeof(*caps), workspace, bytes, text, strlen(text),
        &selection, sizeof(selection), flags, outputs, outputs == NULL ? 0u : sizeof(*outputs), result, sizeof(*result));
}
int main(void) {
    DoeCompilerWorkspaceRequirements requirements, old;
    assert(doe_compiler_stage_requirements(7u, &capacities, sizeof(capacities), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    assert(doe_compiler_resource_requirements(6u, &capacities.module, sizeof(capacities.module), &old, sizeof(old)) == DOE_COMPILER_OK);
    assert(requirements.size_bytes > old.size_bytes && requirements.alignment_bytes == old.alignment_bytes);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    uint32_t words[8192], references[32];
    DoeCompilerTypeV4 types[64]; DoeCompilerMemberV4 members[32]; DoeCompilerResourceV4 resources[8];
    DoeCompilerWorkgroupV5 workgroups[4]; DoeCompilerEntryV5 entries[4]; DoeCompilerHandleV6 handles[8];
    DoeCompilerStageInterfaceV7 interfaces[32]; char names[2048];
    DoeCompilerStageOutputs outputs = {.resources = {.module = {
        .words = words, .word_capacity = 8192u,
        .types = types, .type_capacity = 64u, .type_record_size = sizeof(types[0]),
        .members = members, .member_capacity = 32u, .member_record_size = sizeof(members[0]),
        .resources = resources, .resource_capacity = 8u, .resource_record_size = sizeof(resources[0]),
        .workgroups = workgroups, .workgroup_capacity = 4u, .workgroup_record_size = sizeof(workgroups[0]),
        .entries = entries, .entry_capacity = 4u, .entry_record_size = sizeof(entries[0]),
        .entry_references = references, .entry_reference_capacity = 32u, .names = names, .name_capacity = sizeof(names)},
        .handles = handles, .handle_capacity = 8u, .handle_record_size = sizeof(handles[0])},
        .interfaces = interfaces, .interface_capacity = 32u, .interface_record_size = sizeof(interfaces[0])};
    DoeCompilerStageResult result, measured;
    errno = EDOM;
    assert(fesetround(FE_DOWNWARD) == 0);
    assert(compile(&capacities, workspace, requirements.size_bytes, source, 7u, NULL, &measured) == DOE_COMPILER_OK);
    assert(errno == EDOM && fegetround() == FE_DOWNWARD);
    assert(measured.required_interfaces == 9u && measured.published_interfaces == 0u);
    assert(measured.resources.required_handles == 1u && measured.resources.module.required_is_exact == 1u);
    assert(compile(&capacities, workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.resources.module.module.schema_version == 7u && result.resources.module.module.selected_entry == 1u);
    assert(result.published_interfaces == 9u && result.resources.published_handles == 1u);
    assert(memcmp(&result.resources.module.required, &measured.resources.module.required, sizeof(measured.resources.module.required)) == 0);
    assert(entries[0].stage == 2u && entries[1].stage == 3u && entries[2].stage == 1u);
    assert(entries[0].workgroup_size[0] == 0u && entries[1].declared_builtin_mask == 0u && entries[2].workgroup_size[0] == 1u);
    assert(entries[0].resource_reference_count == 1u && references[entries[0].first_resource_reference] == 0u);
    assert(entries[1].resource_reference_count == 1u && references[entries[1].first_resource_reference] == 1u);
    assert(interfaces[0].direction == 1u && interfaces[0].location == 0u && interfaces[0].lane_count == 4u);
    assert(interfaces[1].builtin == DOE_COMPILER_BUILTIN_VERTEX_INDEX && interfaces[1].location == UINT32_MAX);
    assert(interfaces[2].direction == 2u && interfaces[2].builtin == DOE_COMPILER_BUILTIN_POSITION);
    assert(interfaces[4].interpolation == DOE_COMPILER_INTERPOLATION_FLAT && interfaces[4].sampling == DOE_COMPILER_SAMPLING_FIRST);
    assert(interfaces[5].entry_index == 1u && interfaces[5].builtin == DOE_COMPILER_BUILTIN_POSITION);
    for (size_t i = 0u; i < result.published_interfaces; ++i) {
        assert(interfaces[i].reserved == 0u);
        assert((size_t)interfaces[i].name.offset + interfaces[i].name.length < result.resources.module.published.name_bytes);
        assert(names[interfaces[i].name.offset + interfaces[i].name.length] == '\0');
    }
    memset(words, 0xa5, sizeof(words)); memset(interfaces, 0xa5, sizeof(interfaces)); memset(names, 0xa5, sizeof(names));
    outputs.interface_capacity = 8u;
    assert(compile(&capacities, workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.required_interfaces == 9u && result.published_interfaces == 0u && result.resources.module.published.words == 0u);
    assert(words[0] == UINT32_C(0xa5a5a5a5) && interfaces[0].location == UINT32_C(0xa5a5a5a5) && (unsigned char)names[0] == 0xa5u);
    outputs.interfaces = NULL; outputs.interface_capacity = 0u;
    assert(compile(&capacities, workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    outputs.interfaces = interfaces; outputs.interface_capacity = 32u;
    outputs.interface_record_size = sizeof(interfaces[0]) - 1u;
    memset(&result, 0xa5, sizeof(result)); DoeCompilerStageResult unchanged = result;
    assert(compile(&capacities, workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_ABI_MISMATCH);
    assert(memcmp(&unchanged, &result, sizeof(result)) == 0);
    outputs.interface_record_size = sizeof(interfaces[0]); outputs.interface_capacity = SIZE_MAX;
    assert(compile(&capacities, workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_INPUT_ERROR);
    outputs.interface_capacity = 32u;
    DoeCompilerStageCapacities short_caps = capacities;
    short_caps.interface_attributes = 0u;
    assert(compile(&short_caps, workspace, requirements.size_bytes, source, 7u, NULL, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(result.resources.module.exhausted_pool == DOE_COMPILER_POOL_INTERFACE_ATTRIBUTES && result.resources.module.minimum_pool_capacity == 1u);
    short_caps = capacities; short_caps.stage_interfaces = 1u;
    assert(compile(&short_caps, workspace, requirements.size_bytes, source, 7u, NULL, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(result.resources.module.exhausted_pool == DOE_COMPILER_POOL_STAGE_INTERFACES && result.resources.module.minimum_pool_capacity == 2u);
    assert(compile(&capacities, workspace, requirements.size_bytes, "@vertex fn fragment(){}", 7u, NULL, &result) == DOE_COMPILER_SEMANTIC_ERROR);
    assert(compile(&capacities, workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NONE, 0u, NULL, 0u};
    DoeCompilerResourceResult old_result;
    assert(doe_compiler_compile_resources(6u, &capacities.module, sizeof(capacities.module), workspace, requirements.size_bytes,
        source, strlen(source), &selection, sizeof(selection), 5u, NULL, 0u, &old_result, sizeof(old_result)) == DOE_COMPILER_UNSUPPORTED);
    assert(compile(&capacities, workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    outputs.interfaces = NULL; outputs.interface_capacity = 0u;
    const char compute[] = "@compute @workgroup_size(1) fn fragment(){}";
    assert(compile(&capacities, workspace, requirements.size_bytes, compute, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.required_interfaces == 0u && result.published_interfaces == 0u);
    assert(fesetround(FE_TONEAREST) == 0);
    free(workspace);
    puts("ABI7 exact interfaces, stage/resource identity, unchanged old layout, atomic failure and reuse passed");
    return 0;
}
