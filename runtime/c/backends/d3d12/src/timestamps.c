/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_dx_status doe_dx_timestamps_create(doe_dx_context *context, uint32_t count,
    doe_dx_timestamps **out, doe_dx_error *error) {
    if (!context || !out || !count) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "timestamps.create", error);
    if (count > 64 || context->objects == SIZE_MAX) return doe_dx_fail(DOE_DX_RANGE, 0, "timestamps.count", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "timestamps.create", error);
    doe_dx_timestamps *timestamps = calloc(1, sizeof(*timestamps));
    if (!timestamps) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "timestamps.allocate", error);
    doe_dx_status status = doe_dx_native_timestamps_create(context->native, count, &timestamps->native, error);
    if (status != DOE_DX_OK) {
        free(timestamps);
        return doe_dx_context_result(context, status, error);
    }
    timestamps->context = context;
    timestamps->count = count;
    timestamps->references = 1;
    context->objects++;
    *out = timestamps;
    return doe_dx_fail(DOE_DX_OK, 0, "timestamps.create", error);
}

doe_dx_status doe_dx_timestamps_retain(doe_dx_timestamps *timestamps) {
    if (!timestamps) return DOE_DX_INVALID_ARGUMENT;
    if (timestamps->references == UINT32_MAX) return DOE_DX_RANGE;
    timestamps->references++;
    return DOE_DX_OK;
}

void doe_dx_timestamps_release(doe_dx_timestamps *timestamps) {
    if (!timestamps) return;
    timestamps->references--;
    if (timestamps->references) return;
    doe_dx_native_timestamps_destroy(timestamps->native);
    timestamps->context->objects--;
    free(timestamps);
}

doe_dx_status doe_dx_timestamp_frequency(doe_dx_context *context, uint64_t *out, doe_dx_error *error) {
    if (!context || !out) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "timestamp.frequency", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "timestamp.frequency", error);
    uint64_t frequency = 0;
    doe_dx_status status = doe_dx_native_timestamp_frequency(context->native, &frequency, error);
    if (status != DOE_DX_OK) return doe_dx_context_result(context, status, error);
    if (!frequency) return doe_dx_fail(DOE_DX_UNAVAILABLE, 0, "timestamp.frequency.zero", error);
    *out = frequency;
    return doe_dx_fail(DOE_DX_OK, 0, "timestamp.frequency", error);
}

static doe_dx_timestamp_use *find_timestamps(doe_dx_commands *commands, doe_dx_timestamps *timestamps) {
    for (size_t index = 0; index < commands->timestamp_count; index++)
        if (commands->timestamps[index].timestamps == timestamps) return &commands->timestamps[index];
    return NULL;
}

static doe_dx_status preflight(doe_dx_commands *commands, doe_dx_timestamps *timestamps) {
    if (!timestamps) return DOE_DX_INVALID_ARGUMENT;
    if (timestamps->context != commands->context) return DOE_DX_DEVICE_MISMATCH;
    if (timestamps->owner && timestamps->owner != commands) return DOE_DX_BUSY;
    if (!find_timestamps(commands, timestamps) && timestamps->references == UINT32_MAX) return DOE_DX_RANGE;
    return DOE_DX_OK;
}

static doe_dx_timestamp_use *use_timestamps(doe_dx_commands *commands, doe_dx_timestamps *timestamps) {
    doe_dx_timestamp_use *use = find_timestamps(commands, timestamps);
    if (use) return use;
    use = &commands->timestamps[commands->timestamp_count++];
    use->timestamps = timestamps;
    use->initialized = timestamps->initialized;
    timestamps->owner = commands;
    timestamps->references++;
    return use;
}

doe_dx_status doe_dx_commands_timestamp(doe_dx_commands *commands,
    doe_dx_timestamps *timestamps, uint32_t index, doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    status = preflight(commands, timestamps);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.timestamp", error);
    if (index >= timestamps->count || commands->operation_count == 65536 ||
        (!find_timestamps(commands, timestamps) && commands->buffer_count + commands->texture_count +
        commands->timestamp_count == DOE_DX_MAX_COMMAND_RESOURCES))
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.timestamp.range", error);
    doe_dx_timestamp_use *use = use_timestamps(commands, timestamps);
    doe_dx_native_timestamp(commands->native, timestamps->native, index);
    use->initialized |= UINT64_C(1) << index;
    commands->operation_count++;
    return doe_dx_fail(DOE_DX_OK, 0, "commands.timestamp", error);
}

doe_dx_status doe_dx_commands_resolve_timestamps(doe_dx_commands *commands,
    doe_dx_timestamps *timestamps, uint32_t first, uint32_t count,
    doe_dx_buffer *destination, uint64_t offset, doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    status = preflight(commands, timestamps);
    if (status == DOE_DX_OK) status = doe_dx_buffer_preflight(commands, destination, false);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.resolve", error);
    if (!count || first > timestamps->count || count > timestamps->count - first)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.resolve.slots", error);
    if (destination->memory != DOE_DX_READBACK)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.resolve.heap", error);
    if (offset % 8) return doe_dx_fail(DOE_DX_ALIGNMENT, 0, "commands.resolve.offset", error);
    uint64_t bytes = (uint64_t)count * 8;
    if (!doe_dx_range(destination->size, offset, bytes))
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.resolve.capacity", error);
    doe_dx_timestamp_use *use = find_timestamps(commands, timestamps);
    uint64_t initialized = use ? use->initialized : timestamps->initialized;
    uint64_t mask = count == 64 ? UINT64_MAX : ((UINT64_C(1) << count) - 1) << first;
    if ((initialized & mask) != mask)
        return doe_dx_fail(DOE_DX_UNINITIALIZED, 0, "commands.resolve.unwritten", error);
    bool whole = !offset && bytes == destination->size;
    if (!whole) {
        status = doe_dx_buffer_preflight(commands, destination, true);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.resolve.partial", error);
    }
    size_t added = (use ? 0u : 1u) + (doe_dx_find_buffer(commands, destination) ? 0u : 1u);
    if (commands->operation_count == 65536 ||
        added > DOE_DX_MAX_COMMAND_RESOURCES - commands->buffer_count - commands->texture_count - commands->timestamp_count)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.resolve.budget", error);
    use_timestamps(commands, timestamps);
    doe_dx_use_buffer(commands, destination);
    doe_dx_native_resolve_timestamps(commands->native, timestamps->native, first, count, destination->native, offset);
    if (whole) doe_dx_find_buffer(commands, destination)->initialized = true;
    commands->operation_count++;
    return doe_dx_fail(DOE_DX_OK, 0, "commands.resolve", error);
}
