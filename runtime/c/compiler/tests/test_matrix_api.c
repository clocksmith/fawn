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
        .target_declarations = 1024u, .traversal_items = 128u, .words = 32768u,
        .expression_depth = 32u, .control_depth = 8u, .type_syntax_depth = 32u},
        .entries = 4u, .entry_inputs = 16u, .entry_references = 32u},
    .interface_attributes = 32u, .stage_interfaces = 32u
};
static const char source[] =
    "struct S{prefix:vec2f,matrix:mat3x2f,suffix:vec2u}"
    "@group(0) @binding(0) var<uniform> data:S;"
    "@group(0) @binding(1) var<storage,read_write> output:array<mat3x2f>;"
    "fn fetch_matrix()->mat3x2f{return data.matrix;}"
    "@compute @workgroup_size(1) fn main(){output[0]=fetch_matrix();}"
    "@compute @workgroup_size(1) fn other(){}";
static DoeCompilerStatus compile(uint32_t version, const DoeCompilerStageCapacities *caps,
    void *workspace, size_t bytes, const char *text, uint32_t flags,
    const DoeCompilerStageOutputs *outputs, DoeCompilerStageResult *result) {
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_AUTO, 0u, NULL, 0u};
    const DoeCompilerEntrySelectionV5 named = {DOE_COMPILER_SELECT_NAME, 0u, "main", 4u};
    return doe_compiler_compile_stages(version, caps, sizeof(*caps), workspace, bytes, text, strlen(text),
        strstr(text, "other") == NULL ? &selection : &named, sizeof(selection), flags,
        outputs, outputs == NULL ? 0u : sizeof(*outputs), result, sizeof(*result));
}
int main(void) {
    DoeCompilerWorkspaceRequirements requirements, previous;
    assert(doe_compiler_stage_requirements(8u, &capacities, sizeof(capacities), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    assert(doe_compiler_stage_requirements(7u, &capacities, sizeof(capacities), &previous, sizeof(previous)) == DOE_COMPILER_OK);
    assert(requirements.size_bytes == previous.size_bytes && requirements.alignment_bytes == previous.alignment_bytes);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    uint32_t *words = malloc(32768u * sizeof(*words));
    assert(words != NULL);
    uint32_t references[32]; char names[2048];
    DoeCompilerTypeV4 types[64]; DoeCompilerMemberV4 members[32]; DoeCompilerResourceV4 resources[8];
    DoeCompilerWorkgroupV5 workgroups[4]; DoeCompilerEntryV5 entries[4];
    DoeCompilerStageOutputs outputs = {.resources = {.module = {
        .words = words, .word_capacity = 32768u,
        .types = types, .type_capacity = 64u, .type_record_size = sizeof(types[0]),
        .members = members, .member_capacity = 32u, .member_record_size = sizeof(members[0]),
        .resources = resources, .resource_capacity = 8u, .resource_record_size = sizeof(resources[0]),
        .workgroups = workgroups, .workgroup_capacity = 4u, .workgroup_record_size = sizeof(workgroups[0]),
        .entries = entries, .entry_capacity = 4u, .entry_record_size = sizeof(entries[0]),
        .entry_references = references, .entry_reference_capacity = 32u, .names = names, .name_capacity = sizeof(names)},
        .handle_record_size = sizeof(DoeCompilerHandleV6)}, .interface_record_size = sizeof(DoeCompilerStageInterfaceV7)};
    DoeCompilerStageResult measured, result;
    errno = EDOM;
    assert(fesetround(FE_DOWNWARD) == 0);
    assert(compile(8u, &capacities, workspace, requirements.size_bytes, source, 7u, NULL, &measured) == DOE_COMPILER_OK);
    assert(errno == EDOM && fegetround() == FE_DOWNWARD);
    assert(compile(8u, &capacities, workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.resources.module.module.schema_version == 8u);
    assert(memcmp(&result.resources.module.required, &measured.resources.module.required, sizeof(measured.resources.module.required)) == 0);
    assert(result.required_interfaces == 0u && result.resources.required_handles == 0u);
    assert(result.resources.module.published.resources == 2u && result.resources.module.published.entries == 2u);
    assert(resources[0].minimum_binding_size_bytes == 40u && resources[0].type_alignment_bytes == 8u);
    assert(resources[1].runtime_array_stride_bytes == 24u && resources[1].minimum_binding_size_bytes == 24u);
    assert(members[1].offset_bytes == 8u && members[1].size_bytes == 24u && members[2].offset_bytes == 32u);
    assert(entries[0].resource_reference_count == 2u && entries[1].resource_reference_count == 0u);
    size_t matrices = 0u;
    for (size_t i = 0u; i < result.resources.module.published.types; ++i) if (types[i].kind == DOE_COMPILER_TYPE_MATRIX) {
        const DoeCompilerTypeV4 *m = &types[i];
        assert(m->array_count == 3u && m->vector_width == 2u && m->stride_bytes == 8u);
        assert(m->size_bytes == 24u && m->alignment_bytes == 8u);
        assert(types[m->element_type].kind == DOE_COMPILER_TYPE_VECTOR && types[m->element_type].vector_width == 2u);
        matrices++;
    }
    assert(matrices == 1u);
    memset(words, 0xa5, 32768u * sizeof(*words)); memset(types, 0xa5, sizeof(types)); memset(names, 0xa5, sizeof(names));
    assert(compile(7u, &capacities, workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_UNSUPPORTED);
    assert(result.resources.module.published.words == 0u && words[0] == UINT32_C(0xa5a5a5a5));
    assert(types[0].kind == UINT32_C(0xa5a5a5a5) && (unsigned char)names[0] == 0xa5u);
    assert(compile(7u, &capacities, workspace, requirements.size_bytes, source, 5u, NULL, &result) == DOE_COMPILER_OK);
    const char local[] = "@compute @workgroup_size(1) fn main(){let m=mat2x2f();let t=transpose(m);}";
    assert(compile(7u, &capacities, workspace, requirements.size_bytes, local, 7u, NULL, &result) == DOE_COMPILER_OK);
    outputs.resources.module.type_capacity = 0u;
    assert(compile(8u, &capacities, workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.resources.module.required_is_exact == 1u && result.resources.module.published.words == 0u);
    assert(words[0] == UINT32_C(0xa5a5a5a5));
    outputs.resources.module.type_capacity = 64u;
    DoeCompilerStageCapacities short_caps = capacities;
    short_caps.module.typed.target_declarations = 1u;
    assert(compile(8u, &short_caps, workspace, requirements.size_bytes, source, 7u, NULL, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(result.resources.module.exhausted_pool == DOE_COMPILER_POOL_TARGET_DECLARATIONS);
    short_caps = capacities; short_caps.module.typed.words = 16u;
    assert(compile(8u, &short_caps, workspace, requirements.size_bytes, source, 7u, NULL, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.resources.module.exhausted_pool == DOE_COMPILER_POOL_WORDS);
    assert(compile(8u, &capacities, workspace, requirements.size_bytes, source, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(errno == EDOM && fegetround() == FE_DOWNWARD);
    assert(fesetround(FE_TONEAREST) == 0);
    free(words); free(workspace);
    puts("ABI8 matrix dimensions/stride, exact old layout, sparse closure, atomic projection failure and reuse passed");
    return 0;
}
