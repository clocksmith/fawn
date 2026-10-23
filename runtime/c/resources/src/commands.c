/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

doe_resource_status resource_encoder_fail(doe_resource_encoder *encoder, doe_resource_status status)
{
    if (encoder && encoder->status == DOE_RESOURCE_OK) encoder->status = status;
    return encoder ? encoder->status : DOE_RESOURCE_INVALID_ARGUMENT;
}

doe_resource_status doe_resource_encoder_invalidate(doe_resource_encoder *encoder,
    doe_resource_status rejection)
{
    if (rejection <= DOE_RESOURCE_OK || rejection > DOE_RESOURCE_EXECUTION_FAILED ||
        rejection == DOE_RESOURCE_PENDING) return DOE_RESOURCE_INVALID_ARGUMENT;
    return resource_encoder_fail(encoder, rejection);
}

doe_resource_status resource_encoder_open(doe_resource_encoder *encoder)
{
    if (!encoder) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (encoder->status != DOE_RESOURCE_OK) return encoder->status;
    if (encoder->active_pass) return resource_encoder_fail(encoder, DOE_RESOURCE_STATE);
    if (encoder->finished) return resource_encoder_fail(encoder, DOE_RESOURCE_STATE);
    if (!resource_alive(&encoder->base)) return resource_encoder_fail(encoder, DOE_RESOURCE_UNAVAILABLE);
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_encoder_reserve(doe_resource_encoder *encoder)
{
    if (encoder->count < encoder->capacity) return DOE_RESOURCE_OK;
    size_t capacity = encoder->capacity ? encoder->capacity : 8;
    if (encoder->capacity) {
        if (capacity > SIZE_MAX / 2) return resource_encoder_fail(encoder, DOE_RESOURCE_OVERFLOW);
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*encoder->commands)) return resource_encoder_fail(encoder, DOE_RESOURCE_OVERFLOW);
    doe_resource_command *commands = resource_allocate(&encoder->base.config,
        capacity * sizeof(*commands), _Alignof(doe_resource_command));
    if (!commands) return resource_encoder_fail(encoder, DOE_RESOURCE_OUT_OF_MEMORY);
    if (encoder->count) memcpy(commands, encoder->commands, encoder->count * sizeof(*commands));
    resource_free(&encoder->base.config, encoder->commands,
        encoder->capacity * sizeof(*commands), _Alignof(doe_resource_command));
    encoder->commands = commands;
    encoder->capacity = capacity;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_encoder_create(const doe_resource_config *config,
    doe_resource_encoder **out)
{
    if (!out || !resource_config_valid(config)) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity)) return DOE_RESOURCE_UNAVAILABLE;
    doe_resource_encoder *encoder = resource_allocate(config, sizeof(*encoder), _Alignof(doe_resource_encoder));
    if (!encoder) return DOE_RESOURCE_OUT_OF_MEMORY;
    memset(encoder, 0, sizeof(*encoder));
    resource_init(&encoder->base, config, (doe_resource_native){0});
    *out = encoder;
    return DOE_RESOURCE_OK;
}

void resource_command_release(doe_resource_command *command)
{
    switch (command->kind) {
    case DOE_COMMAND_END_OCCLUSION:
    case DOE_COMMAND_BEGIN_OCCLUSION:
    case DOE_COMMAND_WRITE_TIMESTAMP:
        doe_resource_query_set_release(command->value.timestamp.set);
        break;
    case DOE_COMMAND_RESOLVE_QUERY:
        doe_resource_query_set_release(command->value.resolve.set);
        doe_resource_buffer_release(command->value.resolve.destination);
        break;
    case DOE_COMMAND_RENDER:
        resource_render_record_release(command->value.render);
        break;
    case DOE_COMMAND_COPY_TEXTURE:
        doe_resource_texture_release(command->value.blit.source.texture);
        doe_resource_texture_release(command->value.blit.destination.texture);
        break;
    case DOE_COMMAND_COPY_BUFFER:
        doe_resource_buffer_release(command->value.copy.source);
        doe_resource_buffer_release(command->value.copy.destination);
        break;
    case DOE_COMMAND_CLEAR_BUFFER:
        doe_resource_buffer_release(command->value.clear.buffer);
        break;
    case DOE_COMMAND_BUFFER_TEXTURE:
        doe_resource_buffer_release(command->value.texture.buffer);
        doe_resource_texture_release(command->value.texture.texture);
        break;
    case DOE_COMMAND_DISPATCH_INDIRECT:
    case DOE_COMMAND_DISPATCH:
        doe_resource_buffer_release(command->value.dispatch.indirect);
        resource_free(&command->value.dispatch.pipeline->base.config,
            (void *)command->value.dispatch.dynamic_offsets,
            sizeof(doe_resource_dispatch_offsets), _Alignof(doe_resource_dispatch_offsets));
        for (size_t i = 0; i < command->value.dispatch.group_count; i++)
            doe_resource_bind_group_release(command->value.dispatch.groups[i]);
        doe_resource_pipeline_release(command->value.dispatch.pipeline);
        break;
    }
}

void doe_resource_encoder_retain(doe_resource_encoder *encoder)
{
    if (encoder) resource_retain(&encoder->base);
}

void doe_resource_encoder_release(doe_resource_encoder *encoder)
{
    if (!encoder || !resource_last_reference(&encoder->base)) return;
    for (size_t i = 0; i < encoder->count; i++) resource_command_release(&encoder->commands[i]);
    resource_free(&encoder->base.config, encoder->commands,
        encoder->capacity * sizeof(*encoder->commands), _Alignof(doe_resource_command));
    resource_finalize(&encoder->base, sizeof(*encoder), _Alignof(doe_resource_encoder));
}

doe_resource_status doe_resource_encoder_copy_buffer(doe_resource_encoder *encoder,
    const doe_resource_buffer_copy *copy, doe_resource_alias_policy alias)
{
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    uint64_t size;
    status = doe_resource_buffer_copy_validate(encoder->base.config.device.identity, copy, alias, &size);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(encoder, status);
    status = resource_encoder_reserve(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_command command = {.kind = DOE_COMMAND_COPY_BUFFER};
    command.value.copy = *copy;
    command.value.copy.size = size;
    doe_resource_buffer_retain(copy->source);
    doe_resource_buffer_retain(copy->destination);
    encoder->commands[encoder->count++] = command;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_encoder_clear_buffer(doe_resource_encoder *encoder,
    doe_resource_buffer *buffer, uint64_t offset, uint64_t size)
{
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    if (!resource_buffer_available(buffer)) return resource_encoder_fail(encoder, DOE_RESOURCE_UNAVAILABLE);
    if (buffer->base.config.device.identity != encoder->base.config.device.identity)
        return resource_encoder_fail(encoder, DOE_RESOURCE_DEVICE_MISMATCH);
    if (!(buffer->desc.usage & WGPUBufferUsage_CopyDst)) return resource_encoder_fail(encoder, DOE_RESOURCE_USAGE);
    uint64_t resolved;
    status = resource_range(buffer->desc.size, offset, size, &resolved);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(encoder, status);
    if (offset % 4 || resolved % 4) return resource_encoder_fail(encoder, DOE_RESOURCE_ALIGNMENT);
    status = resource_encoder_reserve(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_command command = {.kind = DOE_COMMAND_CLEAR_BUFFER};
    command.value.clear.buffer = buffer;
    command.value.clear.offset = offset;
    command.value.clear.size = resolved;
    doe_resource_buffer_retain(buffer);
    encoder->commands[encoder->count++] = command;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_encoder_copy_texture(doe_resource_encoder *encoder,
    doe_resource_buffer *buffer, doe_resource_texture *texture,
    const doe_resource_texture_copy *copy, doe_resource_copy_direction direction,
    doe_resource_copy_alignment alignment)
{
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    if (!resource_buffer_available(buffer) || !resource_texture_available(texture))
        return resource_encoder_fail(encoder, DOE_RESOURCE_UNAVAILABLE);
    if (buffer->base.config.device.identity != encoder->base.config.device.identity ||
        texture->base.config.device.identity != encoder->base.config.device.identity)
        return resource_encoder_fail(encoder, DOE_RESOURCE_DEVICE_MISMATCH);
    uint64_t buffer_usage = direction == DOE_RESOURCE_BUFFER_TO_TEXTURE ? WGPUBufferUsage_CopySrc : WGPUBufferUsage_CopyDst;
    uint64_t texture_usage = direction == DOE_RESOURCE_BUFFER_TO_TEXTURE ? WGPUTextureUsage_CopyDst : WGPUTextureUsage_CopySrc;
    if (!(buffer->desc.usage & buffer_usage) || !(texture->desc.usage & texture_usage))
        return resource_encoder_fail(encoder, DOE_RESOURCE_USAGE);
    doe_resource_copy_region region;
    status = doe_resource_texture_copy_validate(buffer->desc.size, &texture->desc, copy, direction, alignment, &region);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(encoder, status);
    if (!copy->width || !copy->height || !copy->depth_or_layers) return DOE_RESOURCE_OK;
    status = resource_encoder_reserve(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_command command = {.kind = DOE_COMMAND_BUFFER_TEXTURE};
    command.value.texture.buffer = buffer;
    command.value.texture.texture = texture;
    command.value.texture.copy = *copy;
    command.value.texture.region = region;
    command.value.texture.direction = direction;
    doe_resource_buffer_retain(buffer);
    doe_resource_texture_retain(texture);
    encoder->commands[encoder->count++] = command;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_encoder_blit_texture(doe_resource_encoder *encoder,
    const doe_resource_texture_blit *copy, doe_resource_copy_alignment alignment)
{
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_texture_blit normalized;
    status = doe_resource_texture_blit_validate(encoder->base.config.device.identity, copy, alignment, &normalized);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(encoder, status);
    if (!copy->width || !copy->height || !copy->depth_or_layers) return DOE_RESOURCE_OK;
    status = resource_encoder_reserve(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_command command = {.kind = DOE_COMMAND_COPY_TEXTURE};
    command.value.blit = normalized;
    doe_resource_texture_retain(normalized.source.texture);
    doe_resource_texture_retain(normalized.destination.texture);
    encoder->commands[encoder->count++] = command;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_encoder_dispatch(doe_resource_encoder *encoder,
    doe_resource_pipeline *pipeline, doe_resource_bind_group *const *groups,
    size_t group_count, uint32_t x, uint32_t y, uint32_t z)
{
    if (group_count > DOE_RESOURCE_MAX_GROUPS || (group_count && !groups))
        return resource_encoder_fail(encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    doe_resource_dispatch_group bindings[DOE_RESOURCE_MAX_GROUPS];
    for (size_t i = 0; i < group_count; i++)
        bindings[i] = (doe_resource_dispatch_group){groups[i], NULL, 0};
    return doe_resource_encoder_dispatch_groups(encoder, pipeline, bindings, group_count, x, y, z);
}

doe_resource_status doe_resource_encoder_dispatch_groups(doe_resource_encoder *encoder,
    doe_resource_pipeline *pipeline, const doe_resource_dispatch_group *groups,
    size_t group_count, uint32_t x, uint32_t y, uint32_t z)
{
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    return resource_record_dispatch(encoder, pipeline, groups, group_count, x, y, z);
}

doe_resource_status resource_record_dispatch(doe_resource_encoder *encoder,
    doe_resource_pipeline *pipeline, const doe_resource_dispatch_group *groups,
    size_t group_count, uint32_t x, uint32_t y, uint32_t z)
{
    doe_resource_status status;
    if (!pipeline || (group_count && !groups)) return resource_encoder_fail(encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    if (pipeline->base.config.device.identity != encoder->base.config.device.identity)
        return resource_encoder_fail(encoder, DOE_RESOURCE_DEVICE_MISMATCH);
    if (group_count != pipeline->layout->count) return resource_encoder_fail(encoder, DOE_RESOURCE_LAYOUT);
    bool dynamic = false;
    for (size_t i = 0; i < group_count; i++) {
        doe_resource_binding resolved[DOE_RESOURCE_MAX_BINDINGS];
        status = doe_resource_bind_group_resolve(groups[i].group, groups[i].dynamic_offsets,
            groups[i].dynamic_offset_count, resolved, DOE_RESOURCE_MAX_BINDINGS);
        if (status != DOE_RESOURCE_OK) return resource_encoder_fail(encoder, status);
        if (!doe_resource_layout_compatible(pipeline->layout->groups[i], groups[i].group->layout))
            return resource_encoder_fail(encoder, DOE_RESOURCE_LAYOUT);
        if (groups[i].dynamic_offset_count) dynamic = true;
    }
    doe_resource_dispatch_offsets *offsets = NULL;
    if (dynamic) {
        offsets = resource_allocate(&pipeline->base.config, sizeof(*offsets), _Alignof(doe_resource_dispatch_offsets));
        if (!offsets) return resource_encoder_fail(encoder, DOE_RESOURCE_OUT_OF_MEMORY);
        memset(offsets, 0, sizeof(*offsets));
        for (size_t i = 0; i < group_count; i++) {
            offsets->counts[i] = groups[i].dynamic_offset_count;
            if (offsets->counts[i]) memcpy(offsets->values[i], groups[i].dynamic_offsets,
                offsets->counts[i] * sizeof(uint32_t));
        }
    }
    status = resource_encoder_reserve(encoder);
    if (status != DOE_RESOURCE_OK) {
        resource_free(&pipeline->base.config, offsets, sizeof(*offsets), _Alignof(doe_resource_dispatch_offsets));
        return status;
    }
    doe_resource_command command = {.kind = DOE_COMMAND_DISPATCH};
    command.value.dispatch.pipeline = pipeline;
    command.value.dispatch.group_count = group_count;
    command.value.dispatch.dynamic_offsets = offsets;
    command.value.dispatch.x = x;
    command.value.dispatch.y = y;
    command.value.dispatch.z = z;
    for (size_t i = 0; i < group_count; i++) {
        command.value.dispatch.groups[i] = groups[i].group;
        doe_resource_bind_group_retain(groups[i].group);
    }
    doe_resource_pipeline_retain(pipeline);
    encoder->commands[encoder->count++] = command;
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_record_dispatch_indirect(doe_resource_encoder *encoder,
    doe_resource_pipeline *pipeline, const doe_resource_dispatch_group *groups,
    size_t group_count, doe_resource_buffer *arguments, uint64_t offset)
{
    doe_resource_status status = resource_indirect_validate(encoder->base.config.device.identity, arguments, offset, 12);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(encoder, status);
    status = resource_record_dispatch(encoder, pipeline, groups, group_count, 0, 0, 0);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_command *command = &encoder->commands[encoder->count - 1];
    command->kind = DOE_COMMAND_DISPATCH_INDIRECT;
    command->value.dispatch.indirect = arguments;
    command->value.dispatch.indirect_offset = offset;
    doe_resource_buffer_retain(arguments);
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_encoder_dispatch_indirect(doe_resource_encoder *encoder,
    doe_resource_pipeline *pipeline, const doe_resource_dispatch_group *groups,
    size_t group_count, doe_resource_buffer *arguments, uint64_t offset)
{
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    return resource_record_dispatch_indirect(encoder, pipeline, groups, group_count, arguments, offset);
}

doe_resource_status doe_resource_encoder_finish(doe_resource_encoder *encoder,
    doe_resource_commands **out)
{
    if (!out) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_commands *commands = resource_allocate(&encoder->base.config,
        sizeof(*commands), _Alignof(doe_resource_commands));
    if (!commands) return resource_encoder_fail(encoder, DOE_RESOURCE_OUT_OF_MEMORY);
    resource_init(&commands->base, &encoder->base.config, (doe_resource_native){0});
    atomic_init(&commands->submission_state, 0);
    commands->count = encoder->count;
    commands->capacity = encoder->capacity;
    commands->commands = encoder->commands;
    encoder->count = 0;
    encoder->capacity = 0;
    encoder->commands = NULL;
    encoder->finished = true;
    *out = commands;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_encoder_status(const doe_resource_encoder *encoder)
{
    return encoder ? encoder->status : DOE_RESOURCE_INVALID_ARGUMENT;
}

void doe_resource_commands_retain(doe_resource_commands *commands)
{
    if (commands) resource_retain(&commands->base);
}

void doe_resource_commands_release(doe_resource_commands *commands)
{
    if (!commands || !resource_last_reference(&commands->base)) return;
    for (size_t i = 0; i < commands->count; i++) resource_command_release(&commands->commands[i]);
    resource_free(&commands->base.config, commands->commands,
        commands->capacity * sizeof(*commands->commands), _Alignof(doe_resource_command));
    resource_finalize(&commands->base, sizeof(*commands), _Alignof(doe_resource_commands));
}

const doe_resource_command *doe_resource_commands_borrow(const doe_resource_commands *commands,
    size_t *count)
{
    if (!commands || !count) return NULL;
    *count = commands->count;
    return commands->commands;
}

static doe_resource_status submit_buffer(const doe_resource_buffer *buffer)
{
    if (!resource_buffer_available(buffer)) return DOE_RESOURCE_UNAVAILABLE;
    return buffer->mapped || buffer->map_pending ? DOE_RESOURCE_STATE : DOE_RESOURCE_OK;
}

static doe_resource_status validate_submission(const doe_resource_command *command)
{
    doe_resource_status status;
    switch (command->kind) {
    case DOE_COMMAND_END_OCCLUSION:
    case DOE_COMMAND_BEGIN_OCCLUSION:
    case DOE_COMMAND_WRITE_TIMESTAMP:
        return resource_query_available(command->value.timestamp.set) ? DOE_RESOURCE_OK : DOE_RESOURCE_UNAVAILABLE;
    case DOE_COMMAND_RESOLVE_QUERY:
        if (!resource_query_available(command->value.resolve.set)) return DOE_RESOURCE_UNAVAILABLE;
        return submit_buffer(command->value.resolve.destination);
    case DOE_COMMAND_RENDER:
        return resource_render_record_validate(command->value.render);
    case DOE_COMMAND_COPY_TEXTURE:
        return resource_texture_available(command->value.blit.source.texture) &&
            resource_texture_available(command->value.blit.destination.texture) ? DOE_RESOURCE_OK : DOE_RESOURCE_UNAVAILABLE;
    case DOE_COMMAND_COPY_BUFFER:
        status = submit_buffer(command->value.copy.source);
        if (status != DOE_RESOURCE_OK) return status;
        return submit_buffer(command->value.copy.destination);
    case DOE_COMMAND_CLEAR_BUFFER:
        return submit_buffer(command->value.clear.buffer);
    case DOE_COMMAND_BUFFER_TEXTURE:
        status = submit_buffer(command->value.texture.buffer);
        if (status != DOE_RESOURCE_OK) return status;
        return resource_texture_available(command->value.texture.texture) ? DOE_RESOURCE_OK : DOE_RESOURCE_UNAVAILABLE;
    case DOE_COMMAND_DISPATCH_INDIRECT:
    case DOE_COMMAND_DISPATCH:
        if (command->value.dispatch.indirect) {
            status = submit_buffer(command->value.dispatch.indirect);
            if (status != DOE_RESOURCE_OK) return status;
        }
        if (!resource_alive(&command->value.dispatch.pipeline->base)) return DOE_RESOURCE_UNAVAILABLE;
        for (size_t i = 0; i < command->value.dispatch.group_count; i++) {
            status = resource_group_validate(command->value.dispatch.groups[i]);
            if (status != DOE_RESOURCE_OK) return status;
        }
        return DOE_RESOURCE_OK;
    }
    return DOE_RESOURCE_UNSUPPORTED;
}

doe_resource_status doe_resource_commands_begin_submit(doe_resource_commands *commands)
{
    if (!commands) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!resource_alive(&commands->base)) return DOE_RESOURCE_UNAVAILABLE;
    if (atomic_load_explicit(&commands->submission_state, memory_order_acquire) != 0)
        return DOE_RESOURCE_STATE;
    for (size_t i = 0; i < commands->count; i++) {
        doe_resource_status status = validate_submission(&commands->commands[i]);
        if (status != DOE_RESOURCE_OK) return status;
    }
    unsigned expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&commands->submission_state, &expected, 1,
            memory_order_acq_rel, memory_order_acquire)) return DOE_RESOURCE_STATE;
    doe_resource_commands_retain(commands);
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_commands_settle(doe_resource_commands *commands,
    doe_resource_status terminal_status)
{
    if (!commands) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (terminal_status == DOE_RESOURCE_PENDING)
        return atomic_load_explicit(&commands->submission_state, memory_order_acquire) == 1 ?
            DOE_RESOURCE_PENDING : DOE_RESOURCE_STATE;
    if (terminal_status != DOE_RESOURCE_OK && terminal_status != DOE_RESOURCE_EXECUTION_FAILED)
        return DOE_RESOURCE_INVALID_ARGUMENT;
    unsigned expected = 1;
    if (!atomic_compare_exchange_strong_explicit(&commands->submission_state, &expected, 2,
            memory_order_acq_rel, memory_order_acquire)) return DOE_RESOURCE_STATE;
    doe_resource_commands_release(commands);
    return terminal_status;
}
