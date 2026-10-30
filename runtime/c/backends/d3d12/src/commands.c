/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_dx_status doe_dx_commands_create(doe_dx_context *context,
    doe_dx_commands **out, doe_dx_error *error) {
    if (!context || !out) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.create", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "commands.create", error);
    if (context->objects == SIZE_MAX) return doe_dx_fail(DOE_DX_RANGE, 0, "commands.create", error);
    doe_dx_commands *commands = calloc(1, sizeof(*commands));
    if (!commands) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "commands.allocate", error);
    doe_dx_status status = doe_dx_native_commands_create(context->native, &commands->native, error);
    if (status != DOE_DX_OK) {
        free(commands);
        return doe_dx_context_result(context, status, error);
    }
    commands->context = context;
    commands->caller_owned = true;
    commands->state = DOE_DX_RECORDING;
    context->objects++;
    *out = commands;
    return doe_dx_fail(DOE_DX_OK, 0, "commands.create", error);
}

static void release_resources(doe_dx_commands *commands, bool publish) {
    for (size_t index = 0; index < commands->buffer_count; index++) {
        doe_dx_buffer_use *use = &commands->buffers[index];
        if (use->buffer->owner == commands) use->buffer->owner = NULL;
        if (use->buffer->pending == commands) {
            use->buffer->pending = NULL;
            use->buffer->accepted_initialized = false;
        }
        /* Initialization is monotonic; retirement may visit newer fences first. */
        if (publish && use->initialized) use->buffer->initialized = true;
        doe_dx_buffer_release(use->buffer);
    }
    commands->buffer_count = 0;
    for (size_t index = 0; index < commands->texture_count; index++) {
        doe_dx_texture_use *use = &commands->textures[index];
        use->texture->owner = NULL;
        if (publish) {
            use->texture->initialized = use->initialized;
            if (use->surface_acquire) doe_dx_surface_acquisition_complete(use->texture);
        }
        doe_dx_texture_release(use->texture);
    }
    commands->texture_count = 0;
    for (size_t index = 0; index < commands->timestamp_count; index++) {
        doe_dx_timestamp_use *use = &commands->timestamps[index];
        use->timestamps->owner = NULL;
        if (publish) {
            use->timestamps->initialized = use->initialized;
            memcpy(use->timestamps->initialized_upper,use->initialized_upper,sizeof(use->initialized_upper));
        }
        doe_dx_timestamps_release(use->timestamps);
    }
    commands->timestamp_count = 0;
    for (size_t index = 0; index < commands->pipeline_count; index++)
        doe_dx_pipeline_release(commands->pipelines[index]);
    commands->pipeline_count = 0;
}

/* Every command retains its existing buffer references through retirement.
 * A known fence establishes acceptance on this context's one ordered queue;
 * publish only future GPU-readable initialization, never CPU completion. */
void doe_dx_commands_accept_buffers(doe_dx_commands *commands) {
    if (commands->state != DOE_DX_SUBMITTED || !commands->fence) abort();
    for (size_t index = 0; index < commands->buffer_count; ++index) {
        doe_dx_buffer_use *use = &commands->buffers[index];
        if (use->buffer->owner != commands) abort();
        use->buffer->owner = NULL;
        use->buffer->pending = commands;
        use->buffer->accepted_initialized = use->initialized;
    }
}

void doe_dx_commands_dispose(doe_dx_commands *commands) {
    doe_dx_native_commands_destroy(commands->native);
    doe_dx_surface_retire_present(commands,false,false);
    release_resources(commands, false);
    commands->context->objects--;
    free(commands->buffers);
    free(commands->textures);
    free(commands->timestamps);
    free(commands->pipelines);
    free(commands);
}

void doe_dx_commands_settle(doe_dx_commands *commands, bool lost, const doe_dx_error *error) {
    bool present_failed=commands->present_failure.status!=DOE_DX_OK;
    commands->state = lost || present_failed ? DOE_DX_FAILED : DOE_DX_COMPLETE;
    if (present_failed) commands->failure=commands->present_failure;
    else if (lost && error) commands->failure = *error;
    if (lost) doe_dx_native_commands_destroy(commands->native);
    else doe_dx_native_commands_recycle(commands->native);
    commands->native = NULL;
    doe_dx_surface_retire_present(commands,true,!lost&&!present_failed);
    release_resources(commands, !lost);
}

void doe_dx_commands_release(doe_dx_commands *commands) {
    if (!commands) return;
    commands->caller_owned = false;
    if (commands->state != DOE_DX_SUBMITTED) doe_dx_commands_dispose(commands);
}

doe_dx_status doe_dx_recording(doe_dx_commands *commands, doe_dx_error *error) {
    if (!commands) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.record", error);
    if (commands->state == DOE_DX_POISONED) {
        if (error) *error = commands->failure;
        return commands->failure.status;
    }
    if (commands->state != DOE_DX_RECORDING)
        return doe_dx_fail(DOE_DX_INVALID_STATE, 0, "commands.record", error);
    if (commands->context->lost)
        return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "commands.record", error);
    return DOE_DX_OK;
}

doe_dx_buffer_use *doe_dx_find_buffer(doe_dx_commands *commands, doe_dx_buffer *buffer) {
    if (!buffer || buffer->owner != commands) return NULL;
    size_t index = buffer->owner_index;
    if (index >= commands->buffer_count || commands->buffers[index].buffer != buffer) abort();
    return &commands->buffers[index];
}

doe_dx_status doe_dx_buffer_preflight(doe_dx_commands *commands,
    doe_dx_buffer *buffer, bool readable) {
    if (!buffer) return DOE_DX_INVALID_ARGUMENT;
    if (buffer->context != commands->context) return DOE_DX_DEVICE_MISMATCH;
    if (buffer->lent) return DOE_DX_INVALID_STATE; /* Host-lent bytes exclude GPU use. */
    if (buffer->owner && buffer->owner != commands) {
        commands->conflict = buffer->owner;
        return DOE_DX_BUSY;
    }
    doe_dx_buffer_use *use = doe_dx_find_buffer(commands, buffer);
    if (!use && buffer->references == UINT32_MAX) return DOE_DX_RANGE;
    bool initialized = buffer->pending ? buffer->accepted_initialized : buffer->initialized;
    if (readable && !(use ? use->initialized : initialized)) return DOE_DX_UNINITIALIZED;
    return DOE_DX_OK;
}

void doe_dx_use_buffer(doe_dx_commands *commands, doe_dx_buffer *buffer) {
    if (doe_dx_find_buffer(commands, buffer)) return;
    if (commands->buffer_count >= commands->buffer_capacity) abort();
    buffer->owner_index = commands->buffer_count;
    doe_dx_buffer_use *use = &commands->buffers[commands->buffer_count++];
    use->buffer = buffer;
    use->initialized = buffer->pending ? buffer->accepted_initialized : buffer->initialized;
    buffer->owner = commands;
    buffer->references++; /* Preflight checked overflow and capacity. */
}

static doe_dx_status copy_bytes(doe_dx_commands *commands,
    doe_dx_buffer *source, uint64_t source_offset, doe_dx_buffer *destination,
    uint64_t destination_offset, uint64_t size, bool aligned,doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    status = doe_dx_buffer_preflight(commands, source, true);
    if (status == DOE_DX_OK) status = doe_dx_buffer_preflight(commands, destination, false);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.copy", error);
    if (source == destination || !size)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.copy", error);
    if (source->memory == DOE_DX_READBACK || destination->memory == DOE_DX_UPLOAD)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.copy.heap", error);
    if (!doe_dx_range(source->size, source_offset, size) ||
        !doe_dx_range(destination->size, destination_offset, size))
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.copy", error);
    if (aligned && ((source_offset | destination_offset | size) & 3))
        return doe_dx_fail(DOE_DX_ALIGNMENT, 0, "commands.copy", error);
    size_t added = (doe_dx_find_buffer(commands, source) ? 0u : 1u) +
        (doe_dx_find_buffer(commands, destination) ? 0u : 1u);
    status = doe_dx_reserve_uses(commands, (doe_dx_reservation){.buffers=added}, error);
    if (status != DOE_DX_OK) return status;
    doe_dx_use_buffer(commands, source);
    doe_dx_use_buffer(commands, destination);
    doe_dx_native_copy(commands->native, source->native, source_offset,
        destination->native, destination_offset, size);
    if (!destination_offset && size == destination->size)
        doe_dx_find_buffer(commands, destination)->initialized = true;
    return doe_dx_fail(DOE_DX_OK, 0, "commands.copy", error);
}

doe_dx_status doe_dx_commands_copy(doe_dx_commands *commands,doe_dx_buffer *source,uint64_t from,
    doe_dx_buffer *destination,uint64_t to,uint64_t size,doe_dx_error *error)
{
    return copy_bytes(commands,source,from,destination,to,size,true,error);
}
doe_dx_status doe_dx_commands_copy_bytes(doe_dx_commands *commands,doe_dx_buffer *source,uint64_t from,
    doe_dx_buffer *destination,uint64_t to,uint64_t size,doe_dx_error *error)
{
    return copy_bytes(commands,source,from,destination,to,size,false,error);
}

doe_dx_status doe_dx_commands_order_buffer_writes(doe_dx_commands *commands, doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    /* READBACK state is fixed at COPY_DEST: nothing can order its writes here. */
    for (size_t index = 0; index < commands->buffer_count; ++index)
        if (commands->buffers[index].buffer->memory == DOE_DX_READBACK)
            return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.order.fixed_state", error);
    doe_dx_native_buffer *chunk[16];
    size_t count = 0;
    for (size_t index = 0; index < commands->buffer_count; ++index) {
        doe_dx_buffer *buffer = commands->buffers[index].buffer;
        if (buffer->memory != DOE_DX_DEFAULT) continue;
        chunk[count++] = buffer->native;
        if (count == 16) {
            doe_dx_native_order_buffers(commands->native, chunk, count);
            count = 0;
        }
    }
    if (count) doe_dx_native_order_buffers(commands->native, chunk, count);
    return doe_dx_fail(DOE_DX_OK, 0, "commands.order", error);
}

doe_dx_status doe_dx_commands_submit(doe_dx_commands *commands, doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    doe_dx_context *context = commands->context;
    for (doe_dx_commands *item = context->pending; item; item = item->next)
        if (!item->fence) return doe_dx_fail(DOE_DX_BUSY, 0, "commands.unsignaled", error);
    if (context->next_fence == UINT64_MAX - 1)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.fence_budget", error);
    doe_dx_error detail = {0};
    status = doe_dx_native_finish(commands->native, &detail);
    if (status != DOE_DX_OK) {
        commands->state = DOE_DX_POISONED;
        commands->failure = detail;
        if (error) *error = detail;
        return doe_dx_context_result(context, status, &detail);
    }
    /* Publish retirement ownership before ExecuteCommandLists, a void API. */
    commands->state = DOE_DX_SUBMITTED;
    commands->next = context->pending;
    context->pending = commands;
    context->next_fence++;
    doe_dx_native_execute(context->native, commands->native);
    doe_dx_error presentation={0};
    doe_dx_status presented=doe_dx_surface_execute_present(commands,&presentation);
    if (presented!=DOE_DX_OK) {
        presentation.status=presented;
        commands->present_failure=presentation;
    }
    status = doe_dx_native_signal(context->native, context->next_fence, &detail);
    if (status == DOE_DX_OK) {
        commands->fence = context->next_fence;
        doe_dx_commands_accept_buffers(commands);
        if (presented!=DOE_DX_OK) {
            if (presented==DOE_DX_DEVICE_LOST) doe_dx_context_lose(context,&presentation);
            if (error) *error=presentation;
            return presented;
        }
        return doe_dx_fail(DOE_DX_OK, 0, "commands.submit", error);
    }
    if (status == DOE_DX_DEVICE_LOST) {
        doe_dx_error first=presented!=DOE_DX_OK ? presentation : detail;
        doe_dx_context_lose(context, &detail);
        if (error) *error = first;
        return first.status;
    }
    if (commands->present_surface && commands->present_failure.status==DOE_DX_OK)
        commands->present_failure=detail;
    const doe_dx_error *first=commands->present_failure.status!=DOE_DX_OK ? &commands->present_failure : &detail;
    return doe_dx_fail(DOE_DX_COMPLETION_UNKNOWN, first->native_code, first->operation, error);
}

doe_dx_status doe_dx_commands_result(const doe_dx_commands *commands, doe_dx_error *error) {
    if (!commands) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.result", error);
    if (commands->state == DOE_DX_FAILED || commands->state == DOE_DX_POISONED) {
        if (error) *error = commands->failure;
        return commands->failure.status;
    }
    doe_dx_status status = DOE_DX_INVALID_STATE;
    if (commands->state == DOE_DX_COMPLETE) status = DOE_DX_OK;
    if (commands->state == DOE_DX_SUBMITTED)
        status = commands->fence ? DOE_DX_PENDING : DOE_DX_COMPLETION_UNKNOWN;
    return doe_dx_fail(status, 0, "commands.result", error);
}
