/* SPDX-License-Identifier: MIT */
#include "../src/texture_internal.h"
#include "../include/doe_vk_bindings.h"
#include <assert.h>
#include <stdio.h>

/* Native procedure boundary only: counts descriptor work per recorded dispatch. */
static struct {
    unsigned pools_created, pools_destroyed, sets_allocated, updates;
    unsigned barriers, dispatches, pipeline_binds, set_binds;
    uint64_t next_handle;
} mock;

static VkResult create_pool(VkDevice device, const VkDescriptorPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkDescriptorPool *out) {
    (void)device; (void)info; (void)allocator;
    ++mock.pools_created;
    *out = ++mock.next_handle;
    return 0;
}
static void destroy_pool(VkDevice device, VkDescriptorPool pool, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(pool != 0);
    ++mock.pools_destroyed;
}
static VkResult allocate_sets(VkDevice device, const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *sets) {
    (void)device;
    for (uint32_t i = 0; i < info->descriptorSetCount; ++i) sets[i] = ++mock.next_handle;
    ++mock.sets_allocated;
    return 0;
}
static void update_sets(VkDevice device, uint32_t count, const VkWriteDescriptorSet *writes,
    uint32_t copies, const void *copy) {
    (void)device; (void)count; (void)writes; (void)copies; (void)copy;
    ++mock.updates;
}
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination, VkFlags flags,
    uint32_t memory, const void *memory_barriers, uint32_t buffers, const void *buffer_barriers,
    uint32_t images, const void *image_barriers) {
    (void)command; (void)source; (void)destination; (void)flags; (void)memory; (void)memory_barriers;
    (void)buffers; (void)buffer_barriers; (void)images; (void)image_barriers;
    ++mock.barriers;
}
static void bind_pipeline(VkCommandBuffer command, int32_t point, VkPipeline pipeline) {
    (void)command; (void)point; (void)pipeline;
    ++mock.pipeline_binds;
}
static void bind_sets(VkCommandBuffer command, int32_t point, VkPipelineLayout layout, uint32_t first,
    uint32_t count, const VkDescriptorSet *sets, uint32_t dynamic, const uint32_t *offsets) {
    (void)command; (void)point; (void)layout; (void)first; (void)count; (void)sets; (void)dynamic; (void)offsets;
    ++mock.set_binds;
}
static void dispatch(VkCommandBuffer command, uint32_t x, uint32_t y, uint32_t z) {
    (void)command; (void)x; (void)y; (void)z;
    ++mock.dispatches;
}

static void release_uses(doe_vk_commands *commands) {
    while (commands->dispatches != NULL) {
        doe_vk_dispatch_use *use = commands->dispatches;
        commands->dispatches = use->next;
        doe_vk_dispatch_release(commands->context, use);
    }
    while (commands->buffers != NULL) {
        doe_vk_buffer_use *use = commands->buffers;
        commands->buffers = use->next;
        --use->buffer->recorded_uses;
        --use->buffer->references;
        free(use);
    }
}

int main(void) {
    doe_vk_context context = { .references = 1 };
    context.caps.max_workgroup_count[0] = context.caps.max_workgroup_count[1] = context.caps.max_workgroup_count[2] = 64;
    context.caps.storage_offset_alignment = 16;
    context.caps.max_storage_buffer_range = 1u << 20;
    context.vk = (doe_vk_functions){
        .vkCreateDescriptorPool = create_pool, .vkDestroyDescriptorPool = destroy_pool,
        .vkAllocateDescriptorSets = allocate_sets, .vkUpdateDescriptorSets = update_sets,
        .vkCmdPipelineBarrier = barrier, .vkCmdBindPipeline = bind_pipeline,
        .vkCmdBindDescriptorSets = bind_sets, .vkCmdDispatch = dispatch
    };
    doe_vk_pipeline pipeline = { .context = &context, .references = 1, .binding_count = 1, .group_count = 1 };
    pipeline.bindings[0] = (doe_vk_binding_layout){0, 0, DOE_VK_BINDING_STORAGE};
    doe_vk_pipeline other = pipeline;
    doe_vk_buffer buffer = { .context = &context, .references = 1, .usage = DOE_VK_BUFFER_STORAGE, .size = 256, .buffer = 5 };
    doe_vk_commands commands = { .context = &context, .state = DOE_VK_RECORDING, .uses_active = true };
    doe_vk_binding_resource binding = { .kind = DOE_VK_BINDING_STORAGE, .value.buffer = {&buffer, 0, 64} };

    /* Repeated identical dispatches share the latest use's descriptor sets;
     * each still records its barrier, binds and dispatch. */
    for (unsigned i = 0; i < 3; ++i)
        assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_OK);
    assert(mock.pools_created == 1 && mock.sets_allocated == 1 && mock.updates == 1);
    assert(mock.barriers == 3 && mock.pipeline_binds == 3 && mock.set_binds == 3 && mock.dispatches == 3);
    assert(pipeline.references == 2 && buffer.recorded_uses == 1);

    /* A different range or pipeline gets its own sets, and a use is shared
     * only with the immediately preceding one. */
    binding.value.buffer.offset = 64;
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_OK);
    assert(doe_vk_commands_dispatch_resources(&commands, &other, &binding, 1, 1, 1, 1) == DOE_VK_OK);
    binding.value.buffer.offset = 0;
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_OK);
    assert(mock.pools_created == 4 && mock.updates == 4 && mock.dispatches == 6);

    /* Validation still runs for a shared use. */
    buffer.content_status = DOE_VK_OUT_OF_MEMORY;
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, &binding, 1, 1, 1, 1) == DOE_VK_OUT_OF_MEMORY);
    assert(commands.state == DOE_VK_POISONED && mock.dispatches == 6);
    buffer.content_status = DOE_VK_OK;

    release_uses(&commands);
    assert(mock.pools_destroyed == mock.pools_created && pipeline.references == 1 && other.references == 1);
    assert(buffer.recorded_uses == 0 && buffer.references == 1);
    puts("PASS identical consecutive buffer dispatches share descriptor sets; ranges, pipelines and validation stay per dispatch");
    return 0;
}
