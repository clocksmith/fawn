/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_dx_status doe_dx_commands_dispatch_resources(doe_dx_commands *commands,
    doe_dx_pipeline *pipeline, const doe_dx_compute_resources *resources,
    uint32_t x, uint32_t y, uint32_t z, doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    if (!pipeline || !resources || !x || !y || !z)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.dispatch", error);
    const doe_dx_buffer_binding *bindings = resources->buffers;
    size_t binding_count = resources->buffer_count;
    if ((binding_count && !bindings) || (resources->texture_count && !resources->textures))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.dispatch", error);
    if (pipeline->context != commands->context)
        return doe_dx_fail(DOE_DX_DEVICE_MISMATCH, 0, "commands.dispatch", error);
    if (binding_count != pipeline->binding_count || resources->texture_count != pipeline->texture_count)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.dispatch.bindings", error);
    if (x > 65535 || y > 65535 || z > 65535 || commands->operation_count == 65536)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.dispatch", error);
    bool pipeline_used = false;
    for (size_t index = 0; index < commands->pipeline_count; index++)
        if (commands->pipelines[index] == pipeline) pipeline_used = true;
    if (!pipeline_used && (commands->pipeline_count == DOE_DX_MAX_COMMAND_RESOURCES ||
        pipeline->references == UINT32_MAX))
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.pipeline_budget", error);
    doe_dx_native_resources native = {0};
    native.buffer_count = binding_count;
    native.texture_count = resources->texture_count;
    size_t added = 0;
    for (size_t index = 0; index < binding_count; index++) {
        const doe_dx_buffer_binding *binding = &bindings[index];
        doe_dx_buffer *buffer = binding->buffer;
        status = doe_dx_buffer_preflight(commands, buffer, true);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.dispatch.buffer", error);
        if (buffer->memory != DOE_DX_DEFAULT)
            return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.dispatch.heap", error);
        if (!binding->size || !doe_dx_range(buffer->size, binding->offset, binding->size))
            return doe_dx_fail(DOE_DX_RANGE, 0, "commands.dispatch.range", error);
        uint32_t stride = pipeline->bindings[index].stride;
        if (binding->offset % stride || binding->size % stride)
            return doe_dx_fail(DOE_DX_ALIGNMENT, 0, "commands.dispatch.stride", error);
        if (binding->size / stride > UINT32_MAX)
            return doe_dx_fail(DOE_DX_RANGE, 0, "commands.dispatch.elements", error);
        for (size_t prior = 0; prior < index; prior++)
            if (bindings[prior].buffer == buffer)
                return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.dispatch.alias", error);
        if (!doe_dx_find_buffer(commands, buffer)) added++;
        native.buffers[index] = (doe_dx_native_binding){buffer->native, binding->offset, binding->size};
    }
    for (size_t index = 0; index < resources->texture_count; index++) {
        doe_dx_texture *texture = resources->textures[index];
        const doe_dx_texture_binding_layout *layout = &pipeline->textures[index];
        uint32_t usage = layout->access == DOE_DX_READ_ONLY ? DOE_DX_TEXTURE_SAMPLED : DOE_DX_TEXTURE_STORAGE;
        status = doe_dx_texture_preflight(commands, texture, usage, true);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.dispatch.texture", error);
        if (texture->desc.format != layout->format)
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.dispatch.texture_format", error);
        for (size_t prior = 0; prior < index; prior++)
            if (resources->textures[prior] == texture)
                return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.dispatch.texture_alias", error);
        if (!doe_dx_find_texture(commands, texture)) added++;
        native.textures[index] = texture->native;
    }
    if (added > DOE_DX_MAX_COMMAND_RESOURCES - commands->buffer_count - commands->texture_count)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.resource_budget", error);
    /* Native preparation may fail, but performs no command mutation until all
     * descriptor allocation succeeds. The caller still owns all inputs here. */
    status = doe_dx_native_dispatch(commands->native, pipeline->native, &native, x, y, z, error);
    if (status != DOE_DX_OK) return doe_dx_context_result(commands->context, status, error);
    for (size_t index = 0; index < binding_count; index++)
        doe_dx_use_buffer(commands, bindings[index].buffer);
    for (size_t index = 0; index < resources->texture_count; index++)
        doe_dx_use_texture(commands, resources->textures[index]);
    if (!pipeline_used) {
        pipeline->references++;
        commands->pipelines[commands->pipeline_count++] = pipeline;
    }
    commands->operation_count++;
    return doe_dx_fail(DOE_DX_OK, 0, "commands.dispatch", error);
}

doe_dx_status doe_dx_commands_dispatch(doe_dx_commands *commands,
    doe_dx_pipeline *pipeline, const doe_dx_buffer_binding *bindings,
    size_t binding_count, uint32_t x, uint32_t y, uint32_t z, doe_dx_error *error) {
    doe_dx_compute_resources resources = {bindings, binding_count, NULL, 0};
    return doe_dx_commands_dispatch_resources(commands, pipeline, &resources, x, y, z, error);
}
