/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"
#include "query_internal.h"

static doe_mtl_status use_pipeline(doe_mtl_commands *commands,
    doe_mtl_pipeline *pipeline) {
    doe_mtl_pipeline_use *use;
    doe_mtl_status status;
    for (use = commands->pipelines; use != NULL; use = use->next)
        if (use->pipeline == pipeline) return DOE_MTL_OK;
    use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_MTL_OUT_OF_MEMORY;
    status = doe_mtl_pipeline_retain(pipeline);
    if (status != DOE_MTL_OK) {
        free(use);
        return status;
    }
    use->pipeline = pipeline;
    use->next = commands->pipelines;
    commands->pipelines = use;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_commands_compute(doe_mtl_commands *commands,
    const doe_mtl_compute_dispatch *dispatch, doe_mtl_error *error) {
    doe_mtl_pipeline *pipeline;
    const doe_mtl_buffer_binding *bindings;
    const doe_mtl_texture_binding *textures;
    const doe_mtl_sampler_binding *samplers;
    size_t count, texture_count, sampler_count;
    doe_mtl_dimensions groups;
    uint32_t repeat;
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_native_resources resources = {0};
    doe_mtl_error detail = {0};
    doe_mtl_native *native;
    doe_mtl_timestamp_use *timestamp_use = NULL;
    doe_mtl_native_timestamps timestamp_pair = {0};
    bool empty;
    size_t i;
    if (status != DOE_MTL_OK) return status;
    if (dispatch == NULL) return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "compute descriptor", error);
    pipeline = dispatch->pipeline;
    bindings = dispatch->buffers;
    count = dispatch->buffer_count;
    textures = dispatch->textures;
    texture_count = dispatch->texture_count;
    samplers = dispatch->samplers;
    sampler_count = dispatch->sampler_count;
    groups = dispatch->groups;
    repeat = dispatch->repeat;
    if (pipeline == NULL || count > DOE_MTL_BUFFER_SLOTS || (count != 0 && bindings == NULL))
        return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "dispatch arguments", error);
    if (pipeline->context != commands->context)
        return doe_mtl_poison(commands, DOE_MTL_DEVICE_MISMATCH, "dispatch pipeline", error);
    status = doe_mtl_buffer_bindings_preflight(commands, bindings, count, pipeline->sizes_slot, resources.buffers);
    if (status == DOE_MTL_OK)
        status = doe_mtl_image_bindings_preflight(commands, textures, texture_count,
            samplers, sampler_count, &resources);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "dispatch binding", error);
    empty = groups.x == 0 || groups.y == 0 || groups.z == 0 || repeat == 0;
    if (dispatch->timestamps != NULL) {
        if (!commands->context->caps.timestamp_stage)
            return doe_mtl_poison(commands, DOE_MTL_UNSUPPORTED, "compute stage timestamps", error);
        if (dispatch->timestamp_begin == dispatch->timestamp_end)
            return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "timestamp pair alias", error);
        status = doe_mtl_timestamps_preflight(commands, dispatch->timestamps, dispatch->timestamp_begin, true);
        if (status == DOE_MTL_OK)
            status = doe_mtl_timestamps_preflight(commands, dispatch->timestamps, dispatch->timestamp_end, true);
        if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "compute timestamps", error);
        timestamp_pair = (doe_mtl_native_timestamps){dispatch->timestamps->native,
            dispatch->timestamp_begin, dispatch->timestamp_end};
    } else if (empty) return doe_mtl_fail(DOE_MTL_OK, "empty dispatch", error);
    /* Metal forms grid extents in NSUInteger. Preserve checked arithmetic on
     * every supported host, even though the macOS driver requires 64-bit ABI. */
    if (groups.x > SIZE_MAX / pipeline->workgroup.x ||
        groups.y > SIZE_MAX / pipeline->workgroup.y ||
        groups.z > SIZE_MAX / pipeline->workgroup.z)
        return doe_mtl_poison(commands, DOE_MTL_RANGE, "dispatch grid", error);
    status = use_pipeline(commands, pipeline);
    for (i = 0; status == DOE_MTL_OK && i < count; ++i) {
        doe_mtl_buffer_use *use;
        status = doe_mtl_use_buffer(commands, bindings[i].buffer, &use);
    }
    if (status == DOE_MTL_OK)
        status = doe_mtl_image_bindings_retain(commands, textures, texture_count, samplers, sampler_count);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "dispatch lease", error);
    if (dispatch->timestamps != NULL)
        status = doe_mtl_use_timestamps(commands, dispatch->timestamps, &timestamp_use);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "timestamp lease", error);
    native = &commands->context->native;
    status = native->dispatch(native->userdata, commands->native, pipeline->native,
        &resources, pipeline->sizes_slot, groups, pipeline->workgroup, empty ? 0 : repeat,
        dispatch->timestamps != NULL ? &timestamp_pair : NULL, &detail);
    if (status == DOE_MTL_OK && timestamp_use != NULL) {
        timestamp_use->available[timestamp_pair.begin] = true;
        timestamp_use->available[timestamp_pair.end] = true;
        timestamp_use->written[timestamp_pair.begin] = true;
        timestamp_use->written[timestamp_pair.end] = true;
    }
    return doe_mtl_native_record_result(commands, status, &detail, error);
}

doe_mtl_status doe_mtl_commands_dispatch_resources(doe_mtl_commands *commands,
    doe_mtl_pipeline *pipeline, const doe_mtl_buffer_binding *bindings, size_t count,
    const doe_mtl_texture_binding *textures, size_t texture_count,
    const doe_mtl_sampler_binding *samplers, size_t sampler_count,
    doe_mtl_dimensions groups, uint32_t repeat, doe_mtl_error *error) {
    const doe_mtl_compute_dispatch dispatch = {pipeline, bindings, count, textures,
        texture_count, samplers, sampler_count, groups, repeat, NULL, 0, 0};
    return doe_mtl_commands_compute(commands, &dispatch, error);
}

doe_mtl_status doe_mtl_commands_dispatch(doe_mtl_commands *commands,
    doe_mtl_pipeline *pipeline, const doe_mtl_buffer_binding *bindings,
    size_t count, doe_mtl_dimensions groups, uint32_t repeat, doe_mtl_error *error) {
    return doe_mtl_commands_dispatch_resources(commands, pipeline, bindings, count,
        NULL, 0, NULL, 0, groups, repeat, error);
}

