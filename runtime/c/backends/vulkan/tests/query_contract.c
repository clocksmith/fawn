/* SPDX-License-Identifier: MIT */
#include "../src/query_internal.h"
#include <assert.h>
#include <stdio.h>

static doe_vk_commands recording(doe_vk_context *context) {
    return (doe_vk_commands){.context = context, .state = DOE_VK_RECORDING, .uses_active = true};
}

static unsigned timestamp_calls, copy_calls, fill_calls;
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination,
    VkFlags flags, uint32_t memory_count, const void *memory, uint32_t buffer_count,
    const void *buffers, uint32_t image_count, const void *images) {
    (void)command;
    (void)source;
    (void)destination;
    (void)flags;
    (void)memory;
    (void)buffers;
    (void)images;
    assert(memory_count == 1 && buffer_count == 0 && image_count == 0);
}
static void write_timestamp(VkCommandBuffer command, VkFlags stage, VkQueryPool pool, uint32_t query) {
    (void)command;
    assert(pool == 8);
    assert((query == 0 && stage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT) ||
        (query == 2 && stage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT));
    ++timestamp_calls;
}
static void copy_results(VkCommandBuffer command, VkQueryPool pool, uint32_t first,
    uint32_t count, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize stride, VkFlags flags) {
    (void)command;
    assert(pool == 8 && buffer == 7 && count == 1 && stride == 8);
    assert((first == 0 && offset == 8) || (first == 2 && offset == 24));
    assert(flags == (VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    ++copy_calls;
}
static void fill_results(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset,
    VkDeviceSize size, uint32_t data) {
    (void)command;
    assert(buffer == 7 && size == 8 && data == 0 && (offset == 16 || offset == 32));
    ++fill_calls;
}
static void mixed_query_ranges(void) {
    doe_vk_context context = {.references = 2};
    context.vk.vkCmdPipelineBarrier = barrier;
    context.vk.vkCmdWriteTimestamp = write_timestamp;
    context.vk.vkCmdCopyQueryPoolResults = copy_results;
    context.vk.vkCmdFillBuffer = fill_results;
    uint8_t states[4] = {DOE_VK_QUERY_RESET, DOE_VK_QUERY_UNRESET, DOE_VK_QUERY_RESET, DOE_VK_QUERY_RESET};
    doe_vk_query_pool pool = {.context = &context, .references = 1, .count = 4, .states = states, .pool = 8};
    doe_vk_buffer buffer = {.context = &context, .references = 1,
        .size = 40, .usage = DOE_VK_BUFFER_COPY_DST, .buffer = 7};
    doe_vk_commands *commands = calloc(1, sizeof(*commands));
    assert(commands != NULL);
    *commands = recording(&context);
    assert(doe_vk_commands_write_timestamp(commands, &pool, 0, DOE_VK_TIMESTAMP_BEGIN) == DOE_VK_OK);
    assert(doe_vk_commands_write_timestamp(commands, &pool, 2, DOE_VK_TIMESTAMP_END) == DOE_VK_OK);
    assert(doe_vk_commands_resolve_queries(commands, &pool, 0, 4, &buffer, 8) == DOE_VK_OK);
    assert(timestamp_calls == 2 && copy_calls == 2 && fill_calls == 2);
    assert(buffer.references == 2 && pool.references == 2);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(context.references == 1 && buffer.references == 1 && pool.references == 1);
    assert(states[0] == DOE_VK_QUERY_RESET && states[2] == DOE_VK_QUERY_RESET);
}

int main(void) {
    mixed_query_ranges();
    doe_vk_context context = {.references = 1};
    uint8_t states[2] = {DOE_VK_QUERY_UNRESET, DOE_VK_QUERY_UNRESET};
    doe_vk_query_pool pool = {.context = &context, .references = 1, .count = 2, .states = states};
    doe_vk_commands first = recording(&context), second = recording(&context);
    assert(doe_vk_commands_write_timestamp(&first, &pool, 0, DOE_VK_TIMESTAMP_BEGIN) == DOE_VK_INVALID_STATE);
    assert(first.queries != NULL && pool.references == 2);
    doe_vk_queries_release(&first);
    assert(pool.references == 1 && pool.recording_owner == NULL && states[0] == DOE_VK_QUERY_UNRESET);
    first = recording(&context);
    assert(doe_vk_commands_reset_queries(&first, &pool, UINT32_MAX, 2) == DOE_VK_INVALID_ARGUMENT);
    assert(first.queries == NULL);
    doe_vk_buffer destination = {.context = &context, .size = 16, .usage = DOE_VK_BUFFER_COPY_DST};
    first = recording(&context);
    assert(doe_vk_commands_resolve_queries(&first, &pool, 0, 2, &destination, 8) == DOE_VK_INVALID_ARGUMENT);
    assert(first.queries == NULL);
    first = recording(&context);
    assert(doe_vk_commands_resolve_queries(&first, &pool, 2, 0, &destination, 16) == DOE_VK_OK);
    assert(first.queries == NULL && first.buffers == NULL);
    doe_vk_queries_release(&first);
    first = recording(&context);
    doe_vk_query_use *use = NULL;
    assert(doe_vk_use_queries(&first, &pool, &use) == DOE_VK_OK);
    use->states[0] = DOE_VK_QUERY_RESET;
    use->states[1] = DOE_VK_QUERY_WRITTEN;
    assert(states[0] == DOE_VK_QUERY_UNRESET && states[1] == DOE_VK_QUERY_UNRESET);
    doe_vk_query_use *other = NULL;
    assert(doe_vk_use_queries(&second, &pool, &other) == DOE_VK_BUSY);
    doe_vk_queries_complete(&first, DOE_VK_OK);
    assert(states[0] == DOE_VK_QUERY_RESET && states[1] == DOE_VK_QUERY_WRITTEN);
    assert(doe_vk_use_queries(&second, &pool, &other) == DOE_VK_OK);
    doe_vk_queries_release(&first);
    assert(pool.recording_owner == &second);
    doe_vk_queries_complete(&second, DOE_VK_OUT_OF_MEMORY);
    doe_vk_queries_release(&second);
    assert(pool.references == 1 && pool.recording_owner == NULL);
    assert(doe_vk_use_queries(&first, &pool, &use) == DOE_VK_OUT_OF_MEMORY);
    doe_vk_query_pool *output = NULL;
    assert(doe_vk_query_pool_create(&context, 2, &output, NULL) == DOE_VK_UNSUPPORTED && output == NULL);
    puts("PASS timestamp TOP/BOTTOM, mixed native copy/zero without unissued WAIT, reset/write/resolve state, last-byte/range rejection, exclusive unresolved owner, transactional completion, old-owner release and failed-query poisoning");
    return 0;
}
