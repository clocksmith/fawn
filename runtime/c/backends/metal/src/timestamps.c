/* SPDX-License-Identifier: MIT */
#include "query_internal.h"

doe_mtl_status doe_mtl_timestamps_create(doe_mtl_context *context, uint32_t count,
    doe_mtl_timestamps **out, doe_mtl_error *error) {
    doe_mtl_timestamps *timestamps;
    doe_mtl_status status;
    if (context == NULL || out == NULL || count == 0)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "timestamp create", error);
    if (count > DOE_MTL_TIMESTAMP_CAPACITY)
        return doe_mtl_fail(DOE_MTL_RANGE, "timestamp capacity", error);
    if ((!context->caps.timestamp_stage && !context->caps.timestamp_blit) ||
        context->native.timestamps == NULL)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "timestamp counters", error);
    timestamps = calloc(1, sizeof(*timestamps));
    if (timestamps == NULL)
        return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "timestamp allocation", error);
    timestamps->available = calloc(count, sizeof(bool));
    if (timestamps->available == NULL) {
        free(timestamps);
        return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "timestamp availability allocation", error);
    }
    status = doe_mtl_context_retain(context);
    if (status != DOE_MTL_OK) {
        free(timestamps->available);
        free(timestamps);
        return doe_mtl_fail(status, "timestamp context lease", error);
    }
    timestamps->context = context;
    timestamps->count = count;
    timestamps->references = 1;
    status = context->native.timestamps(context->native.userdata, context->device,
        count, &timestamps->native, error);
    if (status != DOE_MTL_OK) {
        doe_mtl_timestamps_release(timestamps);
        return status;
    }
    *out = timestamps;
    return doe_mtl_fail(DOE_MTL_OK, "timestamp create", error);
}

doe_mtl_status doe_mtl_timestamps_retain(doe_mtl_timestamps *timestamps) {
    if (timestamps == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (timestamps->references == UINT32_MAX) return DOE_MTL_RANGE;
    timestamps->references++;
    return DOE_MTL_OK;
}

void doe_mtl_timestamps_release(doe_mtl_timestamps *timestamps) {
    doe_mtl_context *context;
    if (timestamps == NULL || --timestamps->references != 0) return;
    context = timestamps->context;
    if (timestamps->native != NULL)
        context->native.release(context->native.userdata, timestamps->native);
    free(timestamps->available);
    free(timestamps);
    doe_mtl_context_release(context);
}

doe_mtl_status doe_mtl_timestamps_preflight(doe_mtl_commands *commands,
    const doe_mtl_timestamps *timestamps, uint32_t index, bool write) {
    const doe_mtl_timestamp_use *use;
    if (timestamps == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (timestamps->context != commands->context) return DOE_MTL_DEVICE_MISMATCH;
    if (timestamps->owner != NULL && timestamps->owner != commands) {
        commands->conflict = timestamps->owner;
        return DOE_MTL_BUSY;
    }
    if (timestamps->poisoned) return DOE_MTL_EXECUTION_FAILED;
    if (index >= timestamps->count) return DOE_MTL_RANGE;
    for (use = commands->timestamps; use != NULL; use = use->next) {
        if (use->timestamps != timestamps) continue;
        if (write) return use->written[index] ? DOE_MTL_INVALID_STATE : DOE_MTL_OK;
        return use->available[index] ? DOE_MTL_OK : DOE_MTL_UNINITIALIZED;
    }
    return write || timestamps->available[index] ? DOE_MTL_OK : DOE_MTL_UNINITIALIZED;
}

doe_mtl_status doe_mtl_use_timestamps(doe_mtl_commands *commands,
    doe_mtl_timestamps *timestamps, doe_mtl_timestamp_use **out) {
    doe_mtl_timestamp_use *use;
    doe_mtl_status status;
    for (use = commands->timestamps; use != NULL; use = use->next) {
        if (use->timestamps == timestamps) {
            *out = use;
            return DOE_MTL_OK;
        }
    }
    use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_MTL_OUT_OF_MEMORY;
    use->available = calloc(timestamps->count, sizeof(bool));
    use->written = calloc(timestamps->count, sizeof(bool));
    status = use->available == NULL || use->written == NULL ?
        DOE_MTL_OUT_OF_MEMORY : doe_mtl_timestamps_retain(timestamps);
    if (status != DOE_MTL_OK) {
        free(use->available);
        free(use->written);
        free(use);
        return status;
    }
    memcpy(use->available, timestamps->available, timestamps->count * sizeof(bool));
    use->timestamps = timestamps;
    use->next = commands->timestamps;
    commands->timestamps = use;
    timestamps->owner = commands;
    *out = use;
    return DOE_MTL_OK;
}

void doe_mtl_timestamps_retire(doe_mtl_commands *commands, bool completed, bool success) {
    while (commands->timestamps != NULL) {
        doe_mtl_timestamp_use *use = commands->timestamps;
        doe_mtl_timestamps *timestamps = use->timestamps;
        commands->timestamps = use->next;
        if (completed) {
            if (success) memcpy(timestamps->available, use->available, timestamps->count * sizeof(bool));
            else timestamps->poisoned = true;
        }
        if (use->resolve_event != NULL)
            commands->context->native.release(commands->context->native.userdata, use->resolve_event);
        timestamps->owner = NULL;
        doe_mtl_timestamps_release(timestamps);
        free(use->available);
        free(use->written);
        free(use);
    }
}

doe_mtl_status doe_mtl_commands_timestamp(doe_mtl_commands *commands,
    doe_mtl_timestamps *timestamps, uint32_t index, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_timestamp_use *use;
    doe_mtl_native *native;
    doe_mtl_error detail = {0};
    if (status != DOE_MTL_OK) return status;
    native = &commands->context->native;
    if (!commands->context->caps.timestamp_blit || native->timestamp == NULL)
        return doe_mtl_poison(commands, DOE_MTL_UNSUPPORTED, "blit timestamp", error);
    status = doe_mtl_timestamps_preflight(commands, timestamps, index, true);
    if (status == DOE_MTL_OK) status = doe_mtl_use_timestamps(commands, timestamps, &use);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "timestamp sample", error);
    status = native->timestamp(native->userdata, commands->native, timestamps->native, index, &detail);
    if (status == DOE_MTL_OK) {
        use->available[index] = true;
        use->written[index] = true;
    }
    return doe_mtl_native_record_result(commands, status, &detail, error);
}

doe_mtl_status doe_mtl_commands_resolve_timestamps(doe_mtl_commands *commands,
    doe_mtl_timestamps *timestamps, uint32_t first, uint32_t count,
    doe_mtl_buffer *destination, uint64_t offset, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_timestamp_use *query_use;
    doe_mtl_buffer_use *buffer_use;
    doe_mtl_native *native;
    doe_mtl_error detail = {0};
    uint32_t i;
    uint64_t size = (uint64_t)count * sizeof(uint64_t);
    if (status != DOE_MTL_OK) return status;
    native = &commands->context->native;
    if (native->resolve_timestamps == NULL)
        return doe_mtl_poison(commands, DOE_MTL_UNSUPPORTED, "timestamp resolve", error);
    if (timestamps == NULL || count == 0 || first > timestamps->count || count > timestamps->count - first)
        return doe_mtl_poison(commands, DOE_MTL_RANGE, "timestamp resolve range", error);
    for (i = 0; i < count; ++i) {
        status = doe_mtl_timestamps_preflight(commands, timestamps, first + i, false);
        if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "timestamp availability", error);
    }
    status = doe_mtl_buffer_preflight(commands, destination, false);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "timestamp destination", error);
    if (offset % 8 != 0)
        return doe_mtl_poison(commands, DOE_MTL_ALIGNMENT, "timestamp destination alignment", error);
    if (!doe_mtl_range(destination->size, offset, size))
        return doe_mtl_poison(commands, DOE_MTL_RANGE, "timestamp destination range", error);
    status = doe_mtl_use_timestamps(commands, timestamps, &query_use);
    if (status == DOE_MTL_OK) status = doe_mtl_use_buffer(commands, destination, &buffer_use);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "timestamp resolve lease", error);
    if (query_use->resolve_value == UINT64_MAX)
        return doe_mtl_poison(commands, DOE_MTL_RANGE, "timestamp resolve sequence", error);
    status = native->resolve_timestamps(native->userdata, commands->native, timestamps->native,
        first, count, destination->native, (size_t)offset, &query_use->resolve_event,
        ++query_use->resolve_value, &detail);
    if (status == DOE_MTL_OK && offset == 0 && size == destination->size)
        buffer_use->initialized = true;
    return doe_mtl_native_record_result(commands, status, &detail, error);
}

doe_mtl_status doe_mtl_timestamps_available(doe_mtl_commands *commands,
    doe_mtl_timestamps *timestamps, uint32_t index, bool *out) {
    if (out == NULL) return DOE_MTL_INVALID_ARGUMENT;
    doe_mtl_status status = doe_mtl_recording(commands, NULL);
    if (status != DOE_MTL_OK) return status;
    status = doe_mtl_timestamps_preflight(commands, timestamps, index, false);
    if (status != DOE_MTL_OK && status != DOE_MTL_UNINITIALIZED) return status;
    *out = status == DOE_MTL_OK;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_commands_ordered_timestamp(doe_mtl_commands *commands,
    doe_mtl_timestamps *timestamps, uint32_t index, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    if (status != DOE_MTL_OK) return status;
    doe_mtl_native *native = &commands->context->native;
    bool stage = commands->context->caps.timestamp_stage;
    if ((!stage && !commands->context->caps.timestamp_blit) || native->timestamp_boundary == NULL)
        return doe_mtl_poison(commands, DOE_MTL_UNSUPPORTED, "stage timestamp", error);
    doe_mtl_timestamp_use *query_use = NULL;
    /* Every boundary first orders all preceding GPU work. Rewriting an index
     * after an earlier resolve therefore preserves that resolve's snapshot. */
    status = doe_mtl_timestamps_preflight(commands, timestamps, index, false);
    if (status == DOE_MTL_UNINITIALIZED) status = DOE_MTL_OK;
    if (status == DOE_MTL_OK) status = doe_mtl_use_timestamps(commands, timestamps, &query_use);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "timestamp sample", error);
    if (query_use->resolve_value == UINT64_MAX)
        return doe_mtl_poison(commands, DOE_MTL_RANGE, "timestamp ordering sequence", error);
    doe_mtl_buffer *scratch = NULL;
    doe_mtl_buffer_use *buffer_use = NULL;
    doe_mtl_error detail = {0};
    if (stage) status = doe_mtl_buffer_create(commands->context, 4, DOE_MTL_PRIVATE, &scratch, &detail);
    if (status == DOE_MTL_OK && stage) status = doe_mtl_use_buffer(commands, scratch, &buffer_use);
    if (status == DOE_MTL_OK) status = native->timestamp_boundary(native->userdata, commands->native,
        timestamps->native, index, stage, scratch ? scratch->native : NULL, &query_use->resolve_event, ++query_use->resolve_value, &detail);
    doe_mtl_buffer_release(scratch);
    if (status == DOE_MTL_OK) {
        query_use->available[index] = true;
        query_use->written[index] = true;
        if (buffer_use) buffer_use->initialized = true;
    }
    return doe_mtl_native_record_result(commands, status, &detail, error);
}
