/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"
#include "query_internal.h"

doe_mtl_status doe_mtl_use_compute_pipeline(doe_mtl_commands *commands,
    doe_mtl_pipeline *pipeline) {
    doe_mtl_pipeline_use *use;
    doe_mtl_status status;
    if (pipeline->last_commands == commands) return DOE_MTL_OK;
    for (use = commands->pipelines; use != NULL; use = use->next)
        if (use->pipeline == pipeline) { pipeline->last_commands = commands; return DOE_MTL_OK; }
    use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_MTL_OUT_OF_MEMORY;
    status = doe_mtl_pipeline_retain(pipeline);
    if (status != DOE_MTL_OK) {
        free(use);
        return status;
    }
    pipeline->last_commands = commands;
    use->pipeline = pipeline;
    use->next = commands->pipelines;
    commands->pipelines = use;
    return DOE_MTL_OK;
}

static void clear_compute_slots(const doe_mtl_render_bindings *bindings, doe_mtl_native_resources *native) {
    size_t i;
    for (i = 0; i < bindings->buffer_count && bindings->buffers != NULL; ++i)
        if (bindings->buffers[i].slot < DOE_MTL_BUFFER_SLOTS)
            native->buffers[bindings->buffers[i].slot] = (doe_mtl_native_binding){0};
    for (i = 0; i < bindings->texture_count && bindings->textures != NULL; ++i)
        if (bindings->textures[i].slot < DOE_MTL_TEXTURE_SLOTS)
            native->textures[bindings->textures[i].slot] = NULL;
    for (i = 0; i < bindings->sampler_count && bindings->samplers != NULL; ++i)
        if (bindings->samplers[i].slot < DOE_MTL_SAMPLER_SLOTS)
            native->samplers[bindings->samplers[i].slot] = NULL;
    native->immediate_size = 0;
    native->texture_count = 0;
    native->sampler_count = 0;
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
    doe_mtl_pass_timestamps pass_timestamps = {0};
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
    if (status == DOE_MTL_OK)
        status = doe_mtl_immediates_preflight(dispatch->immediates, pipeline->sizes_slot, &resources);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "dispatch binding", error);
    empty = groups.x == 0 || groups.y == 0 || groups.z == 0 || repeat == 0;
    if (dispatch->timestamps != NULL) {
        if (!commands->context->caps.timestamp_stage)
            return doe_mtl_poison(commands, DOE_MTL_UNSUPPORTED, "compute stage timestamps", error);
        pass_timestamps = (doe_mtl_pass_timestamps){dispatch->timestamps,
            dispatch->timestamp_begin, dispatch->timestamp_end};
        status = doe_mtl_pass_timestamps_preflight(commands, &pass_timestamps, true);
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
    status = doe_mtl_use_compute_pipeline(commands, pipeline);
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
        doe_mtl_pass_timestamps_publish(timestamp_use, &pass_timestamps);
    }
    return doe_mtl_native_record_result(commands, status, &detail, error);
}

doe_mtl_status doe_mtl_commands_dispatch_resources(doe_mtl_commands *commands,
    doe_mtl_pipeline *pipeline, const doe_mtl_buffer_binding *bindings, size_t count,
    const doe_mtl_texture_binding *textures, size_t texture_count,
    const doe_mtl_sampler_binding *samplers, size_t sampler_count,
    doe_mtl_dimensions groups, uint32_t repeat, doe_mtl_error *error) {
    const doe_mtl_compute_dispatch dispatch = {pipeline, bindings, count, textures,
        texture_count, samplers, sampler_count, groups, repeat, NULL, 0, 0, {0}};
    return doe_mtl_commands_compute(commands, &dispatch, error);
}

doe_mtl_status doe_mtl_commands_dispatch(doe_mtl_commands *commands,
    doe_mtl_pipeline *pipeline, const doe_mtl_buffer_binding *bindings,
    size_t count, doe_mtl_dimensions groups, uint32_t repeat, doe_mtl_error *error) {
    return doe_mtl_commands_dispatch_resources(commands, pipeline, bindings, count,
        NULL, 0, NULL, 0, groups, repeat, error);
}

doe_mtl_status doe_mtl_commands_compute_batch(doe_mtl_commands *commands,
    const doe_mtl_compute_dispatch *dispatches, size_t count, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_native_compute_dispatch *steps = NULL;
    doe_mtl_native_resources scratch = {0};
    doe_mtl_error detail = {0};
    doe_mtl_native *native;
    const char *fault_op = "dispatch binding";
    size_t d, i, active = 0;
    if (status != DOE_MTL_OK) return status;
    if (count == 0) return doe_mtl_fail(DOE_MTL_OK, "empty dispatch", error);
    if (dispatches == NULL || count > SIZE_MAX / sizeof(*steps))
        return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "compute descriptor", error);
    steps = calloc(count, sizeof(*steps));
    if (steps == NULL)
        return doe_mtl_poison(commands, DOE_MTL_OUT_OF_MEMORY, "compute batch snapshot", error);
    for (d = 0; d < count; ++d) {
        const doe_mtl_compute_dispatch *step = &dispatches[d];
        doe_mtl_pipeline *pipeline = step->pipeline;
        doe_mtl_render_bindings current = {step->buffers, step->buffer_count,
            step->textures, step->texture_count, step->samplers, step->sampler_count, step->immediates};
        bool empty, same_previous;
        if (step->timestamps != NULL) { status = DOE_MTL_INVALID_ARGUMENT; fault_op = "compute timestamps"; break; }
        if (pipeline == NULL || step->buffer_count > DOE_MTL_BUFFER_SLOTS ||
            (step->buffer_count != 0 && step->buffers == NULL)) {
            status = DOE_MTL_INVALID_ARGUMENT; fault_op = "dispatch arguments"; break;
        }
        if (pipeline->context != commands->context) {
            status = DOE_MTL_DEVICE_MISMATCH; fault_op = "dispatch pipeline"; break;
        }
        same_previous = d != 0 && pipeline == dispatches[d - 1].pipeline &&
            doe_mtl_render_bindings_same(&current, &steps[d - 1].resources);
        if (!same_previous) {
            clear_compute_slots(&current, &scratch);
            status = doe_mtl_buffer_bindings_preflight(commands, step->buffers, step->buffer_count,
                pipeline->sizes_slot, scratch.buffers);
            if (status == DOE_MTL_OK)
                status = doe_mtl_image_bindings_preflight(commands, step->textures, step->texture_count,
                    step->samplers, step->sampler_count, &scratch);
            if (status == DOE_MTL_OK)
                status = doe_mtl_immediates_preflight(step->immediates, pipeline->sizes_slot, &scratch);
            if (status != DOE_MTL_OK) { fault_op = "dispatch binding"; break; }
        }
        empty = step->groups.x == 0 || step->groups.y == 0 || step->groups.z == 0 || step->repeat == 0;
        if (!empty && (step->groups.x > SIZE_MAX / pipeline->workgroup.x ||
            step->groups.y > SIZE_MAX / pipeline->workgroup.y ||
            step->groups.z > SIZE_MAX / pipeline->workgroup.z)) {
            status = DOE_MTL_RANGE; fault_op = "dispatch grid"; break;
        }
        steps[d] = (doe_mtl_native_compute_dispatch){pipeline->native, current,
            pipeline->sizes_slot, empty ? 0 : step->repeat, step->groups, pipeline->workgroup};
        if (!empty) ++active;
    }
    if (status != DOE_MTL_OK) {
        free(steps);
        return doe_mtl_poison(commands, status, fault_op, error);
    }
    if (active == 0) {
        free(steps);
        return doe_mtl_fail(DOE_MTL_OK, "empty dispatch", error);
    }
    for (d = 0; d < count && status == DOE_MTL_OK; ++d) {
        const doe_mtl_compute_dispatch *step = &dispatches[d];
        if (steps[d].repeat == 0) continue;
        status = doe_mtl_use_compute_pipeline(commands, step->pipeline);
        for (i = 0; status == DOE_MTL_OK && i < step->buffer_count; ++i) {
            doe_mtl_buffer_use *use;
            status = doe_mtl_use_buffer(commands, step->buffers[i].buffer, &use);
        }
        if (status == DOE_MTL_OK)
            status = doe_mtl_image_bindings_retain(commands, step->textures, step->texture_count,
                step->samplers, step->sampler_count);
    }
    if (status != DOE_MTL_OK) {
        free(steps);
        return doe_mtl_poison(commands, status, "dispatch lease", error);
    }
    native = &commands->context->native;
    if (native->dispatch_batch != NULL) {
        status = native->dispatch_batch(native->userdata, commands->native, steps, count, &detail);
    } else {
        for (d = 0; d < count && status == DOE_MTL_OK; ++d) {
            if (steps[d].repeat == 0) continue;
            clear_compute_slots(&steps[d].resources, &scratch);
            status = doe_mtl_buffer_bindings_preflight(commands, steps[d].resources.buffers,
                steps[d].resources.buffer_count, steps[d].sizes_slot, scratch.buffers);
            if (status == DOE_MTL_OK)
                status = doe_mtl_image_bindings_preflight(commands, steps[d].resources.textures,
                    steps[d].resources.texture_count, steps[d].resources.samplers,
                    steps[d].resources.sampler_count, &scratch);
            if (status == DOE_MTL_OK)
                status = doe_mtl_immediates_preflight(steps[d].resources.immediates,
                    steps[d].sizes_slot, &scratch);
            if (status == DOE_MTL_OK)
                status = native->dispatch(native->userdata, commands->native, steps[d].pipeline,
                    &scratch, steps[d].sizes_slot, steps[d].groups, steps[d].workgroup,
                    steps[d].repeat, NULL, &detail);
        }
    }
    free(steps);
    return doe_mtl_native_record_result(commands, status, &detail, error);
}
