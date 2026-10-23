/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_module.h"
#include <assert.h>
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
    "struct Tail{prefix:vec4u,tail:array<vec2u>}"
    "@group(0) @binding(0) var<storage,read> structured:Tail;"
    "@group(0) @binding(1) var<storage,read> other:array<u32>;"
    "@group(0) @binding(2) var<storage,read_write> output:array<u32>;"
    "fn size()->u32{return arrayLength(&other);}"
    "@compute @workgroup_size(1) fn first(){"
        "let unused=false&&(arrayLength(&structured.tail)>0u);output[0]=1u;}"
    "@compute @workgroup_size(1) fn second(){let n=size();}";
static DoeCompilerStatus compile(void *workspace, size_t bytes, const char *text, uint32_t flags,
    const DoeCompilerModuleOutputs *outputs, DoeCompilerModuleResult *result) {
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NAME, 0u, "first", 5u};
    return doe_compiler_compile_module(5u, &capacities, sizeof(capacities), workspace, bytes,
        text, strlen(text), &selection, sizeof(selection), flags, outputs,
        outputs == NULL ? 0u : sizeof(*outputs), result, sizeof(*result));
}
int main(void) {
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_module_requirements(5u, &capacities, sizeof(capacities),
        &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    uint32_t words[8192], references[32];
    DoeCompilerTypeV4 types[64]; DoeCompilerMemberV4 members[32]; DoeCompilerResourceV4 resources[8];
    DoeCompilerWorkgroupV5 workgroups[4]; DoeCompilerEntryV5 entries[4]; char names[1024];
    DoeCompilerModuleOutputs outputs = {
        .words = words, .word_capacity = 8192u,
        .types = types, .type_capacity = 64u, .type_record_size = sizeof(types[0]),
        .members = members, .member_capacity = 32u, .member_record_size = sizeof(members[0]),
        .resources = resources, .resource_capacity = 8u, .resource_record_size = sizeof(resources[0]),
        .workgroups = workgroups, .workgroup_capacity = 4u, .workgroup_record_size = sizeof(workgroups[0]),
        .entries = entries, .entry_capacity = 4u, .entry_record_size = sizeof(entries[0]),
        .entry_references = references, .entry_reference_capacity = 32u,
        .names = names, .name_capacity = sizeof(names)};
    DoeCompilerModuleResult measured, result;
    assert(compile(workspace, requirements.size_bytes, source, 7u, NULL, &measured) == DOE_COMPILER_OK);
    assert(measured.required_is_exact == 1u && measured.required.entry_references == 3u);
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(memcmp(&result.required, &measured.required, sizeof(result.required)) == 0);
    assert(entries[0].resource_reference_count == 2u && entries[1].resource_reference_count == 1u);
    assert(references[entries[0].first_resource_reference] == 0u);
    assert(references[entries[0].first_resource_reference + 1u] == 2u);
    assert(references[entries[1].first_resource_reference] == 1u);
    assert(resources[0].runtime_array_offset_bytes == 16u && resources[0].runtime_array_stride_bytes == 8u);
    assert(resources[1].runtime_array_offset_bytes == 0u && resources[1].runtime_array_stride_bytes == 4u);
    memset(words, 0xa5, sizeof(words)); memset(references, 0xa5, sizeof(references));
    outputs.entry_reference_capacity = 2u;
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.published.words == 0u && words[0] == UINT32_C(0xa5a5a5a5));
    assert(references[0] == UINT32_C(0xa5a5a5a5));
    outputs.entry_reference_capacity = 32u;
    const char *invalid = "@compute @workgroup_size(1) fn first(){let n=arrayLength(&absent);}";
    assert(compile(workspace, requirements.size_bytes, invalid, 3u, &outputs, &result) == DOE_COMPILER_SEMANTIC_ERROR);
    assert(words[0] == UINT32_C(0xa5a5a5a5) && references[0] == UINT32_C(0xa5a5a5a5));
    assert(compile(workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    free(workspace);
    puts("arrayLength ABI5 static closure, exact sizing, tail identity, atomic failure and reuse passed");
    return 0;
}
