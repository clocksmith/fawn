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
    .module = {.typed = {.source_bytes = 8192u, .tokens = 1024u, .statements = 64u, .expressions = 256u,
        .types = 64u, .members = 32u, .resources = 8u, .access_steps = 128u, .constants = 128u,
        .constant_edges = 256u, .ir_instructions = 512u, .ir_operands = 512u,
        .target_declarations = 512u, .traversal_items = 128u, .words = 8192u,
        .expression_depth = 32u, .control_depth = 8u, .type_syntax_depth = 32u},
        .entries = 2u, .entry_inputs = 4u, .entry_references = 16u},
    .interface_attributes = 8u, .stage_interfaces = 8u
};
static DoeCompilerStatus compile(uint32_t version, const DoeCompilerStageCapacities *caps,
    void *workspace, size_t bytes, const char *text, size_t length, uint32_t mode,
    uint32_t flags, const DoeCompilerStageOutputs *outputs, DoeCompilerStageResult *result) {
    const DoeCompilerEntrySelectionV5 selection = {mode, 0u,
        mode == DOE_COMPILER_SELECT_NAME ? "helper" : NULL, mode == DOE_COMPILER_SELECT_NAME ? 6u : 0u};
    if (version == 5u) return doe_compiler_compile_module(version, &caps->module, sizeof(caps->module), workspace, bytes,
        text, length, &selection, sizeof(selection), flags, outputs == NULL ? NULL : &outputs->resources.module,
        outputs == NULL ? 0u : sizeof(outputs->resources.module), &result->resources.module, sizeof(result->resources.module));
    if (version == 6u) return doe_compiler_compile_resources(version, &caps->module, sizeof(caps->module), workspace, bytes,
        text, length, &selection, sizeof(selection), flags, outputs == NULL ? NULL : &outputs->resources,
        outputs == NULL ? 0u : sizeof(outputs->resources), &result->resources, sizeof(result->resources));
    return doe_compiler_compile_stages(version, caps, sizeof(*caps), workspace, bytes,
        text, length, &selection, sizeof(selection), flags, outputs, outputs == NULL ? 0u : sizeof(*outputs), result, sizeof(*result));
}
int main(void) {
    DoeCompilerWorkspaceRequirements requirement;
    assert(doe_compiler_stage_requirements(8u, &capacities, sizeof(capacities), &requirement, sizeof(requirement)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirement.size_bytes);
    assert(workspace != NULL);
    uint32_t words[8192], references[16]; char names[1024];
    DoeCompilerTypeV4 types[64]; DoeCompilerMemberV4 members[32]; DoeCompilerResourceV4 resources[8];
    DoeCompilerWorkgroupV5 workgroups[4]; DoeCompilerEntryV5 entries[2];
    DoeCompilerStageOutputs outputs = {.resources = {.module = {
        .words = words, .word_capacity = 8192u,
        .types = types, .type_capacity = 64u, .type_record_size = sizeof(types[0]),
        .members = members, .member_capacity = 32u, .member_record_size = sizeof(members[0]),
        .resources = resources, .resource_capacity = 8u, .resource_record_size = sizeof(resources[0]),
        .workgroups = workgroups, .workgroup_capacity = 4u, .workgroup_record_size = sizeof(workgroups[0]),
        .entries = entries, .entry_capacity = 2u, .entry_record_size = sizeof(entries[0]),
        .entry_references = references, .entry_reference_capacity = 16u, .names = names, .name_capacity = sizeof(names)},
        .handle_record_size = sizeof(DoeCompilerHandleV6)}, .interface_record_size = sizeof(DoeCompilerStageInterfaceV7)};
    DoeCompilerStageResult result;
    const char *valid[] = {NULL, "", " \n// comment\n/* nested /* comment */ */", ";;;", "const value=3u;",
        "fn helper()->u32{return 7u;}fn caller()->u32{return helper();}"};
    errno = EDOM;
    assert(fesetround(FE_UPWARD) == 0);
    for (uint32_t version = 5u; version <= 8u; ++version) {
        for (size_t i = 0u; i < sizeof(valid) / sizeof(valid[0]); ++i) {
            const size_t length = valid[i] == NULL ? 0u : strlen(valid[i]);
            memset(words, 0xa5, sizeof(words)); memset(entries, 0xa5, sizeof(entries));
            assert(compile(version, &capacities, workspace, requirement.size_bytes, valid[i], length, 0u, 3u, &outputs, &result) == DOE_COMPILER_OK);
            assert(result.resources.module.required_is_exact == 1u && result.resources.module.published.words == 0u);
            assert(result.resources.module.published.entries == 0u && result.resources.module.module.selected_entry == UINT32_MAX);
            assert(result.resources.module.module.schema_version == version);
            assert(words[0] == UINT32_C(0xa5a5a5a5) && entries[0].stage == UINT32_C(0xa5a5a5a5));
            assert(compile(version, &capacities, workspace, requirement.size_bytes, valid[i], length, 1u, 7u, NULL, &result) == DOE_COMPILER_SEMANTIC_ERROR);
            assert(compile(version, &capacities, workspace, requirement.size_bytes, valid[i], length, 2u, 7u, NULL, &result) == DOE_COMPILER_SEMANTIC_ERROR);
        }
    }
    DoeCompilerStageCapacities empty = capacities;
    empty.module.typed.source_bytes = 0u;
    empty.module.typed.tokens = 0u;
    empty.module.typed.statements = 0u;
    empty.module.typed.expressions = 0u;
    empty.module.typed.target_declarations = 0u;
    empty.module.typed.words = 0u;
    empty.module.entries = 0u;
    assert(compile(8u, &empty, workspace, requirement.size_bytes, NULL, 0u, 0u, 7u, NULL, &result) == DOE_COMPILER_OK);
    assert(result.resources.module.required.words == 0u && result.resources.module.required.entries == 0u);
    assert(compile(8u, &capacities, workspace, requirement.size_bytes, NULL, 1u, 0u, 7u, NULL, &result) == DOE_COMPILER_INPUT_ERROR);
    const char library[] = "struct S{m:mat3x2f} @group(0) @binding(0) var<uniform> input:S;"
        "fn helper()->mat3x2f{return input.m;}";
    assert(compile(8u, &capacities, workspace, requirement.size_bytes, library, strlen(library), 0u, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.resources.module.published.words == 0u && result.resources.module.published.resources == 1u);
    assert(resources[0].minimum_binding_size_bytes == 24u && result.resources.module.published.types == 4u);
    const char *invalid[] = {"const bad=1/0;", "fn helper()->u32{}", "fn helper(){absent();}",
        "fn helper(){helper();}", "fn helper(){return;let bad=absent;}", "struct S{value:unknown}"};
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        memset(words, 0xa5, sizeof(words));
        assert(compile(8u, &capacities, workspace, requirement.size_bytes, invalid[i], strlen(invalid[i]), 0u, 3u, &outputs, &result) != DOE_COMPILER_OK);
        assert(result.resources.module.published.words == 0u && words[0] == UINT32_C(0xa5a5a5a5));
    }
    DoeCompilerTypedResult old;
    DoeCompilerWorkspaceRequirements old_requirement;
    assert(doe_compiler_typed_requirements(4u, &capacities.module.typed, sizeof(capacities.module.typed),
        &old_requirement, sizeof(old_requirement)) == DOE_COMPILER_OK);
    void *old_workspace = malloc(old_requirement.size_bytes);
    assert(old_workspace != NULL);
    assert(doe_compiler_compile_typed(4u, &capacities.module.typed, sizeof(capacities.module.typed), old_workspace,
        old_requirement.size_bytes, "", 0u, 5u, NULL, 0u, &old, sizeof(old)) == DOE_COMPILER_INPUT_ERROR);
    const char helper[] = "fn helper(){}";
    assert(doe_compiler_compile_typed(4u, &capacities.module.typed, sizeof(capacities.module.typed), old_workspace,
        old_requirement.size_bytes, helper, strlen(helper), 5u, NULL, 0u, &old, sizeof(old)) == DOE_COMPILER_PARSE_ERROR);
    free(old_workspace);
    const char entry[] = "@compute @workgroup_size(1) fn main(){}";
    assert(compile(8u, &capacities, workspace, requirement.size_bytes, entry, strlen(entry), 1u, 3u, &outputs, &result) == DOE_COMPILER_OK);
    assert(result.resources.module.published.words > 5u && result.resources.module.published.entries == 1u);
    assert(errno == EDOM && fegetround() == FE_UPWARD);
    assert(fesetround(FE_TONEAREST) == 0);
    free(workspace);
    puts("ABI5-8 empty/library validation, no synthetic binary, exact metadata, selection and workspace reuse passed");
    return 0;
}
