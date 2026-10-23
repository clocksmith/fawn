/* SPDX-License-Identifier: MIT */
#include "../src/timestamp_internal.h"
#include <assert.h>
#include <stdio.h>

/* Native-boundary recording/lifetime injections only, never shader execution. */
static unsigned sequence, destroyed_pipeline, destroyed_buffer, destroyed_pool;
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
static void query_copy(VkCommandBuffer command, VkQueryPool pool, uint32_t first,
    uint32_t count, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize stride, VkFlags flags) {
    (void)command;
    assert(sequence++ == 0 && pool == 4 && first == 1 && count == 1 && buffer == 3);
    assert(offset == 0 && stride == 8 && flags == (VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
}
static void fill(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset,
    VkDeviceSize size, uint32_t value) {
    (void)command;
    assert(sequence++ == 1 && buffer == 3 && offset == 8 && size == 16 && value == 0);
}
static VkResult create_pool(VkDevice device, const VkDescriptorPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkDescriptorPool *out) {
    (void)device;
    (void)allocator;
    assert(info->maxSets == 1 && info->poolSizeCount == 1 && info->pPoolSizes[0].descriptorCount == 1);
    *out = 9;
    return 0;
}
static VkResult allocate_sets(VkDevice device, const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *out) {
    (void)device;
    assert(info->descriptorPool == 9 && info->descriptorSetCount == 1 && info->pSetLayouts[0] == 7);
    *out = 10;
    return 0;
}
static void update_sets(VkDevice device, uint32_t count, const VkWriteDescriptorSet *writes,
    uint32_t copies, const void *copy) {
    (void)device;
    (void)copy;
    assert(sequence++ == 2 && count == 1 && copies == 0 && writes[0].dstSet == 10);
    assert(writes[0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    assert(writes[0].pBufferInfo->buffer == 3 && writes[0].pBufferInfo->offset == 0 && writes[0].pBufferInfo->range == 24);
}
static void bind_pipeline(VkCommandBuffer command, int32_t point, VkPipeline pipeline) {
    (void)command;
    assert(point == VK_PIPELINE_BIND_POINT_COMPUTE && pipeline == 8);
}
static void bind_sets(VkCommandBuffer command, int32_t point, VkPipelineLayout layout,
    uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t offsets, const uint32_t *values) {
    (void)command;
    (void)values;
    assert(point == VK_PIPELINE_BIND_POINT_COMPUTE && layout == 6 && first == 0);
    assert(count == 1 && sets[0] == 10 && offsets == 0);
}
static void dispatch(VkCommandBuffer command, uint32_t x, uint32_t y, uint32_t z) {
    (void)command;
    assert(sequence++ == 3 && x == 1 && y == 1 && z == 1);
}
static void copy_buffer(VkCommandBuffer command, VkBuffer source, VkBuffer destination,
    uint32_t count, const VkBufferCopy *copy) {
    (void)command;
    assert(sequence++ == 4 && source == 3 && destination == 5 && count == 1);
    assert(copy->srcOffset == 0 && copy->dstOffset == 8 && copy->size == 24);
}
static void destroy_pipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(pipeline == 8);
    ++destroyed_pipeline;
}
static void destroy_layout(VkDevice device, VkPipelineLayout layout, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(layout == 6);
}
static void destroy_set(VkDevice device, VkDescriptorSetLayout layout, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(layout == 7);
}
static void destroy_buffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(buffer == 3);
    ++destroyed_buffer;
}
static void destroy_pool(VkDevice device, VkDescriptorPool pool, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(pool == 9);
    ++destroyed_pool;
}

static VkResult wait_failed(VkDevice device, uint32_t count, const VkFence *fences,
    VkBool32 all, uint64_t timeout) {
    (void)device;
    (void)fences;
    (void)timeout;
    assert(count == 1 && all == 1);
    return -4;
}

int main(void) {
    doe_vk_context context = {.references = 4}; /* Caller, pipeline, scratch, commands. */
    context.caps.max_workgroup_count[0] = 8;
    context.caps.max_workgroup_count[1] = 1;
    context.caps.max_workgroup_count[2] = 1;
    context.caps.storage_offset_alignment = 16;
    context.caps.max_storage_buffer_range = 64;
    context.vk = (doe_vk_functions){.vkCmdPipelineBarrier = barrier,
        .vkCmdCopyQueryPoolResults = query_copy, .vkCmdFillBuffer = fill,
        .vkCreateDescriptorPool = create_pool, .vkAllocateDescriptorSets = allocate_sets,
        .vkUpdateDescriptorSets = update_sets, .vkCmdBindPipeline = bind_pipeline,
        .vkCmdBindDescriptorSets = bind_sets, .vkCmdDispatch = dispatch,
        .vkCmdCopyBuffer = copy_buffer, .vkDestroyPipeline = destroy_pipeline,
        .vkDestroyPipelineLayout = destroy_layout, .vkDestroyDescriptorSetLayout = destroy_set,
        .vkDestroyBuffer = destroy_buffer, .vkDestroyDescriptorPool = destroy_pool,
        .vkWaitForFences = wait_failed};
    doe_vk_timestamp_normalizer *normalizer = calloc(1, sizeof(*normalizer));
    doe_vk_buffer *scratch = calloc(1, sizeof(*scratch));
    doe_vk_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    doe_vk_commands *commands = calloc(1, sizeof(*commands));
    assert(normalizer != NULL && scratch != NULL && pipeline != NULL && commands != NULL);
    *normalizer = (doe_vk_timestamp_normalizer){1, 5, 64, pipeline, scratch};
    *scratch = (doe_vk_buffer){.context = &context, .references = 1, .size = 40,
        .buffer = 3, .usage = DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_COPY_SRC | DOE_VK_BUFFER_COPY_DST};
    *pipeline = (doe_vk_pipeline){.context = &context, .references = 1, .pipeline = 8,
        .layout = 6, .group_count = 1, .binding_count = 1, .sets = {7},
        .bindings = {{0, 0, DOE_VK_BINDING_STORAGE}}};
    *commands = (doe_vk_commands){.context = &context, .state = DOE_VK_RECORDING, .uses_active = true};
    uint8_t states[] = {DOE_VK_QUERY_UNRESET, DOE_VK_QUERY_WRITTEN, DOE_VK_QUERY_RESET, DOE_VK_QUERY_UNRESET};
    doe_vk_query_pool pool = {.context = &context, .references = 1, .count = 4, .states = states, .pool = 4};
    doe_vk_buffer destination = {.context = &context, .references = 1, .size = 32,
        .buffer = 5, .usage = DOE_VK_BUFFER_COPY_DST};
    doe_vk_commands rejected = {.context = &context, .state = DOE_VK_RECORDING};
    assert(doe_vk_commands_resolve_queries_ns(&rejected, &pool, 0, 6, normalizer, &destination, 0) == DOE_VK_INVALID_ARGUMENT);
    rejected.state = DOE_VK_RECORDING;
    assert(doe_vk_commands_resolve_queries_ns(&rejected, &pool, 0, 1, normalizer, &destination, 1) == DOE_VK_INVALID_ARGUMENT);
    rejected.state = DOE_VK_RECORDING;
    scratch->content_status = DOE_VK_NATIVE_FAILURE;
    assert(doe_vk_commands_resolve_queries_ns(&rejected, &pool, 1, 3, normalizer, &destination, 8) == DOE_VK_NATIVE_FAILURE);
    assert(sequence == 0 && rejected.buffers == NULL);
    doe_vk_queries_release(&rejected);
    scratch->content_status = DOE_VK_OK;
    rejected.state = DOE_VK_RECORDING;
    doe_vk_context foreign = {0};
    destination.context = &foreign;
    assert(doe_vk_commands_resolve_queries_ns(&rejected, &pool, 1, 3, normalizer, &destination, 8) == DOE_VK_INVALID_ARGUMENT);
    destination.context = &context;
    assert(doe_vk_commands_resolve_queries_ns(commands, &pool, 4, 0, normalizer, &destination, 32) == DOE_VK_OK);
    assert(commands->buffers == NULL && commands->queries == NULL && sequence == 0);
    assert(doe_vk_commands_resolve_queries_ns(commands, &pool, 1, 3, normalizer, &destination, 8) == DOE_VK_OK);
    assert(sequence == 5 && pool.references == 2 && destination.references == 2);
    assert(scratch->references == 2 && pipeline->references == 2);
    assert(doe_vk_timestamp_normalizer_retain(normalizer) == DOE_VK_OK);
    doe_vk_timestamp_normalizer_release(normalizer);
    doe_vk_timestamp_normalizer_release(normalizer);
    assert(destroyed_buffer == 0 && destroyed_pipeline == 0);
    commands->state = DOE_VK_UNCERTAIN;
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    commands->state = DOE_VK_SUBMITTED;
    assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_DEVICE_LOST);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(destroyed_buffer == 1 && destroyed_pipeline == 1 && destroyed_pool == 1);
    assert(context.references == 1 && destination.references == 1 && pool.references == 1);
    assert(destination.content_status == DOE_VK_DEVICE_LOST && pool.content_status == DOE_VK_DEVICE_LOST);
    puts("PASS timestamp native copy/zero/descriptor/dispatch/copy ordering, exact short binding, zero-count no-op, early normalizer release and uncertain/failure lifetime");
    return 0;
}
