/* SPDX-License-Identifier: MIT */
#include "internal.h"

static doe_dx_status create_queries(doe_dx_context *context,doe_dx_query_kind kind,uint32_t maximum,uint32_t count,
    doe_dx_timestamps **out, doe_dx_error *error) {
    if (!context || !out || !count) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "timestamps.create", error);
    if (kind!=DOE_DX_QUERY_TIMESTAMP && kind!=DOE_DX_QUERY_OCCLUSION)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"queries.kind",error);
    if (count > maximum || context->objects == SIZE_MAX) return doe_dx_fail(DOE_DX_RANGE, 0, "timestamps.count", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "timestamps.create", error);
    doe_dx_timestamps *timestamps = calloc(1, sizeof(*timestamps));
    if (!timestamps) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "timestamps.allocate", error);
    doe_dx_status status = doe_dx_native_timestamps_create(context->native, kind, count, &timestamps->native, error);
    if (status != DOE_DX_OK) {
        free(timestamps);
        return doe_dx_context_result(context, status, error);
    }
    timestamps->context = context;
    timestamps->count = count;
    timestamps->kind = kind;
    timestamps->references = 1;
    context->objects++;
    *out = timestamps;
    return doe_dx_fail(DOE_DX_OK, 0, "timestamps.create", error);
}

doe_dx_status doe_dx_timestamps_create(doe_dx_context *context,uint32_t count,doe_dx_timestamps **out,doe_dx_error *error)
{
    return create_queries(context,DOE_DX_QUERY_TIMESTAMP,64,count,out,error);
}
doe_dx_status doe_dx_queries_create(doe_dx_context *context,doe_dx_query_kind kind,uint32_t count,
    doe_dx_queries **out,doe_dx_error *error)
{
    return create_queries(context,kind,DOE_DX_MAX_QUERIES,count,out,error);
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
    if (!timestamps || timestamps->owner != commands) return NULL;
    size_t index = timestamps->owner_index;
    if (index >= commands->timestamp_count || commands->timestamps[index].timestamps != timestamps) abort();
    return &commands->timestamps[index];
}

static doe_dx_status preflight(doe_dx_commands *commands, doe_dx_timestamps *timestamps) {
    if (!timestamps) return DOE_DX_INVALID_ARGUMENT;
    if (timestamps->context != commands->context) return DOE_DX_DEVICE_MISMATCH;
    if (timestamps->owner && timestamps->owner != commands) {
        commands->conflict = timestamps->owner;
        return DOE_DX_BUSY;
    }
    if (!find_timestamps(commands, timestamps) && timestamps->references == UINT32_MAX) return DOE_DX_RANGE;
    return DOE_DX_OK;
}

static doe_dx_timestamp_use *use_timestamps(doe_dx_commands *commands, doe_dx_timestamps *timestamps) {
    doe_dx_timestamp_use *use = find_timestamps(commands, timestamps);
    if (use) return use;
    if (commands->timestamp_count >= commands->timestamp_capacity) abort();
    timestamps->owner_index = commands->timestamp_count;
    use = &commands->timestamps[commands->timestamp_count++];
    use->timestamps = timestamps;
    use->initialized = timestamps->initialized;
    memcpy(use->initialized_upper,timestamps->initialized_upper,sizeof(use->initialized_upper));
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
    if (timestamps->kind!=DOE_DX_QUERY_TIMESTAMP)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"commands.timestamp.kind",error);
    if (index >= timestamps->count)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.timestamp.range", error);
    doe_dx_reservation needed = {.timestamps=find_timestamps(commands,timestamps) ? 0u : 1u};
    status = doe_dx_reserve_uses(commands, needed, error);
    if (status != DOE_DX_OK) return status;
    doe_dx_timestamp_use *use = use_timestamps(commands, timestamps);
    doe_dx_native_timestamp(commands->native, timestamps->native, index);
    if (index<64) use->initialized |= UINT64_C(1) << index;
    else use->initialized_upper[index/64-1] |= UINT64_C(1) << (index%64);
    return doe_dx_fail(DOE_DX_OK, 0, "commands.timestamp", error);
}

static doe_dx_status resolve_queries(doe_dx_commands *commands,bool allow_default,
    doe_dx_timestamps *timestamps, uint32_t first, uint32_t count,
    doe_dx_buffer *destination, uint64_t offset, doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    status = preflight(commands, timestamps);
    if (status == DOE_DX_OK) status = doe_dx_buffer_preflight(commands, destination, false);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.resolve", error);
    if (!count || first > timestamps->count || count > timestamps->count - first)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.resolve.slots", error);
    if (destination->memory != DOE_DX_READBACK && (!allow_default || destination->memory != DOE_DX_DEFAULT))
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.resolve.heap", error);
    if (offset % 8) return doe_dx_fail(DOE_DX_ALIGNMENT, 0, "commands.resolve.offset", error);
    uint64_t bytes = (uint64_t)count * 8;
    if (!doe_dx_range(destination->size, offset, bytes))
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.resolve.capacity", error);
    doe_dx_timestamp_use *use = find_timestamps(commands, timestamps);
    for (uint32_t i=first;i<first+count;++i) {
        uint64_t word=i<64 ? (use ? use->initialized : timestamps->initialized) :
            use ? use->initialized_upper[i/64-1] : timestamps->initialized_upper[i/64-1];
        if (!(word & (UINT64_C(1) << (i%64))))
            return doe_dx_fail(DOE_DX_UNINITIALIZED,0,"commands.resolve.unwritten",error);
    }
    bool whole = !offset && bytes == destination->size;
    if (!whole) {
        status = doe_dx_buffer_preflight(commands, destination, true);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.resolve.partial", error);
    }
    doe_dx_reservation needed = {.buffers=doe_dx_find_buffer(commands,destination) ? 0u : 1u,
        .timestamps=use ? 0u : 1u};
    status = doe_dx_reserve_uses(commands, needed, error);
    if (status != DOE_DX_OK) return status;
    use_timestamps(commands, timestamps);
    doe_dx_use_buffer(commands, destination);
    doe_dx_native_resolve_timestamps(commands->native, timestamps->native, first, count, destination->native, offset);
    if (whole) doe_dx_find_buffer(commands, destination)->initialized = true;
    return doe_dx_fail(DOE_DX_OK, 0, "commands.resolve", error);
}


doe_dx_status doe_dx_commands_resolve_timestamps(doe_dx_commands *commands,doe_dx_timestamps *timestamps,
    uint32_t first,uint32_t count,doe_dx_buffer *destination,uint64_t offset,doe_dx_error *error)
{
    return resolve_queries(commands,false,timestamps,first,count,destination,offset,error);
}
doe_dx_status doe_dx_commands_resolve_queries(doe_dx_commands *commands,doe_dx_queries *queries,
    uint32_t first,uint32_t count,doe_dx_buffer *destination,uint64_t offset,doe_dx_error *error)
{
    return resolve_queries(commands,true,queries,first,count,destination,offset,error);
}
doe_dx_status doe_dx_queries_available(doe_dx_commands *commands,doe_dx_queries *queries,uint32_t index,bool *out)
{
    if (!commands || !out) return DOE_DX_INVALID_ARGUMENT;
    doe_dx_status status=doe_dx_recording(commands,NULL);
    if (status==DOE_DX_OK) status=preflight(commands,queries);
    if (status!=DOE_DX_OK) return status;
    if (index>=queries->count) return DOE_DX_RANGE;
    const doe_dx_timestamp_use *use=find_timestamps(commands,queries);
    uint64_t word=index<64 ? (use ? use->initialized : queries->initialized) :
        use ? use->initialized_upper[index/64-1] : queries->initialized_upper[index/64-1];
    *out=(word & (UINT64_C(1) << (index%64)))!=0;
    return DOE_DX_OK;
}
doe_dx_status doe_dx_render_query_preflight(doe_dx_commands *commands,doe_dx_queries *queries,
    uint32_t index,doe_dx_reservation *needed)
{
    doe_dx_status status=preflight(commands,queries);
    if (status!=DOE_DX_OK) return status;
    if (queries->kind!=DOE_DX_QUERY_OCCLUSION || index>=queries->count) return DOE_DX_INVALID_ARGUMENT;
    if (!find_timestamps(commands,queries)) {
        if (needed->timestamps==SIZE_MAX) return DOE_DX_RANGE;
        ++needed->timestamps;
    }
    return DOE_DX_OK;
}
void doe_dx_use_render_query(doe_dx_commands *commands,doe_dx_queries *queries,uint32_t index,bool ended)
{
    doe_dx_timestamp_use *use=use_timestamps(commands,queries);
    if (!ended) return;
    if (index<64) use->initialized |= UINT64_C(1) << index;
    else use->initialized_upper[index/64-1] |= UINT64_C(1) << (index%64);
}
