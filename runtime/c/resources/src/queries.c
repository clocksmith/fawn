/* SPDX-License-Identifier: MIT */
#include "internal.h"

bool resource_query_available(const doe_resource_query_set *set)
{
    return set && !set->destroyed && resource_alive(&set->base);
}

doe_resource_status doe_resource_query_set_validate(WGPUQueryType type, uint32_t count)
{
    if (type != WGPUQueryType_Timestamp && type != WGPUQueryType_Occlusion) return DOE_RESOURCE_UNSUPPORTED;
    return count <= DOE_RESOURCE_MAX_QUERIES ? DOE_RESOURCE_OK : DOE_RESOURCE_RANGE;
}

doe_resource_status doe_resource_query_set_create(const doe_resource_config *config,
    WGPUQueryType type, uint32_t count, doe_resource_native native, doe_resource_query_set **out)
{
    if (!resource_config_valid(config) || !out ||
        ((native.handle == NULL) != (native.release == NULL)) || (count && !native.handle))
        return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity)) return DOE_RESOURCE_UNAVAILABLE;
    doe_resource_status status = doe_resource_query_set_validate(type, count);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_query_set *set = resource_allocate(config, sizeof(*set), _Alignof(doe_resource_query_set));
    if (!set) return DOE_RESOURCE_OUT_OF_MEMORY;
    set->type = type;
    set->count = count;
    set->destroyed = false;
    resource_init(&set->base, config, native);
    *out = set;
    return DOE_RESOURCE_OK;
}

void doe_resource_query_set_retain(doe_resource_query_set *set)
{
    if (set) resource_retain(&set->base);
}

void doe_resource_query_set_release(doe_resource_query_set *set)
{
    if (set && resource_last_reference(&set->base))
        resource_finalize(&set->base, sizeof(*set), _Alignof(doe_resource_query_set));
}

void doe_resource_query_set_destroy(doe_resource_query_set *set)
{
    if (set) set->destroyed = true;
}

void *doe_resource_query_set_native(const doe_resource_query_set *set)
{
    return set ? set->base.native.handle : NULL;
}

uint32_t doe_resource_query_set_count(const doe_resource_query_set *set)
{
    return set ? set->count : 0;
}

WGPUQueryType doe_resource_query_set_type(const doe_resource_query_set *set)
{
    return set ? set->type : (WGPUQueryType)0;
}

void *doe_resource_query_set_device(const doe_resource_query_set *set)
{
    return set ? set->base.config.device.identity : NULL;
}

doe_resource_status resource_pass_timestamps_validate(const void *device,
    const doe_resource_pass_timestamps *timestamps)
{
    if (!timestamps) return DOE_RESOURCE_OK;
    if (!resource_query_available(timestamps->set)) return DOE_RESOURCE_UNAVAILABLE;
    if (timestamps->set->base.config.device.identity != device) return DOE_RESOURCE_DEVICE_MISMATCH;
    if (timestamps->set->type != WGPUQueryType_Timestamp) return DOE_RESOURCE_LAYOUT;
    /* GPU*PassTimestampWrites must name at least one write index. */
    if (timestamps->begin_index == UINT32_MAX && timestamps->end_index == UINT32_MAX) return DOE_RESOURCE_LAYOUT;
    if ((timestamps->begin_index != UINT32_MAX && timestamps->begin_index >= timestamps->set->count) ||
        (timestamps->end_index != UINT32_MAX && timestamps->end_index >= timestamps->set->count))
        return DOE_RESOURCE_RANGE;
    if (timestamps->begin_index != UINT32_MAX && timestamps->begin_index == timestamps->end_index)
        return DOE_RESOURCE_LAYOUT;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_encoder_write_timestamp(doe_resource_encoder *encoder,
    doe_resource_query_set *set, uint32_t index, doe_resource_timestamp_position position)
{
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    return resource_record_timestamp(encoder, set, index, position);
}

doe_resource_status resource_record_timestamp(doe_resource_encoder *encoder,
    doe_resource_query_set *set, uint32_t index, doe_resource_timestamp_position position)
{
    if (!resource_query_available(set)) return resource_encoder_fail(encoder, DOE_RESOURCE_UNAVAILABLE);
    if (set->base.config.device.identity != encoder->base.config.device.identity)
        return resource_encoder_fail(encoder, DOE_RESOURCE_DEVICE_MISMATCH);
    if (set->type != WGPUQueryType_Timestamp ||
        (position != DOE_TIMESTAMP_COMMAND && position != DOE_TIMESTAMP_PASS_BEGIN && position != DOE_TIMESTAMP_PASS_END))
        return resource_encoder_fail(encoder, DOE_RESOURCE_LAYOUT);
    if (index >= set->count) return resource_encoder_fail(encoder, DOE_RESOURCE_RANGE);
    doe_resource_status status = resource_encoder_reserve(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_command command = {.kind = DOE_COMMAND_WRITE_TIMESTAMP};
    command.value.timestamp.set = set;
    command.value.timestamp.index = index;
    command.value.timestamp.position = position;
    doe_resource_query_set_retain(set);
    encoder->commands[encoder->count++] = command;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_encoder_resolve_queries(doe_resource_encoder *encoder,
    doe_resource_query_set *set, uint32_t first, uint32_t count,
    doe_resource_buffer *destination, uint64_t offset)
{
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    if (!resource_query_available(set) || !resource_buffer_available(destination))
        return resource_encoder_fail(encoder, DOE_RESOURCE_UNAVAILABLE);
    if (set->base.config.device.identity != encoder->base.config.device.identity ||
        destination->base.config.device.identity != encoder->base.config.device.identity)
        return resource_encoder_fail(encoder, DOE_RESOURCE_DEVICE_MISMATCH);
    if (!(destination->desc.usage & WGPUBufferUsage_QueryResolve))
        return resource_encoder_fail(encoder, DOE_RESOURCE_USAGE);
    if (first >= set->count || count > set->count - first)
        return resource_encoder_fail(encoder, DOE_RESOURCE_RANGE);
    if (offset % 256) return resource_encoder_fail(encoder, DOE_RESOURCE_ALIGNMENT);
    uint64_t bytes;
    status = resource_range(destination->desc.size, offset, (uint64_t)count * sizeof(uint64_t), &bytes);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(encoder, status);
    status = resource_encoder_reserve(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_command command = {.kind = DOE_COMMAND_RESOLVE_QUERY};
    command.value.resolve.set = set;
    command.value.resolve.first = first;
    command.value.resolve.count = count;
    command.value.resolve.destination = destination;
    command.value.resolve.offset = offset;
    doe_resource_query_set_retain(set);
    doe_resource_buffer_retain(destination);
    encoder->commands[encoder->count++] = command;
    return DOE_RESOURCE_OK;
}
