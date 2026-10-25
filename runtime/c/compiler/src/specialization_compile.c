/* SPDX-License-Identifier: MIT */
#include "override.h"
#include "compile_internal.h"
#include <errno.h>
#include <string.h>

static bool output_records(uint32_t target, const DoeCompilerSpecializationOutputs *outputs) {
    if (outputs == NULL) return true;
    const DoeCompilerStageOutputs *stages = target == DOE_COMPILER_TARGET_METAL ? &outputs->metal.stages :
        target == DOE_COMPILER_TARGET_HLSL ? &outputs->hlsl.stages : &outputs->spirv;
    if (!compiler_module_records(&stages->resources.module) ||
        stages->resources.handle_record_size != sizeof(DoeCompilerHandleV6) ||
        stages->interface_record_size != sizeof(DoeCompilerStageInterfaceV7)) return false;
    if (target == DOE_COMPILER_TARGET_METAL)
        return outputs->metal.binding_record_size == sizeof(DoeCompilerMetalBindingV12) &&
            outputs->metal.entry_record_size == sizeof(DoeCompilerMetalEntryV12);
    if (target == DOE_COMPILER_TARGET_HLSL)
        return outputs->hlsl.binding_record_size == sizeof(DoeCompilerHlslBindingV11) &&
            outputs->hlsl.entry_record_size == sizeof(DoeCompilerHlslEntryV11);
    return true;
}
static DoeCompilerStatus specialize(bool all_entries, uint32_t version, uint32_t target,
    const DoeCompilerSpecializationCapacities *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSpecializationOutputs *outputs, size_t outputs_size,
    DoeCompilerTextResult *result, size_t result_size) {
    const int saved_errno = errno;
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != (all_entries ? DOE_COMPILER_MODULE_SPECIALIZATION_ABI_VERSION : DOE_COMPILER_SPECIALIZATION_ABI_VERSION) ||
        capacities_size != sizeof(*caps) ||
        selection_size != sizeof(*selection) || value_record_size != sizeof(*values) ||
        result_size != sizeof(*result) || (outputs != NULL ? outputs_size != sizeof(*outputs) : outputs_size != 0u) ||
        !output_records(target, outputs)) return DOE_COMPILER_ABI_MISMATCH;
    memset(result, 0, sizeof(*result));
    DoeCompilerModuleResult *module = &result->stages.resources.module;
    Source s = {.source=source, .length=length, .diagnostic=&module->diagnostic};
    DoeCompilerWorkspaceRequirements requirements;
    size_t base;
    bool valid = specialization_layout(target, caps, &base, &requirements) &&
        workspace != NULL && workspace_size >= requirements.size_bytes &&
        (uintptr_t)workspace % requirements.alignment_bytes == 0u &&
        selection != NULL && (all_entries || selection->mode != DOE_COMPILER_SELECT_NONE) && value_count < UINT32_MAX &&
        compiler_valid_span(values, value_count, sizeof(*values), _Alignof(DoeCompilerOverrideValueV13), true);
    if (valid) for (size_t i = 0u; i < value_count; ++i)
        if (values[i].key == NULL || values[i].key_length == 0u || values[i].key_length > DOE_COMPILER_MAX_NAME_BYTES) valid = false;
    if (!valid) {
        (void)source_error(&s, DOE_COMPILER_INPUT_ERROR, 0u, "invalid specialization capacities/workspace/selection/value spans");
        errno = saved_errno;
        return module->diagnostic.status;
    }
    SpecializationContext context = {.values=values, .value_count=value_count,
        .capacities=caps, .selection=selection, .all_entries=all_entries};
    specialization_bind(&context, workspace, base, caps);
    DoeCompilerStatus status;
    if (target == DOE_COMPILER_TARGET_SPIRV) {
        const DoeCompilerStageOutputs *out = outputs != NULL ? &outputs->spirv : NULL;
        status = compiler_compile_module(&caps->target.stages.module, workspace, workspace_size,
            source, length, selection, flags, out != NULL ? &out->resources.module : NULL, module,
            out != NULL ? &out->resources : NULL, &result->stages.resources, &caps->target.stages,
            out, &result->stages, DOE_COMPILER_ATOMIC_ABI_VERSION, &context);
    } else status = compiler_specialized_text(target, &caps->target, workspace, workspace_size,
        source, length, selection, flags, outputs, result, &context);
    errno = saved_errno;
    return status;
}

DoeCompilerStatus doe_compiler_specialize(uint32_t version, uint32_t target,
    const DoeCompilerSpecializationCapacities *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSpecializationOutputs *outputs, size_t outputs_size,
    DoeCompilerTextResult *result, size_t result_size) {
    return specialize(false, version, target, caps, capacities_size, workspace, workspace_size,
        source, length, selection, selection_size, values, value_count, value_record_size,
        flags, outputs, outputs_size, result, result_size);
}
DoeCompilerStatus doe_compiler_specialize_module(uint32_t version, uint32_t target,
    const DoeCompilerSpecializationCapacities *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSpecializationOutputs *outputs, size_t outputs_size,
    DoeCompilerTextResult *result, size_t result_size) {
    const DoeCompilerEntrySelectionV5 selection = {.mode = DOE_COMPILER_SELECT_NONE};
    return specialize(true, version, target, caps, capacities_size, workspace, workspace_size,
        source, length, &selection, sizeof(selection), values, value_count, value_record_size,
        flags, outputs, outputs_size, result, result_size);
}
