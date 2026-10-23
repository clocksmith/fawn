/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_resources.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DoeCompilerModuleCapacities capacities = {
    .typed = {.source_bytes = 8192u, .tokens = 2048u, .statements = 64u, .expressions = 512u,
        .types = 64u, .members = 32u, .resources = 8u, .access_steps = 128u, .constants = 128u,
        .constant_edges = 256u, .ir_instructions = 1024u, .ir_operands = 1024u,
        .target_declarations = 1024u, .traversal_items = 128u, .words = 8192u,
        .expression_depth = 32u, .control_depth = 8u, .type_syntax_depth = 32u},
    .entries = 4u, .entry_inputs = 16u, .entry_references = 32u
};
static const char source[] =
    "@group(0) @binding(0) var input:texture_2d<f32>;"
    "@group(0) @binding(1) var output:texture_storage_2d<rgba8unorm,write>;"
    "@group(0) @binding(2) var filtering:sampler;"
    "@group(0) @binding(3) var comparison:sampler_comparison;"
    "@group(0) @binding(4) var<storage,read_write> values:array<u32>;"
    "fn load(t:texture_2d<f32>)->vec4f{return textureLoad(t,vec2i(-1,0),0);}"
    "@compute @workgroup_size(1) fn first(){textureStore(output,vec2i(0),load(input));}"
    "@compute @workgroup_size(1) fn second(){let unused=false&&(textureDimensions(input).x>0u);values[0]=1u;}";
static DoeCompilerStatus compile(void *workspace, size_t bytes, const char *text, uint32_t flags,
                                  const DoeCompilerResourceOutputs *outputs, DoeCompilerResourceResult *result) {
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NAME, 0u, "second", 6u};
    return doe_compiler_compile_resources(6u, &capacities, sizeof(capacities), workspace, bytes,
        text, strlen(text), &selection, sizeof(selection), flags, outputs,
        outputs == NULL ? 0u : sizeof(*outputs), result, sizeof(*result));
}
int main(void) {
    DoeCompilerWorkspaceRequirements requirements, old_requirements;
    assert(doe_compiler_resource_requirements(6u, &capacities, sizeof(capacities), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    assert(doe_compiler_module_requirements(5u, &capacities, sizeof(capacities), &old_requirements, sizeof(old_requirements)) == DOE_COMPILER_OK);
    assert(requirements.size_bytes == old_requirements.size_bytes);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    uint32_t words[8192], references[32];
    DoeCompilerTypeV4 types[64]; DoeCompilerMemberV4 members[32]; DoeCompilerResourceV4 resources[8];
    DoeCompilerWorkgroupV5 workgroups[4]; DoeCompilerEntryV5 entries[4]; DoeCompilerHandleV6 handles[8];
    char names[1024];
    DoeCompilerResourceOutputs outputs = {.module = {
        .words = words, .word_capacity = 8192u,
        .types = types, .type_capacity = 64u, .type_record_size = sizeof(types[0]),
        .members = members, .member_capacity = 32u, .member_record_size = sizeof(members[0]),
        .resources = resources, .resource_capacity = 8u, .resource_record_size = sizeof(resources[0]),
        .workgroups = workgroups, .workgroup_capacity = 4u, .workgroup_record_size = sizeof(workgroups[0]),
        .entries = entries, .entry_capacity = 4u, .entry_record_size = sizeof(entries[0]),
        .entry_references = references, .entry_reference_capacity = 32u, .names = names, .name_capacity = sizeof(names)},
        .handles = handles, .handle_capacity = 8u, .handle_record_size = sizeof(handles[0])};
    DoeCompilerResourceResult result, measured;
    errno = EDOM;
    assert(compile(workspace, requirements.size_bytes, source, 7u, NULL, &measured) == DOE_COMPILER_OK);
    assert(errno == EDOM && measured.required_handles == 4u && measured.published_handles == 0u);
    assert(measured.module.required_is_exact == 1u && measured.module.module.schema_version == 0u);
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(errno == EDOM && result.module.module.schema_version == 6u && result.module.module.selected_entry == 1u);
    assert(memcmp(&result.module.required, &measured.module.required, sizeof(result.module.required)) == 0);
    assert(result.published_handles == 4u && result.module.published.resources == 5u);
    for (size_t i = 0u; i < result.published_handles; ++i) {
        assert(handles[i].resource_index == i);
        assert(resources[i].type_index == DOE_COMPILER_TYPED_NONE);
        assert(resources[i].minimum_binding_size_bytes == 0u && resources[i].type_alignment_bytes == 0u);
        assert(resources[i].runtime_array_type == DOE_COMPILER_TYPED_NONE);
        assert(handles[i].reserved[0] == 0u && handles[i].reserved[1] == 0u && handles[i].reserved[2] == 0u);
    }
    assert(handles[0].view_dimension == DOE_COMPILER_VIEW_2D && handles[0].sample_scalar == DOE_COMPILER_LEAF_F32);
    assert(handles[1].storage_format == DOE_COMPILER_FORMAT_RGBA8UNORM && resources[1].access == DOE_COMPILER_WRITE);
    assert(handles[2].view_dimension == 0u && handles[2].sample_scalar == 0u && handles[2].flags == 0u);
    assert(handles[3].flags == DOE_COMPILER_HANDLE_COMPARISON);
    assert(resources[4].kind == DOE_COMPILER_STORAGE_BUFFER && resources[4].minimum_binding_size_bytes == 4u);
    assert(entries[0].resource_reference_count == 2u && entries[1].resource_reference_count == 2u);
    assert(references[entries[0].first_resource_reference] == 0u && references[entries[0].first_resource_reference + 1u] == 1u);
    assert(references[entries[1].first_resource_reference] == 0u && references[entries[1].first_resource_reference + 1u] == 4u);
    memset(words, 0xa5, sizeof(words)); memset(handles, 0xa5, sizeof(handles)); memset(names, 0xa5, sizeof(names));
    outputs.handle_capacity = 3u;
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.required_handles == 4u && result.published_handles == 0u && result.module.published.words == 0u);
    assert(words[0] == UINT32_C(0xa5a5a5a5) && handles[0].flags == UINT32_C(0xa5a5a5a5) && (unsigned char)names[0] == 0xa5u);
    outputs.handle_capacity = 0u; outputs.handles = NULL;
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    outputs.handle_capacity = 8u; outputs.handles = handles;
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NONE, 0u, NULL, 0u};
    DoeCompilerModuleResult old;
    assert(doe_compiler_compile_module(5u, &capacities, sizeof(capacities), workspace, requirements.size_bytes,
        source, strlen(source), &selection, sizeof(selection), 6u, NULL, 0u, &old, sizeof(old)) == DOE_COMPILER_UNSUPPORTED);
    assert(doe_compiler_compile_module(5u, &capacities, sizeof(capacities), workspace, requirements.size_bytes,
        source, strlen(source), &selection, sizeof(selection), 5u, NULL, 0u, &old, sizeof(old)) == DOE_COMPILER_OK);
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    const char buffer_only[] = "@compute @workgroup_size(1) fn second(){}";
    outputs.handles = NULL; outputs.handle_capacity = 0u;
    assert(compile(workspace, requirements.size_bytes, buffer_only, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.required_handles == 0u && result.published_handles == 0u);
    outputs.handles = handles; outputs.handle_capacity = 8u;
    outputs.handle_record_size = sizeof(handles[0]) - 1u;
    memset(&result, 0xa5, sizeof(result));
    DoeCompilerResourceResult unchanged = result;
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_ABI_MISMATCH);
    assert(memcmp(&unchanged, &result, sizeof(result)) == 0);
    outputs.handle_record_size = sizeof(handles[0]);
    outputs.handle_capacity = SIZE_MAX;
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_INPUT_ERROR);
    outputs.handle_capacity = 8u;
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    free(workspace);
    puts("ABI6 resource reflection, exact counts, source-static closure and failure/reuse checks passed");
    return 0;
}
