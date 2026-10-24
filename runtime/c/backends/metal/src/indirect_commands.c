/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"
#include "../include/doe_metal_indirect.h"

struct doe_mtl_dispatch_guard {
    doe_mtl_pipeline *pipeline;
    uint32_t binding_slot;
    doe_mtl_dimensions max_groups;
};

doe_mtl_status doe_mtl_dispatch_guard_create(doe_mtl_context *context,
    const doe_mtl_dispatch_guard_desc *desc, doe_mtl_dispatch_guard **out, doe_mtl_error *error) {
    if (!context || !desc || !out || !desc->max_groups.x || !desc->max_groups.y || !desc->max_groups.z ||
        desc->pipeline.workgroup.x != 1 || desc->pipeline.workgroup.y != 1 || desc->pipeline.workgroup.z != 1 ||
        desc->binding_slot >= DOE_MTL_BUFFER_SLOTS ||
        (desc->pipeline.sizes_slot != UINT32_MAX && desc->binding_slot >= desc->pipeline.sizes_slot))
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "indirect guard descriptor", error);
    doe_mtl_dispatch_guard *guard = calloc(1, sizeof(*guard));
    if (!guard) return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "indirect guard allocation", error);
    doe_mtl_status status = doe_mtl_pipeline_create(context, &desc->pipeline, &guard->pipeline, error);
    if (status != DOE_MTL_OK) {
        free(guard);
        return status;
    }
    guard->binding_slot = desc->binding_slot;
    guard->max_groups = desc->max_groups;
    *out = guard;
    return doe_mtl_fail(DOE_MTL_OK, "indirect guard create", error);
}

void doe_mtl_dispatch_guard_release(doe_mtl_dispatch_guard *guard) {
    if (!guard) return;
    doe_mtl_pipeline_release(guard->pipeline);
    free(guard);
}

doe_mtl_status doe_mtl_commands_dispatch_indirect(doe_mtl_commands *commands,
    const doe_mtl_indirect_dispatch *dispatch, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    if (status != DOE_MTL_OK) return status;
    if (!dispatch || !dispatch->guard || !dispatch->pipeline || !dispatch->arguments)
        return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "indirect dispatch descriptor", error);
    const doe_mtl_dispatch_guard *guard = dispatch->guard;
    doe_mtl_pipeline *pipeline = dispatch->pipeline;
    if (pipeline->context != commands->context || guard->pipeline->context != commands->context)
        return doe_mtl_poison(commands, DOE_MTL_DEVICE_MISMATCH, "indirect dispatch context", error);
    if (!commands->context->native.dispatch_indirect)
        return doe_mtl_poison(commands, DOE_MTL_UNSUPPORTED, "native indirect dispatch unavailable", error);
    if (guard->max_groups.x > SIZE_MAX / pipeline->workgroup.x ||
        guard->max_groups.y > SIZE_MAX / pipeline->workgroup.y ||
        guard->max_groups.z > SIZE_MAX / pipeline->workgroup.z)
        return doe_mtl_poison(commands, DOE_MTL_RANGE, "indirect dispatch grid", error);
    status = doe_mtl_buffer_preflight(commands, dispatch->arguments, true);
    if (status == DOE_MTL_OK && !doe_mtl_range(dispatch->arguments->size, dispatch->argument_offset, 12))
        status = DOE_MTL_RANGE;
    if (status == DOE_MTL_OK && dispatch->argument_offset % 4) status = DOE_MTL_ALIGNMENT;
    doe_mtl_native_resources resources = {0};
    if (status == DOE_MTL_OK)
        status = doe_mtl_buffer_bindings_preflight(commands, dispatch->buffers,
            dispatch->buffer_count, pipeline->sizes_slot, resources.buffers);
    if (status == DOE_MTL_OK)
        status = doe_mtl_image_bindings_preflight(commands, dispatch->textures, dispatch->texture_count,
            dispatch->samplers, dispatch->sampler_count, &resources);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "indirect dispatch arguments", error);
    status = doe_mtl_use_compute_pipeline(commands, pipeline);
    for (size_t i = 0; status == DOE_MTL_OK && i < dispatch->buffer_count; ++i) {
        doe_mtl_buffer_use *use;
        status = doe_mtl_use_buffer(commands, dispatch->buffers[i].buffer, &use);
    }
    if (status == DOE_MTL_OK)
        status = doe_mtl_image_bindings_retain(commands, dispatch->textures, dispatch->texture_count,
            dispatch->samplers, dispatch->sampler_count);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "indirect dispatch lease", error);
    doe_mtl_buffer *scratch = NULL;
    doe_mtl_error detail = {0};
    status = doe_mtl_buffer_create(commands->context, 12, DOE_MTL_PRIVATE, &scratch, &detail);
    if (status == DOE_MTL_OK)
        status = doe_mtl_commands_copy(commands, dispatch->arguments, dispatch->argument_offset, scratch, 0, 12, &detail);
    const doe_mtl_buffer_binding binding = {guard->binding_slot, scratch, 0, 12};
    if (status == DOE_MTL_OK)
        status = doe_mtl_commands_dispatch(commands, guard->pipeline, &binding, 1,
            (doe_mtl_dimensions){1, 1, 1}, 1, &detail);
    if (status == DOE_MTL_OK) {
        doe_mtl_native *native = &commands->context->native;
        status = native->dispatch_indirect(native->userdata, commands->native, pipeline->native,
            &resources, pipeline->sizes_slot, scratch->native, 0, pipeline->workgroup, &detail);
    }
    /* Full copy initialized scratch speculatively and acquired its command
     * lease. That owner retains it through unknown; failures discard recording. */
    doe_mtl_buffer_release(scratch);
    return doe_mtl_native_record_result(commands, status, &detail, error);
}
