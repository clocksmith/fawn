/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* Shared graphics/compute resource rules: validation only, no publication. */
doe_dx_status doe_dx_prepare_resources(doe_dx_commands *commands, doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources, doe_dx_native_resources *native,
    size_t *added, doe_dx_error *error) {
    if (!pipeline || !resources)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.resources", error);
    doe_dx_status status;
    const doe_dx_buffer_binding *bindings = resources->buffers;
    size_t binding_count = resources->buffer_count;
    if ((binding_count && !bindings) || (resources->texture_count && !resources->textures))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.bindings", error);
    if (pipeline->context != commands->context)
        return doe_dx_fail(DOE_DX_DEVICE_MISMATCH, 0, "commands.bindings", error);
    if (binding_count != pipeline->binding_count || resources->texture_count != pipeline->texture_count)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.bindings.bindings", error);
    bool pipeline_used = false;
    for (size_t index = 0; index < commands->pipeline_count; index++)
        if (commands->pipelines[index] == pipeline) pipeline_used = true;
    if (!pipeline_used && (commands->pipeline_count == DOE_DX_MAX_COMMAND_RESOURCES ||
        pipeline->references == UINT32_MAX))
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.pipeline_budget", error);
    native->buffer_count = binding_count;
    native->texture_count = resources->texture_count;
    *added = 0;
    for (size_t index = 0; index < binding_count; index++) {
        const doe_dx_buffer_binding *binding = &bindings[index];
        doe_dx_buffer *buffer = binding->buffer;
        status = doe_dx_buffer_preflight(commands, buffer, true);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.bindings.buffer", error);
        if (buffer->memory != DOE_DX_DEFAULT)
            return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.bindings.heap", error);
        if (!binding->size || !doe_dx_range(buffer->size, binding->offset, binding->size))
            return doe_dx_fail(DOE_DX_RANGE, 0, "commands.bindings.range", error);
        uint32_t stride = pipeline->bindings[index].stride;
        if (binding->offset % stride || binding->size % stride)
            return doe_dx_fail(DOE_DX_ALIGNMENT, 0, "commands.bindings.stride", error);
        if (binding->size / stride > UINT32_MAX)
            return doe_dx_fail(DOE_DX_RANGE, 0, "commands.bindings.elements", error);
        for (size_t prior = 0; prior < index; prior++)
            if (bindings[prior].buffer == buffer)
                return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.bindings.alias", error);
        if (!doe_dx_find_buffer(commands, buffer)) (*added)++;
        native->buffers[index] = (doe_dx_native_binding){buffer->native, binding->offset, binding->size};
    }
    for (size_t index = 0; index < resources->texture_count; index++) {
        doe_dx_texture *texture = resources->textures[index];
        const doe_dx_texture_binding_layout *layout = &pipeline->textures[index];
        uint32_t usage = layout->access == DOE_DX_READ_ONLY ? DOE_DX_TEXTURE_SAMPLED : DOE_DX_TEXTURE_STORAGE;
        status = doe_dx_texture_preflight(commands, texture, usage, true);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.bindings.texture", error);
        if (texture->desc.format != layout->format)
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.bindings.texture_format", error);
        for (size_t prior = 0; prior < index; prior++)
            if (resources->textures[prior] == texture)
                return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.bindings.texture_alias", error);
        if (!doe_dx_find_texture(commands, texture)) (*added)++;
        native->textures[index] = texture->native;
    }
    if (*added > DOE_DX_MAX_COMMAND_RESOURCES - commands->buffer_count - commands->texture_count - commands->timestamp_count)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.resource_budget", error);
    return DOE_DX_OK;
}

void doe_dx_use_resources(doe_dx_commands *commands, doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources) {
    for (size_t index = 0; index < resources->buffer_count; index++)
        doe_dx_use_buffer(commands, resources->buffers[index].buffer);
    for (size_t index = 0; index < resources->texture_count; index++)
        doe_dx_use_texture(commands, resources->textures[index]);
    for (size_t index = 0; index < commands->pipeline_count; index++)
        if (commands->pipelines[index] == pipeline) return;
    pipeline->references++;
    commands->pipelines[commands->pipeline_count++] = pipeline;
}
