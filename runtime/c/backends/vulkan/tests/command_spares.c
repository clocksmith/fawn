/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
#include <assert.h>
#include <stdio.h>

/* Native procedure boundary only: a command pool and fence become a spare only
 * after their recording is proven terminal or was never submitted. */
static struct {
    unsigned pools_created, pools_destroyed, pool_resets, buffers_allocated;
    unsigned fences_created, fences_destroyed, fence_resets, begins, submits, idles;
    VkResult submit_result, fence_status, reset_result;
    uint64_t next_handle;
} mock;

static VkResult create_pool(VkDevice device, const VkCommandPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkCommandPool *out) {
    (void)device; (void)info; (void)allocator;
    ++mock.pools_created;
    *out = ++mock.next_handle;
    return 0;
}
static void destroy_pool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(pool != 0);
    ++mock.pools_destroyed;
}
static VkResult reset_pool(VkDevice device, VkCommandPool pool, VkFlags flags) {
    (void)device;
    assert(pool != 0 && flags == 0);
    ++mock.pool_resets;
    return mock.reset_result;
}
static VkResult allocate(VkDevice device, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *out) {
    (void)device;
    assert(info->commandBufferCount == 1);
    ++mock.buffers_allocated;
    *out = (VkCommandBuffer)(uintptr_t)++mock.next_handle;
    return 0;
}
static VkResult create_fence(VkDevice device, const VkFenceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFence *out) {
    (void)device; (void)info; (void)allocator;
    ++mock.fences_created;
    *out = ++mock.next_handle;
    return 0;
}
static void destroy_fence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(fence != 0);
    ++mock.fences_destroyed;
}
static VkResult reset_fences(VkDevice device, uint32_t count, const VkFence *fences) {
    (void)device;
    assert(count == 1 && fences[0] != 0);
    ++mock.fence_resets;
    return 0;
}
static VkResult begin(VkCommandBuffer command, const VkCommandBufferBeginInfo *info) {
    (void)command; (void)info;
    ++mock.begins;
    return 0;
}
static VkResult end(VkCommandBuffer command) {
    (void)command;
    return 0;
}
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination, VkFlags flags,
    uint32_t memory, const void *memory_barriers, uint32_t buffers, const void *buffer_barriers,
    uint32_t images, const void *image_barriers) {
    (void)command; (void)source; (void)destination; (void)flags; (void)memory; (void)memory_barriers;
    (void)buffers; (void)buffer_barriers; (void)images; (void)image_barriers;
}
static VkResult submit(VkQueue queue, uint32_t count, const VkSubmitInfo *info, VkFence fence) {
    (void)queue; (void)count; (void)info; (void)fence;
    ++mock.submits;
    return mock.submit_result;
}
static VkResult fence_status(VkDevice device, VkFence fence) {
    (void)device; (void)fence;
    return mock.fence_status;
}
static VkResult idle(VkQueue queue) {
    (void)queue;
    ++mock.idles;
    return 0;
}

static doe_vk_context context(void) {
    doe_vk_context value = { .references = 1 };
    value.vk = (doe_vk_functions){
        .vkCreateCommandPool = create_pool, .vkDestroyCommandPool = destroy_pool,
        .vkResetCommandPool = reset_pool, .vkAllocateCommandBuffers = allocate,
        .vkCreateFence = create_fence, .vkDestroyFence = destroy_fence, .vkResetFences = reset_fences,
        .vkBeginCommandBuffer = begin, .vkEndCommandBuffer = end, .vkCmdPipelineBarrier = barrier,
        .vkQueueSubmit = submit, .vkGetFenceStatus = fence_status, .vkQueueWaitIdle = idle
    };
    return value;
}

static doe_vk_commands *recording(doe_vk_context *ctx) {
    doe_vk_commands *commands = NULL;
    assert(doe_vk_commands_create(ctx, &commands, NULL) == DOE_VK_OK && commands);
    return commands;
}

int main(void) {
    doe_vk_context ctx = context();

    /* Never submitted: the pool is reset, the unsignaled fence kept, and the
     * next recording takes both instead of creating new ones. */
    doe_vk_commands *commands = recording(&ctx);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(ctx.command_spare_count == 1 && mock.pool_resets == 1 && mock.fence_resets == 0);
    assert(mock.pools_destroyed == 0 && mock.fences_destroyed == 0 && ctx.references == 1);
    commands = recording(&ctx);
    assert(ctx.command_spare_count == 0 && mock.pools_created == 1 && mock.fences_created == 1 && mock.begins == 2);

    /* Submitted work is not a spare until its fence proves completion. */
    assert(doe_vk_commands_submit(commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY && ctx.command_spare_count == 0);
    mock.fence_status = 1; /* VK_NOT_READY */
    assert(doe_vk_commands_poll(commands, NULL) == DOE_VK_PENDING);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY && ctx.command_spare_count == 0);
    mock.fence_status = 0;
    assert(doe_vk_commands_poll(commands, NULL) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(ctx.command_spare_count == 1 && mock.fence_resets == 1 && mock.pools_destroyed == 0);

    /* A refused submission recovered by queue idle is FAILED, never a spare. */
    commands = recording(&ctx);
    mock.submit_result = -1;
    assert(doe_vk_commands_submit(commands, NULL) != DOE_VK_OK);
    mock.submit_result = 0;
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    assert(doe_vk_commands_recover(commands, NULL) != DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(ctx.command_spare_count == 0 && mock.pools_destroyed == 1 && mock.fences_destroyed == 1);

    /* A fence-held recording is a spare only after the last holder unholds it. */
    commands = recording(&ctx);
    assert(doe_vk_commands_hold(commands) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK && ctx.command_spare_count == 0);
    doe_vk_commands_unhold(commands);
    assert(ctx.command_spare_count == 1);

    /* The list is bounded; a failed pool reset or a lost context destroys. */
    doe_vk_commands *many[DOE_VK_COMMAND_SPARES + 1];
    for (unsigned i = 0; i <= DOE_VK_COMMAND_SPARES; ++i) many[i] = recording(&ctx);
    const unsigned destroyed = mock.pools_destroyed;
    for (unsigned i = 0; i <= DOE_VK_COMMAND_SPARES; ++i) assert(doe_vk_commands_release(many[i]) == DOE_VK_OK);
    assert(ctx.command_spare_count == DOE_VK_COMMAND_SPARES && mock.pools_destroyed == destroyed + 1);
    commands = recording(&ctx);
    mock.reset_result = -1;
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(ctx.command_spare_count == DOE_VK_COMMAND_SPARES - 1 && mock.pools_destroyed == destroyed + 2);
    mock.reset_result = 0;
    commands = recording(&ctx);
    ctx.lost = true;
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(ctx.command_spare_count == DOE_VK_COMMAND_SPARES - 2 && mock.pools_destroyed == destroyed + 3);
    ctx.lost = false;

    /* Loss does not prove submitted work finished: the first lost completion
     * waits for the queue before any lease is dropped, then destroys rather
     * than recycles; later lost completions do not wait again. */
    ctx.queue = (VkQueue)(uintptr_t)0x51;
    doe_vk_commands *first = recording(&ctx), *second = recording(&ctx);
    assert(doe_vk_commands_submit(first, NULL) == DOE_VK_OK && doe_vk_commands_submit(second, NULL) == DOE_VK_OK);
    const unsigned idles = mock.idles, spares = ctx.command_spare_count;
    ctx.lost = true;
    assert(doe_vk_commands_poll(first, NULL) == DOE_VK_DEVICE_LOST && mock.idles == idles + 1);
    assert(doe_vk_commands_release(second) == DOE_VK_OK && mock.idles == idles + 1);
    assert(doe_vk_commands_release(first) == DOE_VK_OK && ctx.command_spare_count == spares);
    ctx.lost = false;
    ctx.lost_drained = false;

    /* Device teardown destroys every spare. */
    doe_vk_command_spares_clear(&ctx);
    assert(ctx.command_spare_count == 0 && ctx.references == 1);
    assert(mock.pools_destroyed == mock.pools_created && mock.fences_destroyed == mock.fences_created);
    assert(mock.buffers_allocated == mock.pools_created);
    puts("PASS command pools and fences become spares only after proven-terminal or never-submitted recordings; bounded, reset and destroyed with the device; a lost device drains its queue once before release");
    return 0;
}
