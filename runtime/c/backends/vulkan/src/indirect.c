/* SPDX-License-Identifier: MIT */
#include "indirect_internal.h"
#include "../../../common/include/doe_dispatch_guard.h"

doe_vk_status doe_vk_dispatch_validator_source(const uint32_t limits[3],
    char *out, size_t capacity, size_t *out_length) {
    switch (doe_dispatch_guard_source(limits,out,capacity,out_length)) {
    case DOE_DISPATCH_GUARD_SOURCE_OK: return DOE_VK_OK;
    case DOE_DISPATCH_GUARD_SOURCE_INVALID_ARGUMENT:
    case DOE_DISPATCH_GUARD_SOURCE_TOO_SMALL: return DOE_VK_INVALID_ARGUMENT;
    case DOE_DISPATCH_GUARD_SOURCE_FORMAT_ERROR: return DOE_VK_INVALID_STATE;
    }
    return DOE_VK_INVALID_STATE;
}

doe_vk_status doe_vk_dispatch_validator_validate(const doe_vk_caps *caps,
    const uint32_t limits[3]) {
    if (caps == NULL || limits == NULL || limits[0] == 0 || limits[1] == 0 || limits[2] == 0)
        return DOE_VK_INVALID_ARGUMENT;
    for (unsigned i = 0; i < 3; ++i) {
        if (limits[i] > caps->max_workgroup_count[i] || caps->max_workgroup_size[i] == 0)
            return DOE_VK_UNSUPPORTED;
    }
    if (caps->max_workgroup_invocations == 0 || caps->max_storage_buffer_range < 12 ||
        caps->max_bind_groups == 0 || caps->max_bindings_per_group == 0 ||
        caps->max_storage_buffers_per_stage == 0) return DOE_VK_UNSUPPORTED;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_dispatch_validator_create(doe_vk_context *context,
    const doe_vk_dispatch_validator_desc *desc, doe_vk_dispatch_validator **out,
    doe_vk_error *error) {
    if (context == NULL || desc == NULL || out == NULL)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "indirect validator descriptor", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "indirect validator context", error);
    doe_vk_status status = doe_vk_dispatch_validator_validate(&context->caps, desc->max_workgroup_count);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "indirect validator limits", error);
    doe_vk_dispatch_validator *validator = calloc(1, sizeof(*validator));
    if (validator == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "indirect validator owner", error);
    validator->references = 1;
    const doe_vk_binding_layout layout = {0, 0, DOE_VK_BINDING_STORAGE};
    status = doe_vk_pipeline_create(context, desc->spirv, desc->word_count, "main",
        &layout, 1, &validator->pipeline, error);
    if (status == DOE_VK_OK) status = doe_vk_buffer_create(context, 12,
        DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_COPY_DST | DOE_VK_BUFFER_INDIRECT,
        DOE_VK_MEMORY_DEVICE, &validator->scratch, error);
    if (status != DOE_VK_OK) {
        doe_vk_dispatch_validator_release(validator);
        return status;
    }
    *out = validator;
    return doe_vk_fail(DOE_VK_OK, "indirect validator ready", error);
}

doe_vk_status doe_vk_dispatch_validator_retain(doe_vk_dispatch_validator *validator) {
    if (validator == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (validator->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++validator->references;
    return DOE_VK_OK;
}
void doe_vk_dispatch_validator_release(doe_vk_dispatch_validator *validator) {
    if (validator == NULL || --validator->references != 0) return;
    doe_vk_pipeline_release(validator->pipeline);
    doe_vk_buffer_release(validator->scratch);
    free(validator);
}

doe_vk_status doe_vk_commands_dispatch_indirect(doe_vk_commands *commands,
    doe_vk_dispatch_validator *validator, doe_vk_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, size_t binding_count,
    doe_vk_buffer *arguments, uint64_t offset) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (validator == NULL || validator->pipeline->context != commands->context ||
        pipeline == NULL || pipeline->graphics || pipeline->context != commands->context ||
        arguments == NULL || arguments->context != commands->context ||
        !(arguments->usage & DOE_VK_BUFFER_INDIRECT) || !(arguments->usage & DOE_VK_BUFFER_COPY_SRC) ||
        offset % 4 != 0 || !doe_vk_range(arguments->size, offset, 12))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    /* Complete target admission before any argument copy or guard dispatch.
     * Prepared image transitions remain valid across the scratch-only guard. */
    doe_vk_dispatch_use *target = NULL;
    status = doe_vk_prepare_resources(commands, pipeline, bindings, binding_count, &target);
    if (status != DOE_VK_OK) return status;
    /* Every dispatch overwrites the complete scratch record. Copy and compute
     * helpers provide the same leases/barriers/poisoning as direct operations. */
    status = doe_vk_commands_copy(commands, arguments, offset, validator->scratch, 0, 12);
    if (status != DOE_VK_OK) return status;
    const doe_vk_buffer_binding scratch_binding = {validator->scratch, 0, 12};
    status = doe_vk_commands_dispatch(commands, validator->pipeline, &scratch_binding, 1, 1, 1, 1);
    if (status != DOE_VK_OK) return status;
    doe_vk_barrier(commands);
    doe_vk_bind_prepared_resources(commands, target);
    commands->context->vk.vkCmdDispatchIndirect(commands->command, validator->scratch->buffer, 0);
    return DOE_VK_OK;
}
