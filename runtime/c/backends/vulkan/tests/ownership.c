/* SPDX-License-Identifier: MIT */
#include "../src/query_internal.h"
#include <assert.h>
#include <stdio.h>

/* Inject only the native procedure boundary. These procedures never evaluate a
 * shader or substitute execution. The independent native test executes Vulkan. */
static struct {
    unsigned step;
    unsigned fail_at;
    unsigned buffers_destroyed;
    unsigned memory_freed;
    unsigned unmaps;
    unsigned pools_destroyed;
    unsigned fences_destroyed;
    unsigned submits;
    int32_t submit_result;
    int32_t fence_result;
    int32_t idle_result;
    int32_t reset_result;
    unsigned resets;
    bool reusable_begin;
    unsigned char bytes[256];
} mock;

static VkResult step(void) {
    ++mock.step;
    return mock.step == mock.fail_at ? -1 : 0;
}
static VkResult create_buffer(VkDevice device, const VkBufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkBuffer *out) {
    (void)device;
    (void)allocator;
    assert(info->size == 256);
    VkResult result = step();
    if (result == 0) *out = 11;
    return result;
}
static void destroy_buffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(buffer == 11);
    ++mock.buffers_destroyed;
}
static void requirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *out) {
    (void)device;
    assert(buffer == 11);
    *out = (VkMemoryRequirements){256, 64, 1};
}
static VkResult allocate_memory(VkDevice device, const VkMemoryAllocateInfo *info,
    const VkAllocationCallbacks *allocator, VkDeviceMemory *out) {
    (void)device;
    (void)allocator;
    assert(info->allocationSize == 256 && info->memoryTypeIndex == 0);
    VkResult result = step();
    if (result == 0) *out = 12;
    return result;
}
static void free_memory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(memory == 12);
    ++mock.memory_freed;
}
static VkResult bind_memory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    (void)device;
    assert(buffer == 11 && memory == 12 && offset == 0);
    return step();
}
static VkResult map_memory(VkDevice device, VkDeviceMemory memory,
    VkDeviceSize offset, VkDeviceSize size, VkFlags flags, void **out) {
    (void)device;
    assert(memory == 12 && offset == 0 && size == 256 && flags == 0);
    VkResult result = step();
    if (result == 0) *out = mock.bytes;
    return result;
}
static void unmap_memory(VkDevice device, VkDeviceMemory memory) {
    (void)device;
    assert(memory == 12);
    ++mock.unmaps;
}
static VkResult create_pool(VkDevice device, const VkCommandPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkCommandPool *out) {
    (void)device;
    (void)allocator;
    assert(info->queueFamilyIndex == 3);
    VkResult result = step();
    if (result == 0) *out = 21;
    return result;
}
static void destroy_pool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(pool == 21);
    ++mock.pools_destroyed;
}
static VkResult allocate_commands(VkDevice device, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *out) {
    (void)device;
    assert(info->commandPool == 21 && info->commandBufferCount == 1);
    VkResult result = step();
    if (result == 0) *out = (VkCommandBuffer)(uintptr_t)22;
    return result;
}
static VkResult create_fence(VkDevice device, const VkFenceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFence *out) {
    (void)device;
    (void)allocator;
    assert(info->flags == 0);
    VkResult result = step();
    if (result == 0) *out = 23;
    return result;
}
static void destroy_fence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(fence == 23);
    ++mock.fences_destroyed;
}
static VkResult begin(VkCommandBuffer command, const VkCommandBufferBeginInfo *info) {
    assert(command == (VkCommandBuffer)(uintptr_t)22);
    assert(info->flags == (mock.reusable_begin ? 0u : (unsigned)VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
    return step();
}
static VkResult end(VkCommandBuffer command) {
    assert(command == (VkCommandBuffer)(uintptr_t)22);
    return 0;
}
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination,
    VkFlags dependencies, uint32_t count, const void *memory, uint32_t buffers,
    const void *buffer_memory, uint32_t images, const void *image_memory) {
    (void)command;
    (void)dependencies;
    (void)buffer_memory;
    (void)image_memory;
    assert(source & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    assert(destination & VK_PIPELINE_STAGE_HOST_BIT);
    assert(count == 1 && buffers == 0 && images == 0);
    const VkMemoryBarrier *value = memory;
    assert(value->srcAccessMask & VK_ACCESS_MEMORY_WRITE_BIT);
    assert(value->dstAccessMask & VK_ACCESS_HOST_READ_BIT);
}
static void fill(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, uint32_t data) {
    (void)data;
    assert(command == (VkCommandBuffer)(uintptr_t)22 && buffer == 11 && offset == 0 && size == 256);
}
static VkResult submit(VkQueue queue, uint32_t count, const VkSubmitInfo *info, VkFence fence) {
    (void)queue;
    assert(count == 1 && info->commandBufferCount == 1 && fence == 23);
    ++mock.submits;
    return mock.submit_result;
}
static VkResult wait_fence(VkDevice device, uint32_t count, const VkFence *fences, VkBool32 all, uint64_t timeout) {
    (void)device;
    (void)timeout;
    assert(count == 1 && fences[0] == 23 && all == 1);
    return mock.fence_result;
}
static VkResult fence_status(VkDevice device, VkFence fence) {
    (void)device;
    assert(fence == 23);
    return mock.fence_result;
}
static VkResult reset_fence(VkDevice device, uint32_t count, const VkFence *fences) {
    (void)device;
    assert(count == 1 && fences[0] == 23);
    ++mock.resets;
    return mock.reset_result;
}
static VkResult idle(VkQueue queue) {
    (void)queue;
    return mock.idle_result;
}

static doe_vk_context context(void) {
    doe_vk_context value = { .references = 1 };
    value.caps.queue_family = 3;
    value.memory.memoryTypeCount = 1;
    value.memory.memoryHeapCount = 1;
    value.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    value.vk = (doe_vk_functions){
        .vkCreateBuffer = create_buffer,
        .vkDestroyBuffer = destroy_buffer,
        .vkGetBufferMemoryRequirements = requirements,
        .vkAllocateMemory = allocate_memory,
        .vkFreeMemory = free_memory,
        .vkBindBufferMemory = bind_memory,
        .vkMapMemory = map_memory,
        .vkUnmapMemory = unmap_memory,
        .vkCreateCommandPool = create_pool,
        .vkDestroyCommandPool = destroy_pool,
        .vkAllocateCommandBuffers = allocate_commands,
        .vkCreateFence = create_fence,
        .vkDestroyFence = destroy_fence,
        .vkBeginCommandBuffer = begin,
        .vkEndCommandBuffer = end,
        .vkCmdPipelineBarrier = barrier,
        .vkCmdFillBuffer = fill,
        .vkQueueSubmit = submit,
        .vkWaitForFences = wait_fence,
        .vkGetFenceStatus = fence_status,
        .vkResetFences = reset_fence,
        .vkQueueWaitIdle = idle
    };
    return value;
}

static void acquisition_failures(void) {
    for (unsigned failure = 1; failure <= 4; ++failure) {
        memset(&mock, 0, sizeof(mock));
        mock.fail_at = failure;
        doe_vk_context ctx = context();
        doe_vk_buffer *output = (doe_vk_buffer *)(uintptr_t)0x1234;
        assert(doe_vk_buffer_create(&ctx, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST,
            &output, NULL) == DOE_VK_OUT_OF_MEMORY);
        assert(output == (doe_vk_buffer *)(uintptr_t)0x1234);
        assert(ctx.references == 1);
        assert(mock.buffers_destroyed == (failure > 1));
        assert(mock.memory_freed == (failure > 2));
        assert(mock.unmaps == 0);
    }
    for (unsigned failure = 1; failure <= 4; ++failure) {
        memset(&mock, 0, sizeof(mock));
        mock.fail_at = failure;
        doe_vk_context ctx = context();
        doe_vk_commands *output = (doe_vk_commands *)(uintptr_t)0x1234;
        assert(doe_vk_commands_create(&ctx, &output, NULL) == DOE_VK_OUT_OF_MEMORY);
        assert(output == (doe_vk_commands *)(uintptr_t)0x1234);
        assert(ctx.references == 1);
        assert(mock.pools_destroyed == (failure > 1));
        assert(mock.fences_destroyed == (failure > 3));
    }
}

static void async_lifetime(void) {
    memset(&mock, 0, sizeof(mock));
    doe_vk_context ctx = context();
    doe_vk_buffer *buffer = NULL;
    doe_vk_commands *commands = NULL;
    assert(doe_vk_buffer_create(&ctx, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &buffer, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_create(&ctx, &commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_fill(commands, buffer, 0, 256, 7) == DOE_VK_OK);
    uint32_t value = 99;
    assert(doe_vk_buffer_write(buffer, 0, &value, sizeof(value)) == DOE_VK_BUSY);
    doe_vk_buffer_release(buffer);
    assert(mock.buffers_destroyed == 0);
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    mock.fence_result = 2;
    assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_TIMEOUT);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    mock.fence_result = -1;
    assert(doe_vk_commands_poll(commands, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    assert(mock.pools_destroyed == 0 && mock.buffers_destroyed == 0);
    mock.fence_result = 0;
    assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(mock.buffers_destroyed == 1 && mock.memory_freed == 1 && mock.unmaps == 1);
    assert(mock.pools_destroyed == 1 && mock.fences_destroyed == 1 && ctx.references == 1);
}

static void uncertain_and_loss(void) {
    memset(&mock, 0, sizeof(mock));
    doe_vk_context ctx = context();
    doe_vk_commands *commands = NULL;
    assert(doe_vk_commands_create(&ctx, &commands, NULL) == DOE_VK_OK);
    mock.submit_result = -1;
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    mock.idle_result = -1;
    assert(doe_vk_commands_recover(commands, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    mock.idle_result = 0;
    assert(doe_vk_commands_recover(commands, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(ctx.references == 1);
    assert(doe_vk_commands_create(&ctx, &commands, NULL) == DOE_VK_OK);
    mock.submit_result = 0;
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OK);
    mock.fence_result = -4;
    assert(doe_vk_commands_poll(commands, NULL) == DOE_VK_DEVICE_LOST);
    assert(ctx.lost);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(ctx.references == 1);
}

static void failed_output_requires_reinitialization(void) {
    memset(&mock, 0, sizeof(mock));
    doe_vk_context ctx = context();
    doe_vk_buffer *buffer = NULL;
    doe_vk_commands *commands = NULL;
    assert(doe_vk_buffer_create(&ctx, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &buffer, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_create(&ctx, &commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_fill(commands, buffer, 0, 256, 7) == DOE_VK_OK);
    mock.submit_result = -1;
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_commands_recover(commands, NULL) == DOE_VK_OUT_OF_MEMORY);
    uint32_t output = 123;
    assert(doe_vk_buffer_read(buffer, 0, &output, sizeof(output)) == DOE_VK_OUT_OF_MEMORY);
    assert(output == 123);
    assert(doe_vk_buffer_write(buffer, 0, &output, sizeof(output)) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(doe_vk_commands_create(&ctx, &commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_fill(commands, buffer, 0, 256, 0) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    uint32_t initialized[64];
    for (unsigned i = 0; i < 64; ++i) initialized[i] = 7;
    assert(doe_vk_buffer_write(buffer, 0, initialized, sizeof(initialized)) == DOE_VK_OK);
    assert(doe_vk_buffer_read(buffer, 0, &output, sizeof(output)) == DOE_VK_OK && output == 7);
    doe_vk_buffer_release(buffer);
    assert(ctx.references == 1);
}

static void first_failure_detail(void) {
    memset(&mock, 0, sizeof(mock));
    doe_vk_context ctx = context();
    doe_vk_commands *commands = NULL;
    doe_vk_error error = {0};
    assert(doe_vk_commands_create(&ctx, &commands, NULL) == DOE_VK_OK);
    mock.submit_result = -1;
    /* The owner must snapshot even when the original caller declines output. */
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    assert(doe_vk_commands_recover(commands, &error) == DOE_VK_OUT_OF_MEMORY);
    assert(error.status == DOE_VK_OUT_OF_MEMORY && error.native_result == -1);
    assert(!strcmp(error.operation, "vkQueueSubmit"));
    memset(&error, 0, sizeof(error));
    assert(doe_vk_commands_poll(commands, &error) == DOE_VK_OUT_OF_MEMORY);
    assert(error.native_result == -1 && !strcmp(error.operation, "vkQueueSubmit"));
    assert(doe_vk_commands_wait(commands, 0, &error) == DOE_VK_OUT_OF_MEMORY);
    assert(error.native_result == -1 && !strcmp(error.operation, "vkQueueSubmit"));
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);

    assert(doe_vk_commands_create(&ctx, &commands, NULL) == DOE_VK_OK);
    mock.submit_result = 0;
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OK);
    mock.fence_result = -4;
    assert(doe_vk_commands_poll(commands, NULL) == DOE_VK_DEVICE_LOST);
    assert(doe_vk_commands_wait(commands, 0, &error) == DOE_VK_DEVICE_LOST);
    assert(error.native_result == -4 && !strcmp(error.operation, "vkGetFenceStatus"));
    assert(doe_vk_commands_recover(commands, &error) == DOE_VK_DEVICE_LOST);
    assert(error.native_result == -4 && !strcmp(error.operation, "vkGetFenceStatus"));
    assert(doe_vk_commands_release(commands) == DOE_VK_OK && ctx.references == 1);
}

static void rejection_and_memory(void) {
    assert(doe_vk_range(UINT64_MAX, UINT64_MAX, 0));
    assert(!doe_vk_range(UINT64_MAX, UINT64_MAX, 1));
    assert(!doe_vk_range(16, 17, 0));
    doe_vk_context ctx = context();
    uint32_t index = 99;
    assert(doe_vk_memory_type(&ctx.memory, 1, 6, 8, &index) == DOE_VK_OK && index == 0);
    index = 99;
    assert(doe_vk_memory_type(&ctx.memory, 0, 6, 0, &index) == DOE_VK_UNSUPPORTED && index == 99);
    ctx.memory.memoryTypeCount = 33;
    assert(doe_vk_memory_type(&ctx.memory, UINT32_MAX, 6, 0, &index) == DOE_VK_INVALID_ARGUMENT);
    memset(&mock, 0, sizeof(mock));
    ctx = context();
    doe_vk_buffer *buffer = NULL;
    doe_vk_commands *commands = NULL;
    assert(doe_vk_buffer_create(&ctx, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &buffer, NULL) == DOE_VK_OK);
    uint32_t sentinel = 123;
    assert(doe_vk_buffer_read(buffer, UINT64_MAX, &sentinel, sizeof(sentinel)) == DOE_VK_INVALID_ARGUMENT);
    assert(sentinel == 123);
    assert(doe_vk_commands_create(&ctx, &commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_fill(commands, buffer, 1, 4, 0) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_commands_fill(commands, buffer, 0, 256, 0) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_INVALID_ARGUMENT);
    assert(mock.submits == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    doe_vk_buffer_release(buffer);
    assert(ctx.references == 1);
}

/* Compute-program recording: one native command buffer, many submissions. */
static void reusable_lifetime(void) {
    memset(&mock, 0, sizeof(mock));
    mock.reusable_begin = true;
    doe_vk_context ctx = context();
    doe_vk_buffer *buffer = NULL;
    doe_vk_commands *commands = NULL;
    uint32_t value = 99;
    assert(doe_vk_buffer_create(&ctx, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &buffer, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_create_reusable(&ctx, &commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_fill(commands, buffer, 0, 256, 7) == DOE_VK_OK);
    doe_vk_buffer_release(buffer); /* The recording keeps the native buffer. */
    for (unsigned run = 1; run <= 3; ++run) {
        assert(doe_vk_commands_retain(commands) == DOE_VK_OK); /* Pending holder. */
        assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OK);
        assert(mock.submits == run && mock.resets == run - 1);
        assert(doe_vk_buffer_write(buffer, 0, &value, sizeof(value)) == DOE_VK_BUSY);
        assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_INVALID_STATE); /* In flight. */
        assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
        assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_OK);
        assert(doe_vk_commands_release(commands) == DOE_VK_OK); /* Pending holder done. */
        /* Between runs the buffer is no longer recorded-in-use: hosts may map it. */
        assert(doe_vk_buffer_write(buffer, 0, &value, sizeof(value)) == DOE_VK_OK);
    }
    assert(doe_vk_commands_fill(commands, buffer, 0, 256, 7) == DOE_VK_INVALID_STATE);
    assert(doe_vk_commands_seal(commands, NULL) == DOE_VK_INVALID_STATE); /* Already ended. */
    /* Program drops while the last submission is still in flight: the pending
     * holder alone owns completion, and its release frees the recording. */
    assert(doe_vk_commands_retain(commands) == DOE_VK_OK);
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OK && mock.resets == 3);
    assert(doe_vk_commands_drop(commands) == DOE_VK_OK);
    assert(mock.pools_destroyed == 0 && mock.buffers_destroyed == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(mock.pools_destroyed == 1 && mock.buffers_destroyed == 1 && ctx.references == 1);

    /* Fence re-arm failure and execution failure are terminal. */
    memset(&mock, 0, sizeof(mock));
    mock.reusable_begin = true;
    ctx = context();
    assert(doe_vk_buffer_create(&ctx, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &buffer, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_create_reusable(&ctx, &commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_fill(commands, buffer, 0, 256, 7) == DOE_VK_OK);
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_OK);
    mock.reset_result = -1;
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OUT_OF_MEMORY && mock.submits == 1);
    assert(doe_vk_buffer_write(buffer, 0, &value, sizeof(value)) == DOE_VK_OK); /* Uses dropped. */
    mock.reset_result = 0;
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_INVALID_STATE && mock.submits == 1);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(doe_vk_commands_create_reusable(&ctx, &commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_fill(commands, buffer, 0, 256, 7) == DOE_VK_OK);
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OK);
    mock.fence_result = -1; /* A failed wait is not completion: still in flight. */
    assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    mock.fence_result = 0;
    assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_OK); /* READY again. */
    mock.submit_result = -1; /* Uncertain resubmission recovers to its failure. */
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OUT_OF_MEMORY && mock.resets == 2);
    assert(doe_vk_commands_recover(commands, NULL) == DOE_VK_OUT_OF_MEMORY);
    mock.submit_result = 0;
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_INVALID_STATE && mock.submits == 3);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    /* Poisoned content rejects resubmission before any native call. */
    assert(doe_vk_buffer_write(buffer, 0, &value, sizeof(value)) == DOE_VK_OUT_OF_MEMORY);
    doe_vk_buffer_release(buffer);
    assert(ctx.references == 1);

    /* Sealing ends a recording before any submission and frees its buffers for
     * host writes; the first submission then re-acquires them. */
    mock.reusable_begin = true;
    assert(doe_vk_buffer_create(&ctx, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &buffer, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_create_reusable(&ctx, &commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_fill(commands, buffer, 0, 256, 7) == DOE_VK_OK);
    assert(doe_vk_buffer_write(buffer, 0, &value, sizeof(value)) == DOE_VK_BUSY);
    assert(doe_vk_commands_seal(commands, NULL) == DOE_VK_OK);
    uint32_t initialized[64] = {0};
    assert(doe_vk_buffer_write(buffer, 0, initialized, sizeof(initialized)) == DOE_VK_OK);
    unsigned submits = mock.submits;
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OK && mock.submits == submits + 1);
    assert(doe_vk_buffer_write(buffer, 0, &value, sizeof(value)) == DOE_VK_BUSY);
    assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    doe_vk_buffer_release(buffer);
    assert(ctx.references == 1);

    /* One-shot commands have exactly one holder and cannot be sealed. */
    mock.reusable_begin = false;
    assert(doe_vk_commands_create(&ctx, &commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_retain(commands) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_commands_seal(commands, NULL) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_commands_drop(commands) == DOE_VK_OK);
    assert(ctx.references == 1);
}

static unsigned query_resets, query_writes, query_copies;
static void reset_queries(VkCommandBuffer command, VkQueryPool pool, uint32_t first, uint32_t count) {
    (void)command;
    assert(pool == 41 && first == 0 && count == 2);
    ++query_resets;
}
static void write_timestamp(VkCommandBuffer command, VkFlags stage, VkQueryPool pool, uint32_t index) {
    (void)command; (void)stage;
    assert(pool == 41 && index < 2);
    ++query_writes;
}
static void copy_queries(VkCommandBuffer command, VkQueryPool pool, uint32_t first, uint32_t count,
    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize stride, VkFlags flags) {
    (void)command; (void)buffer; (void)offset; (void)flags;
    assert(pool == 41 && first == 0 && count == 2 && stride == 8);
    ++query_copies;
}
static doe_vk_commands *query_recording(doe_vk_context *ctx, doe_vk_query_pool *pool, doe_vk_buffer *buffer,
    bool reset, bool write) {
    doe_vk_commands *commands = NULL;
    assert(doe_vk_commands_create_reusable(ctx, &commands, NULL) == DOE_VK_OK);
    if (reset) assert(doe_vk_commands_reset_queries(commands, pool, 0, 2) == DOE_VK_OK);
    if (write) {
        assert(doe_vk_commands_write_timestamp(commands, pool, 0, DOE_VK_TIMESTAMP_BEGIN) == DOE_VK_OK);
        assert(doe_vk_commands_write_timestamp(commands, pool, 1, DOE_VK_TIMESTAMP_END) == DOE_VK_OK);
    }
    assert(doe_vk_commands_resolve_queries(commands, pool, 0, 2, buffer, 0) == DOE_VK_OK);
    return commands;
}

/* Reusable timestamp recordings: self-contained indices only, pool ownership
 * per run, publication of exactly the recorded indices, health and poisoning. */
static void reusable_queries(void) {
    memset(&mock, 0, sizeof(mock));
    mock.reusable_begin = true;
    doe_vk_context ctx = context();
    ctx.vk.vkCmdResetQueryPool = reset_queries;
    ctx.vk.vkCmdWriteTimestamp = write_timestamp;
    ctx.vk.vkCmdCopyQueryPoolResults = copy_queries;
    uint8_t states[3] = {DOE_VK_QUERY_UNRESET, DOE_VK_QUERY_UNRESET, DOE_VK_QUERY_UNRESET};
    doe_vk_query_pool pool = {.context = &ctx, .references = 1, .count = 3, .states = states, .pool = 41};
    doe_vk_buffer *buffer = NULL;
    assert(doe_vk_buffer_create(&ctx, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &buffer, NULL) == DOE_VK_OK);
    doe_vk_commands *commands = query_recording(&ctx, &pool, buffer, true, true);
    assert(pool.recording_owner == commands && query_copies == 1);
    assert(doe_vk_commands_seal(commands, NULL) == DOE_VK_OK);
    /* Sealed: the pool is free for other work and unchanged until a run completes. */
    assert(pool.recording_owner == NULL && states[0] == DOE_VK_QUERY_UNRESET);
    doe_vk_commands other = {.context = &ctx};
    pool.recording_owner = &other;
    unsigned submits = mock.submits;
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_BUSY && mock.submits == submits);
    assert(doe_vk_buffer_write(buffer, 0, states, 1) == DOE_VK_OK); /* No uses were taken. */
    pool.recording_owner = NULL;
    for (unsigned run = 1; run <= 3; ++run) {
        states[2] = (uint8_t)(run % 2 ? DOE_VK_QUERY_WRITTEN : DOE_VK_QUERY_RESET); /* Other work between runs. */
        assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OK && mock.submits == submits + run);
        assert(pool.recording_owner == commands);
        assert(doe_vk_commands_wait(commands, 1, NULL) == DOE_VK_OK);
        assert(pool.recording_owner == NULL);
        assert(states[0] == DOE_VK_QUERY_WRITTEN && states[1] == DOE_VK_QUERY_WRITTEN);
        assert(states[2] == (run % 2 ? DOE_VK_QUERY_WRITTEN : DOE_VK_QUERY_RESET)); /* Untouched. */
    }
    /* Unhealthy pool content rejects before any native call. */
    pool.content_status = DOE_VK_NATIVE_FAILURE;
    submits = mock.submits;
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_NATIVE_FAILURE && mock.submits == submits);
    pool.content_status = DOE_VK_OK;
    /* A failed run poisons the pool, as for one-shot work. */
    mock.submit_result = -1;
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_commands_recover(commands, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(pool.content_status == DOE_VK_OUT_OF_MEMORY && pool.recording_owner == NULL);
    mock.submit_result = 0;
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    pool.content_status = DOE_VK_OK;
    /* The failed run also poisoned the resolve destination; a full rewrite heals it. */
    unsigned char rewrite[256] = {0};
    assert(doe_vk_buffer_write(buffer, 0, rewrite, 1) == DOE_VK_OUT_OF_MEMORY);
    assert(doe_vk_buffer_write(buffer, 0, rewrite, sizeof(rewrite)) == DOE_VK_OK);

    /* Not self-contained: a write of an index reset only by earlier work, or a
     * resolve of an index this recording never reset, rejects at seal. */
    states[0] = states[1] = DOE_VK_QUERY_RESET;
    commands = query_recording(&ctx, &pool, buffer, false, true);
    assert(doe_vk_commands_seal(commands, NULL) == DOE_VK_UNSUPPORTED && pool.recording_owner == commands);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK && pool.recording_owner == NULL);
    states[0] = states[1] = DOE_VK_QUERY_WRITTEN; /* Values written by earlier work. */
    commands = query_recording(&ctx, &pool, buffer, false, false);
    assert(doe_vk_commands_seal(commands, NULL) == DOE_VK_UNSUPPORTED);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(states[0] == DOE_VK_QUERY_WRITTEN && pool.references == 1);
    doe_vk_buffer_release(buffer);
    assert(ctx.references == 1);
}

int main(void) {
    acquisition_failures();
    async_lifetime();
    uncertain_and_loss();
    rejection_and_memory();
    failed_output_requires_reinitialization();
    first_failure_detail();
    reusable_lifetime();
    reusable_queries();
    puts("PASS host Vulkan boundary rollback, timeout, uncertain submission, device loss, bounds, poisoning");
    puts("PASS reusable recording: one native command buffer resubmitted with fence re-arm, buffer uses dropped between runs, pending-holder BUSY vs program drop, terminal reset/execution failure, one-shot retain rejected");
    puts("PASS reusable timestamp recording: self-contained reset/write/resolve sealed, pool ownership released at seal and per completion, BUSY under another owner without native submit, three runs publishing only recorded indices, unhealthy pool rejection, failed run poisoning, non-self-contained write/resolve rejected at seal");
    return 0;
}
