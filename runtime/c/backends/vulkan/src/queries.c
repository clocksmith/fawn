/* SPDX-License-Identifier: MIT */
#include "query_internal.h"
#include <math.h>

static doe_vk_status create_pool(doe_vk_context *context, uint32_t count,
    doe_vk_query_kind kind, doe_vk_query_pool **out, doe_vk_error *error) {
    if (context == NULL || out == NULL || count == 0)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "query descriptor", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "query context", error);
    if (kind == DOE_VK_QUERY_TIMESTAMP && (context->caps.timestamp_valid_bits == 0 || context->caps.timestamp_valid_bits > 64 ||
        !isfinite(context->caps.timestamp_period_ns) || context->caps.timestamp_period_ns <= 0))
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "queue timestamps unavailable", error);
    if (kind == DOE_VK_QUERY_OCCLUSION && !(context->caps.queue_flags & VK_QUEUE_GRAPHICS_BIT))
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "queue occlusion unavailable", error);
    doe_vk_query_pool *pool = calloc(1, sizeof(*pool));
    if (pool == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "query owner", error);
    pool->states = calloc(count, 1);
    if (pool->states == NULL) {
        free(pool);
        return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "query state", error);
    }
    doe_vk_status status = doe_vk_context_retain(context);
    if (status != DOE_VK_OK) {
        free(pool->states);
        free(pool);
        return doe_vk_fail(status, "query context retain", error);
    }
    pool->context = context;
    pool->references = 1;
    pool->count = count;
    pool->kind = kind;
    const VkQueryPoolCreateInfo create = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = kind == DOE_VK_QUERY_TIMESTAMP ? VK_QUERY_TYPE_TIMESTAMP : VK_QUERY_TYPE_OCCLUSION, .queryCount = count };
    status = doe_vk_result(context, context->vk.vkCreateQueryPool(context->device, &create, NULL, &pool->pool), "vkCreateQueryPool", error);
    if (status != DOE_VK_OK) {
        doe_vk_query_pool_release(pool);
        return status;
    }
    *out = pool;
    return doe_vk_fail(DOE_VK_OK, "query pool ready", error);
}

doe_vk_status doe_vk_query_pool_create(doe_vk_context *context, uint32_t count,
    doe_vk_query_pool **out, doe_vk_error *error) {
    return create_pool(context, count, DOE_VK_QUERY_TIMESTAMP, out, error);
}

doe_vk_status doe_vk_occlusion_pool_create(doe_vk_context *context, uint32_t count,
    doe_vk_query_pool **out, doe_vk_error *error) {
    return create_pool(context, count, DOE_VK_QUERY_OCCLUSION, out, error);
}

doe_vk_status doe_vk_query_pool_retain(doe_vk_query_pool *pool) {
    if (pool == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (pool->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++pool->references;
    return DOE_VK_OK;
}

void doe_vk_query_pool_release(doe_vk_query_pool *pool) {
    if (pool == NULL || --pool->references != 0) return;
    doe_vk_context *context = pool->context;
    if (pool->pool != 0) context->vk.vkDestroyQueryPool(context->device, pool->pool, NULL);
    free(pool->states);
    free(pool);
    doe_vk_context_release(context);
}

doe_vk_status doe_vk_use_queries(doe_vk_commands *commands, doe_vk_query_pool *pool,
    doe_vk_query_use **out) {
    if (pool == NULL || pool->context != commands->context) return DOE_VK_INVALID_ARGUMENT;
    if (pool->content_status != DOE_VK_OK) return pool->content_status;
    if (pool->recording_owner != NULL && pool->recording_owner != commands) return DOE_VK_BUSY;
    for (doe_vk_query_use *use = commands->queries; use != NULL; use = use->next) {
        if (use->pool == pool) {
            *out = use;
            return DOE_VK_OK;
        }
    }
    doe_vk_query_use *use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_VK_OUT_OF_MEMORY;
    use->states = malloc(pool->count);
    if (use->states == NULL) {
        free(use);
        return DOE_VK_OUT_OF_MEMORY;
    }
    doe_vk_status status = doe_vk_query_pool_retain(pool);
    if (status != DOE_VK_OK) {
        free(use->states);
        free(use);
        return status;
    }
    memcpy(use->states, pool->states, pool->count);
    use->pool = pool;
    use->next = commands->queries;
    commands->queries = use;
    pool->recording_owner = commands;
    *out = use;
    return DOE_VK_OK;
}

static doe_vk_status range_use(doe_vk_commands *commands, doe_vk_query_pool *pool,
    uint32_t first, uint32_t count, doe_vk_query_use **out) {
    if (pool == NULL || count == 0 || !doe_vk_range(pool->count, first, count)) return DOE_VK_INVALID_ARGUMENT;
    return doe_vk_use_queries(commands, pool, out);
}

doe_vk_status doe_vk_commands_reset_queries(doe_vk_commands *commands,
    doe_vk_query_pool *pool, uint32_t first, uint32_t count) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    doe_vk_query_use *use = NULL;
    status = range_use(commands, pool, first, count, &use);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    doe_vk_barrier(commands);
    commands->context->vk.vkCmdResetQueryPool(commands->command, pool->pool, first, count);
    memset(use->states + first, DOE_VK_QUERY_RESET, count);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_commands_write_timestamp(doe_vk_commands *commands,
    doe_vk_query_pool *pool, uint32_t index, doe_vk_timestamp_stage stage) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    doe_vk_query_use *use = NULL;
    if (pool == NULL || pool->kind != DOE_VK_QUERY_TIMESTAMP ||
        (stage != DOE_VK_TIMESTAMP_BEGIN && stage != DOE_VK_TIMESTAMP_END))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    status = range_use(commands, pool, index, 1, &use);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    if (use->states[index] != DOE_VK_QUERY_RESET) return doe_vk_poison(commands, DOE_VK_INVALID_STATE);
    uint32_t native_stage = stage == DOE_VK_TIMESTAMP_BEGIN ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    commands->context->vk.vkCmdWriteTimestamp(commands->command, native_stage, pool->pool, index);
    use->states[index] = DOE_VK_QUERY_WRITTEN;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_commands_resolve_queries(doe_vk_commands *commands,
    doe_vk_query_pool *pool, uint32_t first, uint32_t count,
    doe_vk_buffer *destination, uint64_t offset) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (destination == NULL || destination->context != commands->context ||
        !(destination->usage & DOE_VK_BUFFER_COPY_DST) || offset % 8 != 0 ||
        !doe_vk_range(destination->size, offset, (uint64_t)count * 8))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    if (pool == NULL || pool->context != commands->context || !doe_vk_range(pool->count, first, count))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    if (pool->content_status != DOE_VK_OK) return doe_vk_poison(commands, pool->content_status);
    if (count == 0) return DOE_VK_OK;
    doe_vk_query_use *use = NULL;
    status = doe_vk_use_queries(commands, pool, &use);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    status = doe_vk_use_buffer(commands, destination);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    doe_vk_barrier(commands);
    uint32_t start = 0;
    while (start < count) {
        bool written = use->states[first + start] == DOE_VK_QUERY_WRITTEN;
        uint32_t end = start + 1;
        while (end < count && (use->states[first + end] == DOE_VK_QUERY_WRITTEN) == written) ++end;
        uint64_t destination_offset = offset + (uint64_t)start * 8;
        if (written) commands->context->vk.vkCmdCopyQueryPoolResults(commands->command, pool->pool,
            first + start, end - start, destination->buffer, destination_offset, 8,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        else commands->context->vk.vkCmdFillBuffer(commands->command, destination->buffer,
            destination_offset, (uint64_t)(end - start) * 8, 0);
        start = end;
    }
    return DOE_VK_OK;
}

void doe_vk_queries_complete(doe_vk_commands *commands, doe_vk_status status) {
    for (doe_vk_query_use *use = commands->queries; use != NULL; use = use->next) {
        if (status == DOE_VK_OK) memcpy(use->pool->states, use->states, use->pool->count);
        else if (use->pool->content_status == DOE_VK_OK) use->pool->content_status = status;
        if (use->pool->recording_owner == commands) use->pool->recording_owner = NULL;
    }
}

void doe_vk_queries_release(doe_vk_commands *commands) {
    while (commands->queries != NULL) {
        doe_vk_query_use *use = commands->queries;
        commands->queries = use->next;
        if (use->pool->recording_owner == commands) use->pool->recording_owner = NULL;
        doe_vk_query_pool_release(use->pool);
        free(use->states);
        free(use);
    }
}
