/* SPDX-License-Identifier: MIT */
#include "cli_text.h"
#include "../include/doe_compiler_pool_growth.h"
#include <stdlib.h>
#include <string.h>

static DoeCompilerStatus requirements(uint32_t target,
    const DoeCompilerTextCapacities *capacities, DoeCompilerWorkspaceRequirements *result) {
    if (target == DOE_COMPILER_TARGET_METAL)
        return doe_compiler_metal_requirements(DOE_COMPILER_METAL_ABI_VERSION,
            capacities, sizeof(*capacities), result, sizeof(*result));
    return doe_compiler_hlsl_requirements(DOE_COMPILER_HLSL_ABI_VERSION,
        capacities, sizeof(*capacities), result, sizeof(*result));
}

static DoeCompilerStatus compile(uint32_t target, const DoeCompilerTextCapacities *capacities,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    char *text, size_t text_size, bool measure, DoeCompilerTextResult *result) {
    const DoeCompilerEntrySelectionV5 selection = {.mode = DOE_COMPILER_SELECT_NONE};
    const uint32_t flags = DOE_COMPILER_TEXT_CODE | (measure ? DOE_COMPILER_TEXT_MEASURE : 0u);
    const DoeCompilerStageOutputs stages = {
        .resources = {.module = {
            .type_record_size = sizeof(DoeCompilerTypeV4),
            .member_record_size = sizeof(DoeCompilerMemberV4),
            .resource_record_size = sizeof(DoeCompilerResourceV4),
            .workgroup_record_size = sizeof(DoeCompilerWorkgroupV5),
            .entry_record_size = sizeof(DoeCompilerEntryV5)},
            .handle_record_size = sizeof(DoeCompilerHandleV6)},
            .interface_record_size = sizeof(DoeCompilerStageInterfaceV7)
    };
    if (target == DOE_COMPILER_TARGET_METAL) {
        const DoeCompilerMetalOutputs output = {.stages = stages,
            .text = text, .text_capacity = text_size,
            .binding_record_size = sizeof(DoeCompilerMetalBindingV12),
            .entry_record_size = sizeof(DoeCompilerMetalEntryV12)};
        return doe_compiler_compile_metal(DOE_COMPILER_METAL_ABI_VERSION,
            capacities, sizeof(*capacities), workspace, workspace_size, source, length,
            &selection, sizeof(selection), flags, measure ? NULL : &output,
            measure ? 0u : sizeof(output), result, sizeof(*result));
    }
    const DoeCompilerHlslOutputs output = {.stages = stages,
        .text = text, .text_capacity = text_size,
        .binding_record_size = sizeof(DoeCompilerHlslBindingV11),
        .entry_record_size = sizeof(DoeCompilerHlslEntryV11)};
    return doe_compiler_compile_hlsl(DOE_COMPILER_HLSL_ABI_VERSION,
        capacities, sizeof(*capacities), workspace, workspace_size, source, length,
        &selection, sizeof(selection), flags, measure ? NULL : &output,
        measure ? 0u : sizeof(output), result, sizeof(*result));
}

bool cli_compile_text(uint32_t target, const DoeCompilerStageCapacities *stages,
    const char *source, size_t length, unsigned char **code, size_t *code_length,
    DoeCompilerDiagnostic *diagnostic) {
    *code = NULL;
    *code_length = 0u;
    *diagnostic = (DoeCompilerDiagnostic){.status = DOE_COMPILER_INPUT_ERROR};
    memcpy(diagnostic->message, "text job allocation or requirements failed",
        sizeof("text job allocation or requirements failed"));
    DoeCompilerTextCapacities capacities = {.stages = *stages};
    DoeCompilerWorkspaceRequirements size = {0};
    void *workspace = NULL;
    DoeCompilerTextResult result = {0};
    bool success = false;
    char *text = NULL;
    for (;;) {
        if (requirements(target, &capacities, &size) != DOE_COMPILER_OK) goto finish;
        workspace = malloc(size.size_bytes);
        if (workspace == NULL) goto finish;
        const DoeCompilerStatus status = compile(target, &capacities, workspace,
            size.size_bytes, source, length, NULL, 0u, true, &result);
        if (status == DOE_COMPILER_OK) break;
        if (!doe_compiler_grow_stage_pool(&capacities.stages, status,
                result.stages.resources.module.exhausted_pool,
                result.stages.resources.module.minimum_pool_capacity)) {
            *diagnostic = result.stages.resources.module.diagnostic;
            goto finish;
        }
        free(workspace);
        workspace = NULL;
    }
    capacities.text_bytes = result.required_text_bytes;
    if (requirements(target, &capacities, &size) != DOE_COMPILER_OK) goto finish;
    free(workspace);
    workspace = malloc(size.size_bytes);
    text = malloc(capacities.text_bytes == 0u ? 1u : capacities.text_bytes);
    if (workspace == NULL || text == NULL) goto finish;
    if (compile(target, &capacities, workspace, size.size_bytes, source, length,
            text, capacities.text_bytes, false, &result) != DOE_COMPILER_OK) {
        *diagnostic = result.stages.resources.module.diagnostic;
        goto finish;
    }
    *code = (unsigned char *)text;
    *code_length = result.published_text_bytes;
    *diagnostic = result.stages.resources.module.diagnostic;
    text = NULL;
    success = true;
finish:
    free(text);
    free(workspace);
    return success;
}
