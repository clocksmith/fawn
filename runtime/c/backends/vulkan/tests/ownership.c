/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
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
    assert(info->flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
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

int main(void) {
    acquisition_failures();
    async_lifetime();
    uncertain_and_loss();
    rejection_and_memory();
    failed_output_requires_reinitialization();
    puts("PASS host Vulkan boundary rollback, timeout, uncertain submission, device loss, bounds, poisoning");
    return 0;
}
