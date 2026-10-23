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
    .module = {.typed = {.source_bytes = 8192, .tokens = 2048, .statements = 64, .expressions = 512,
        .types = 64, .members = 32, .resources = 8, .access_steps = 128, .constants = 128,
        .constant_edges = 256, .ir_instructions = 1024, .ir_operands = 1024,
        .target_declarations = 1024, .traversal_items = 128, .words = 32768,
        .expression_depth = 32, .control_depth = 8, .type_syntax_depth = 32},
        .entries = 4, .entry_inputs = 16, .entry_references = 32},
    .interface_attributes = 32, .stage_interfaces = 32
};
static const char source[] =
    "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
    "fn next(p:ptr<function,u32>)->u32{let old=*p;*p+=1;return old;}"
    "@compute @workgroup_size(1) fn main(){var x=1u;let p=&x;out[0]=next(p);out[1]=x;}";
static const char bad[] =
    "fn f(p:ptr<function,u32>,q:ptr<function,u32>){*p=*q;}"
    "@compute @workgroup_size(1) fn main(){var x=1u;f(&x,&x);}";
static DoeCompilerStatus compile(uint32_t version, const DoeCompilerStageCapacities *caps,
    void *workspace, size_t bytes, const char *text, uint32_t flags,
    const DoeCompilerStageOutputs *outputs, DoeCompilerStageResult *result) {
    const DoeCompilerEntrySelectionV5 selection = {0};
    return doe_compiler_compile_stages(version, caps, sizeof(*caps), workspace, bytes,
        text, strlen(text), &selection, sizeof(selection), flags, outputs,
        outputs == NULL ? 0u : sizeof(*outputs), result, sizeof(*result));
}
int main(void) {
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_stage_requirements(9, &capacities, sizeof(capacities), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    uint32_t *words = malloc(32768 * sizeof(*words));
    assert(workspace != NULL && words != NULL);
    uint32_t references[32];
    char names[2048];
    DoeCompilerTypeV4 types[64];
    DoeCompilerMemberV4 members[32];
    DoeCompilerResourceV4 resources[8];
    DoeCompilerWorkgroupV5 workgroups[4];
    DoeCompilerEntryV5 entries[4];
    DoeCompilerStageOutputs outputs = {.resources = {.module = {
        .words = words, .word_capacity = 32768,
        .types = types, .type_capacity = 64, .type_record_size = sizeof(types[0]),
        .members = members, .member_capacity = 32, .member_record_size = sizeof(members[0]),
        .resources = resources, .resource_capacity = 8, .resource_record_size = sizeof(resources[0]),
        .workgroups = workgroups, .workgroup_capacity = 4, .workgroup_record_size = sizeof(workgroups[0]),
        .entries = entries, .entry_capacity = 4, .entry_record_size = sizeof(entries[0]),
        .entry_references = references, .entry_reference_capacity = 32,
        .names = names, .name_capacity = sizeof(names)},
        .handle_record_size = sizeof(DoeCompilerHandleV6)}, .interface_record_size = sizeof(DoeCompilerStageInterfaceV7)};
    DoeCompilerStageResult result;
    errno = EDOM;
    assert(fesetround(FE_DOWNWARD) == 0);
    for (uint32_t version = 7; version <= 9; ++version) {
        assert(compile(version, &capacities, workspace, requirements.size_bytes, source, 3, &outputs, &result) == DOE_COMPILER_OK);
        assert(result.resources.module.published.resources == 1 && result.resources.module.published.types == 2);
        assert(entries[0].resource_reference_count == 1 && resources[0].runtime_array_stride_bytes == 4);
        for (size_t i = 0; i < result.resources.module.published.types; ++i)
            assert(types[i].kind == DOE_COMPILER_TYPE_SCALAR || types[i].kind == DOE_COMPILER_TYPE_RUNTIME_ARRAY);
    }
    memset(words, 0xa5, 32768 * sizeof(*words));
    memset(types, 0xa5, sizeof(types));
    assert(compile(9, &capacities, workspace, requirements.size_bytes, bad, 3, &outputs, &result) == DOE_COMPILER_SEMANTIC_ERROR);
    assert(words[0] == UINT32_C(0xa5a5a5a5) && types[0].kind == UINT32_C(0xa5a5a5a5));
    assert(result.resources.module.published.words == 0 && strstr(result.resources.module.diagnostic.message, "alias") != NULL);
    outputs.resources.module.word_capacity = 1;
    assert(compile(9, &capacities, workspace, requirements.size_bytes, source, 3, &outputs, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(words[0] == UINT32_C(0xa5a5a5a5) && result.resources.module.required_is_exact == 1);
    outputs.resources.module.word_capacity = 32768;
    DoeCompilerStageCapacities short_caps = capacities;
    short_caps.module.typed.ir_operands = 0;
    assert(compile(9, &short_caps, workspace, requirements.size_bytes, source, 7, NULL, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(result.resources.module.exhausted_pool == DOE_COMPILER_POOL_IR_OPERANDS);
    assert(compile(9, &capacities, workspace, requirements.size_bytes, source, 3, &outputs, &result) == DOE_COMPILER_OK);
    assert(errno == EDOM && fegetround() == FE_DOWNWARD);
    assert(fesetround(FE_TONEAREST) == 0);
    free(words);
    free(workspace);
    puts("Function pointer ABI7-9 shared pipeline, sparse reflection, alias/output/pool failure atomicity and reuse passed");
    return 0;
}
