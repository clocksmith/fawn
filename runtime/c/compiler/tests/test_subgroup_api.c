/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_features.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DoeCompilerModuleCapacities capacities = {
    .typed = {
        .source_bytes = 8192u, .tokens = 2048u, .statements = 64u, .expressions = 512u,
        .types = 32u, .members = 32u, .resources = 8u, .access_steps = 128u,
        .constants = 128u, .constant_edges = 256u, .ir_instructions = 1024u,
        .ir_operands = 1024u, .target_declarations = 1024u, .traversal_items = 128u,
        .words = 8192u, .expression_depth = 32u, .control_depth = 8u, .type_syntax_depth = 32u
    },
    .entries = 4u, .entry_inputs = 16u, .entry_references = 16u
};
static const char source[] =
    "enable subgroups;requires subgroup_uniformity;"
    "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
    "@compute @workgroup_size(8) fn first(@builtin(subgroup_size) size:u32,"
    "@builtin(subgroup_invocation_id) lane:u32){out[lane]=subgroupAdd(size);}"
    "@compute @workgroup_size(4) fn second(@builtin(num_subgroups) count:u32,"
    "@builtin(subgroup_id) group:u32){out[group]=count;}";

static DoeCompilerStatus compile(void *workspace, size_t bytes, const char *text,
                                  uint32_t flags, const DoeCompilerModuleOutputs *outputs,
                                  DoeCompilerModuleResult *result) {
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NONE, 0u, NULL, 0u};
    return doe_compiler_compile_module(DOE_COMPILER_MODULE_ABI_VERSION,
        &capacities, sizeof(capacities), workspace, bytes, text, strlen(text),
        &selection, sizeof(selection), flags, outputs, outputs == NULL ? 0u : sizeof(*outputs),
        result, sizeof(*result));
}

int main(void) {
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_module_requirements(DOE_COMPILER_MODULE_ABI_VERSION,
        &capacities, sizeof(capacities), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    uint32_t words[8192], references[16];
    DoeCompilerTypeV4 types[32];
    DoeCompilerMemberV4 members[32];
    DoeCompilerResourceV4 resources[8];
    DoeCompilerWorkgroupV5 workgroups[4];
    DoeCompilerEntryV5 entries[4];
    char names[1024];
    const DoeCompilerModuleOutputs outputs = {
        .words = words, .word_capacity = 8192u,
        .types = types, .type_capacity = 32u, .type_record_size = sizeof(types[0]),
        .members = members, .member_capacity = 32u, .member_record_size = sizeof(members[0]),
        .resources = resources, .resource_capacity = 8u, .resource_record_size = sizeof(resources[0]),
        .workgroups = workgroups, .workgroup_capacity = 4u, .workgroup_record_size = sizeof(workgroups[0]),
        .entries = entries, .entry_capacity = 4u, .entry_record_size = sizeof(entries[0]),
        .entry_references = references, .entry_reference_capacity = 16u,
        .names = names, .name_capacity = sizeof(names)
    };
    DoeCompilerModuleResult result;
    assert(compile(workspace, requirements.size_bytes, source, 7u, NULL, &result) == DOE_COMPILER_OK);
    assert(result.required_is_exact == 1u && result.required.entries == 2u);
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.module.selected_entry == DOE_COMPILER_MODULE_NONE && result.published.entries == 2u);
    assert(entries[0].declared_builtin_mask == (DOE_COMPILER_INPUT_SUBGROUP_SIZE |
                                               DOE_COMPILER_INPUT_SUBGROUP_INVOCATION_ID));
    assert(entries[0].static_builtin_mask == entries[0].declared_builtin_mask);
    assert(entries[1].declared_builtin_mask == (DOE_COMPILER_INPUT_NUM_SUBGROUPS |
                                               DOE_COMPILER_INPUT_SUBGROUP_ID));
    assert(entries[1].static_builtin_mask == entries[1].declared_builtin_mask);
    assert(strcmp(names + entries[0].name.offset, "first") == 0);
    assert(strcmp(names + entries[1].name.offset, "second") == 0);
    assert(entries[0].resource_reference_count == 1u && entries[1].resource_reference_count == 1u);
    assert(references[entries[0].first_resource_reference] == 0u);
    assert(references[entries[1].first_resource_reference] == 0u);
    memset(words, 0xa5, sizeof(words));
    memset(entries, 0xa5, sizeof(entries));
    assert(compile(workspace, requirements.size_bytes,
        "@compute @workgroup_size(1) fn main(@builtin(subgroup_size) size:u32){}",
        3u, &outputs, &result) == DOE_COMPILER_SEMANTIC_ERROR);
    assert(result.required_is_exact == 0u && result.published.words == 0u);
    assert(words[0] == UINT32_C(0xa5a5a5a5) && entries[0].stage == UINT32_C(0xa5a5a5a5));
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    free(workspace);
    puts("subgroup ABI5 masks, exact measurement, failure publication and reuse passed");
    return 0;
}
