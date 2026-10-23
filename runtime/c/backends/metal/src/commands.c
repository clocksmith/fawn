/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_mtl_status doe_mtl_recording(doe_mtl_commands *commands, doe_mtl_error *error) {
    if (commands == NULL)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "command recording", error);
    if (commands->state == DOE_MTL_POISONED) {
        if (error != NULL) *error = commands->failure;
        return commands->failure.status;
    }
    if (commands->state != DOE_MTL_RECORDING)
        return doe_mtl_fail(DOE_MTL_INVALID_STATE, "command recording", error);
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_poison(doe_mtl_commands *commands, doe_mtl_status status,
    const char *operation, doe_mtl_error *error) {
    if (commands != NULL && commands->state == DOE_MTL_RECORDING) {
        commands->state = DOE_MTL_POISONED;
        doe_mtl_fail(status, operation, &commands->failure);
    }
    if (commands != NULL && commands->state == DOE_MTL_POISONED) {
        if (error != NULL) *error = commands->failure;
        return commands->failure.status;
    }
    return doe_mtl_fail(status, operation, error);
}

static doe_mtl_status native_record_result(doe_mtl_commands *commands,
    doe_mtl_status status, const doe_mtl_error *detail, doe_mtl_error *error) {
    if (status != DOE_MTL_OK) {
        commands->state = DOE_MTL_POISONED;
        commands->failure = *detail;
        commands->failure.status = status;
    }
    if (error != NULL) *error = *detail;
    return status;
}

doe_mtl_status doe_mtl_commands_create(doe_mtl_context *context,
    doe_mtl_commands **out, doe_mtl_error *error) {
    doe_mtl_commands *commands;
    doe_mtl_status status;
    if (context == NULL || out == NULL)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "command create", error);
    commands = calloc(1, sizeof(*commands));
    if (commands == NULL)
        return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "command allocation", error);
    status = doe_mtl_context_retain(context);
    if (status != DOE_MTL_OK) {
        free(commands);
        return doe_mtl_fail(status, "command context lease", error);
    }
    commands->context = context;
    commands->state = DOE_MTL_RECORDING;
    status = context->native.command(context->native.userdata, context->queue,
        &commands->native, error);
    if (status != DOE_MTL_OK) {
        (void)doe_mtl_commands_release(commands);
        return status;
    }
    *out = commands;
    return doe_mtl_fail(DOE_MTL_OK, "command create", error);
}

static doe_mtl_status buffer_preflight(const doe_mtl_commands *commands,
    const doe_mtl_buffer *buffer, bool readable) {
    if (buffer == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (buffer->context != commands->context) return DOE_MTL_DEVICE_MISMATCH;
    if (buffer->owner != NULL && buffer->owner != commands) return DOE_MTL_BUSY;
    if (buffer->poisoned) return DOE_MTL_EXECUTION_FAILED;
    if (readable && !doe_mtl_buffer_initialized(commands, buffer))
        return DOE_MTL_UNINITIALIZED;
    return DOE_MTL_OK;
}

bool doe_mtl_buffer_initialized(const doe_mtl_commands *commands,
    const doe_mtl_buffer *buffer) {
    const doe_mtl_buffer_use *use;
    for (use = commands->buffers; use != NULL; use = use->next) {
        if (use->buffer == buffer) return use->initialized;
    }
    return buffer->initialized;
}

doe_mtl_status doe_mtl_use_buffer(doe_mtl_commands *commands,
    doe_mtl_buffer *buffer, doe_mtl_buffer_use **out) {
    doe_mtl_buffer_use *use;
    doe_mtl_status status;
    for (use = commands->buffers; use != NULL; use = use->next) {
        if (use->buffer == buffer) {
            *out = use;
            return DOE_MTL_OK;
        }
    }
    use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_MTL_OUT_OF_MEMORY;
    status = doe_mtl_buffer_retain(buffer);
    if (status != DOE_MTL_OK) {
        free(use);
        return status;
    }
    use->buffer = buffer;
    use->initialized = buffer->initialized;
    use->next = commands->buffers;
    commands->buffers = use;
    buffer->owner = commands;
    *out = use;
    return DOE_MTL_OK;
}

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

doe_mtl_status doe_mtl_commands_copy(doe_mtl_commands *commands,
    doe_mtl_buffer *source, uint64_t source_offset, doe_mtl_buffer *destination,
    uint64_t destination_offset, uint64_t size, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_buffer_use *source_use, *destination_use;
    doe_mtl_error detail = {0};
    doe_mtl_native *native;
    if (status != DOE_MTL_OK) return status;
    status = buffer_preflight(commands, source, size != 0);
    if (status == DOE_MTL_OK) status = buffer_preflight(commands, destination, false);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "copy buffer", error);
    if (!doe_mtl_range(source->size, source_offset, size) ||
        !doe_mtl_range(destination->size, destination_offset, size))
        return doe_mtl_poison(commands, DOE_MTL_RANGE, "copy range", error);
    if ((source_offset | destination_offset | size) % 4 != 0)
        return doe_mtl_poison(commands, DOE_MTL_ALIGNMENT, "copy alignment", error);
    if (source == destination && size != 0 &&
        source_offset < destination_offset + size && destination_offset < source_offset + size)
        return doe_mtl_poison(commands, DOE_MTL_RANGE, "overlapping copy", error);
    if (size == 0) return doe_mtl_fail(DOE_MTL_OK, "empty copy", error);
    status = doe_mtl_use_buffer(commands, source, &source_use);
    if (status == DOE_MTL_OK)
        status = doe_mtl_use_buffer(commands, destination, &destination_use);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "copy lease", error);
    native = &commands->context->native;
    status = native->copy(native->userdata, commands->native, source->native,
        (size_t)source_offset, destination->native, (size_t)destination_offset,
        (size_t)size, &detail);
    if (status == DOE_MTL_OK && destination_offset == 0 && size == destination->size)
        destination_use->initialized = true;
    return native_record_result(commands, status, &detail, error);
}

doe_mtl_status doe_mtl_commands_fill(doe_mtl_commands *commands,
    doe_mtl_buffer *destination, uint64_t offset, uint64_t size, uint8_t value,
    doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_buffer_use *use;
    doe_mtl_native *native;
    doe_mtl_error detail = {0};
    if (status != DOE_MTL_OK) return status;
    status = buffer_preflight(commands, destination, false);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "fill buffer", error);
    if (!doe_mtl_range(destination->size, offset, size))
        return doe_mtl_poison(commands, DOE_MTL_RANGE, "fill range", error);
    if ((offset | size) % 4 != 0)
        return doe_mtl_poison(commands, DOE_MTL_ALIGNMENT, "fill alignment", error);
    if (size == 0) return doe_mtl_fail(DOE_MTL_OK, "empty fill", error);
    status = doe_mtl_use_buffer(commands, destination, &use);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "fill lease", error);
    native = &commands->context->native;
    status = native->fill(native->userdata, commands->native, destination->native,
        (size_t)offset, (size_t)size, value, &detail);
    if (status == DOE_MTL_OK && offset == 0 && size == destination->size)
        use->initialized = true;
    return native_record_result(commands, status, &detail, error);
}

static doe_mtl_status binding_preflight(doe_mtl_commands *commands,
    doe_mtl_pipeline *pipeline, const doe_mtl_buffer_binding *bindings,
    size_t count, doe_mtl_native_binding *flat) {
    size_t i;
    for (i = 0; i < count; ++i) {
        const doe_mtl_buffer_binding *binding = &bindings[i];
        doe_mtl_status status;
        if (binding->slot >= DOE_MTL_BUFFER_SLOTS || binding->slot == pipeline->sizes_slot)
            return DOE_MTL_RANGE;
        if (flat[binding->slot].buffer != NULL) return DOE_MTL_INVALID_ARGUMENT;
        status = buffer_preflight(commands, binding->buffer, true);
        if (status != DOE_MTL_OK) return status;
        if (binding->size == 0 ||
            !doe_mtl_range(binding->buffer->size, binding->offset, binding->size))
            return DOE_MTL_RANGE;
        if (binding->offset % 4 != 0) return DOE_MTL_ALIGNMENT;
        if (pipeline->sizes_slot != UINT32_MAX &&
            (binding->size > UINT32_MAX || binding->slot > pipeline->sizes_slot))
            return DOE_MTL_RANGE;
        flat[binding->slot].buffer = binding->buffer->native;
        flat[binding->slot].offset = (size_t)binding->offset;
        flat[binding->slot].size = binding->size <= UINT32_MAX ? (uint32_t)binding->size : 0;
    }
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_commands_dispatch(doe_mtl_commands *commands,
    doe_mtl_pipeline *pipeline, const doe_mtl_buffer_binding *bindings,
    size_t count, doe_mtl_dimensions groups, uint32_t repeat, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_native_binding flat[DOE_MTL_BUFFER_SLOTS] = {{0}};
    doe_mtl_error detail = {0};
    doe_mtl_native *native;
    size_t i;
    if (status != DOE_MTL_OK) return status;
    if (pipeline == NULL || count > DOE_MTL_BUFFER_SLOTS || (count != 0 && bindings == NULL))
        return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "dispatch arguments", error);
    if (pipeline->context != commands->context)
        return doe_mtl_poison(commands, DOE_MTL_DEVICE_MISMATCH, "dispatch pipeline", error);
    status = binding_preflight(commands, pipeline, bindings, count, flat);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "dispatch binding", error);
    if (groups.x == 0 || groups.y == 0 || groups.z == 0 || repeat == 0)
        return doe_mtl_fail(DOE_MTL_OK, "empty dispatch", error);
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
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "dispatch lease", error);
    native = &commands->context->native;
    status = native->dispatch(native->userdata, commands->native, pipeline->native,
        flat, pipeline->sizes_slot, groups, pipeline->workgroup, repeat, &detail);
    return native_record_result(commands, status, &detail, error);
}

doe_mtl_status doe_mtl_commands_dispatch_indirect(doe_mtl_commands *commands,
    doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    if (status != DOE_MTL_OK) return status;
    return doe_mtl_poison(commands, DOE_MTL_UNSUPPORTED, "indirect dispatch", error);
}

static void retire_uses(doe_mtl_commands *commands, bool completed, bool success) {
    while (commands->buffers != NULL) {
        doe_mtl_buffer_use *use = commands->buffers;
        commands->buffers = use->next;
        if (completed) {
            if (success) use->buffer->initialized = use->initialized;
            else use->buffer->poisoned = true;
        }
        use->buffer->owner = NULL;
        doe_mtl_buffer_release(use->buffer);
        free(use);
    }
    while (commands->pipelines != NULL) {
        doe_mtl_pipeline_use *use = commands->pipelines;
        commands->pipelines = use->next;
        doe_mtl_pipeline_release(use->pipeline);
        free(use);
    }
}

doe_mtl_status doe_mtl_commands_submit(doe_mtl_commands *commands, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_native *native;
    if (status != DOE_MTL_OK) return status;
    native = &commands->context->native;
    commands->state = DOE_MTL_SUBMITTED;
    native->commit(native->userdata, commands->native);
    return doe_mtl_fail(DOE_MTL_OK, "command commit", error);
}

static doe_mtl_status observe(doe_mtl_commands *commands, bool wait,
    doe_mtl_error *error) {
    doe_mtl_native *native;
    size_t status;
    int64_t code = 0;
    if (commands == NULL)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "command completion", error);
    if (commands->state == DOE_MTL_COMPLETE)
        return doe_mtl_fail(DOE_MTL_OK, "command completion", error);
    if (commands->state == DOE_MTL_FAILED || commands->state == DOE_MTL_POISONED) {
        if (error != NULL) *error = commands->failure;
        return commands->failure.status;
    }
    if (commands->state != DOE_MTL_SUBMITTED)
        return doe_mtl_fail(DOE_MTL_INVALID_STATE, "unsubmitted completion", error);
    native = &commands->context->native;
    status = native->observe(native->userdata, commands->native, wait, &code);
    if (status == 4) {
        commands->state = DOE_MTL_COMPLETE;
        retire_uses(commands, true, true);
        return doe_mtl_fail(DOE_MTL_OK, "command completion", error);
    }
    if (status == 5) {
        commands->state = DOE_MTL_FAILED;
        doe_mtl_fail(DOE_MTL_EXECUTION_FAILED, "command completion", &commands->failure);
        commands->failure.native_code = code;
        retire_uses(commands, true, false);
        if (error != NULL) *error = commands->failure;
        return DOE_MTL_EXECUTION_FAILED;
    }
    if (!wait && (status == 2 || status == 3))
        return doe_mtl_fail(DOE_MTL_PENDING, "command completion", error);
    return doe_mtl_fail(DOE_MTL_COMPLETION_UNKNOWN, "command completion", error);
}

doe_mtl_status doe_mtl_commands_poll(doe_mtl_commands *commands, doe_mtl_error *error) {
    return observe(commands, false, error);
}

doe_mtl_status doe_mtl_commands_wait(doe_mtl_commands *commands, doe_mtl_error *error) {
    return observe(commands, true, error);
}

doe_mtl_status doe_mtl_commands_release(doe_mtl_commands *commands) {
    doe_mtl_context *context;
    if (commands == NULL) return DOE_MTL_OK;
    if (commands->state == DOE_MTL_SUBMITTED) return DOE_MTL_BUSY;
    context = commands->context;
    /* Discard the native recording before dropping its unretained resources.
     * Submitted commands reach this path only after a terminal observation. */
    if (commands->native != NULL)
        context->native.release(context->native.userdata, commands->native);
    retire_uses(commands, false, false);
    free(commands);
    doe_mtl_context_release(context);
    return DOE_MTL_OK;
}
