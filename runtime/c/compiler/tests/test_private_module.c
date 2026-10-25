/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_module.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void verify(const char *source, bool shared) {
    const DoeCompilerModuleCapacities capacities = {
        .typed = {.source_bytes = 4096u, .tokens = 1024u, .statements = 64u, .expressions = 256u,
            .types = 32u, .members = 32u, .resources = 4u, .access_steps = 128u,
            .constants = 64u, .constant_edges = 128u, .ir_instructions = 512u,
            .ir_operands = 128u, .target_declarations = 128u, .traversal_items = 97u,
            .words = 8192u, .expression_depth = 32u, .control_depth = 16u, .type_syntax_depth = 32u},
        .entries = 4u, .entry_inputs = 16u, .entry_references = 32u};
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_module_requirements(5u, &capacities, sizeof(capacities),
        &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    DoeCompilerTypeV4 types[32];
    DoeCompilerMemberV4 members[32];
    DoeCompilerResourceV4 resources[4];
    DoeCompilerWorkgroupV5 workgroups[1];
    DoeCompilerEntryV5 entries[2];
    uint32_t references[3];
    char names[256];
    const DoeCompilerModuleOutputs outputs = {
        .type_record_size = sizeof(*types), .types = types, .type_capacity = 32u,
        .member_record_size = sizeof(*members), .members = members, .member_capacity = 32u,
        .resource_record_size = sizeof(*resources), .resources = resources, .resource_capacity = 4u,
        .workgroup_record_size = sizeof(*workgroups), .workgroups = shared ? workgroups : NULL,
        .workgroup_capacity = shared ? 1u : 0u,
        .entry_record_size = sizeof(*entries), .entries = entries, .entry_capacity = 2u,
        .entry_references = shared ? references : NULL, .entry_reference_capacity = shared ? 3u : 0u,
        .names = names, .name_capacity = sizeof(names)};
    const DoeCompilerEntrySelectionV5 selection = {.mode = DOE_COMPILER_SELECT_NONE};
    DoeCompilerModuleResult result;
    const DoeCompilerStatus status = doe_compiler_compile_module(5u, &capacities, sizeof(capacities),
        workspace, requirements.size_bytes, source, strlen(source), &selection, sizeof(selection),
        DOE_COMPILER_MODULE_REFLECTION, &outputs, sizeof(outputs), &result, sizeof(result));
    if (status != DOE_COMPILER_OK) fprintf(stderr, "private reflection status %u: %s\n", (unsigned)status, result.diagnostic.message);
    assert(status == DOE_COMPILER_OK);
    assert(result.required.workgroups == (shared ? 1u : 0u));
    assert(result.published.workgroups == result.required.workgroups);
    assert(result.published.entry_references == (shared ? 3u : 0u));
    assert(entries[0].workgroup_reference_count == 0u);
    assert(entries[0].workgroup_storage_bytes == 0u && entries[0].flags == 0u);
    if (shared) {
        assert(result.published.entries == 2u);
        assert(entries[0].resource_reference_count == 1u && entries[1].resource_reference_count == 1u);
        assert(entries[1].workgroup_reference_count == 1u);
        assert(entries[1].workgroup_storage_bytes == 4u);
        assert(entries[1].flags == DOE_COMPILER_ENTRY_WORKGROUP_INITIALIZATION);
        assert(references[entries[1].first_workgroup_reference] == 0u);
        assert(workgroups[0].size_bytes == 4u);
        assert(strcmp(names + workgroups[0].name.offset, "sharedValue") == 0);
    }
    free(workspace);
}
int main(void) {
    verify("var<private> secret:u32=17u;@compute @workgroup_size(1) fn main(){secret+=1u;}", false);
    verify("var<private> first:u32=17u;var<workgroup> sharedValue:u32;var<private> last:u32;"
        "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn local(){last=first;data[0]=last;}"
        "@compute @workgroup_size(1) fn sharedEntry(){sharedValue=first;data[1]=sharedValue+last;}", true);
    puts("Private module reflection: exact shared-only counts, compact references, bytes and initialization flags passed.");
    return 0;
}
