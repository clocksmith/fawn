/* SPDX-License-Identifier: MIT */
#include "../src/surface_internal.h"
#include "../src/feature_internal.h"
#include <assert.h>
#include <stdio.h>

static VkResult acquire_result, wait_result, present_result, release_result, submit_result, fence_result;
static unsigned acquires, presents, releases, barriers, submits, destroyed_fences, destroyed_semaphores;
static unsigned platform_creates, surface_destroys, swapchain_destroys;
static uint64_t next_fence = 100;
static VkSwapchainKHR expected_old_swapchain;
static bool queue_supported = true;
static VkResult create_xcb(VkInstance instance, const VkXcbSurfaceCreateInfoKHR *info,
    const VkAllocationCallbacks *allocator, VkSurfaceKHR *out) {
    (void)instance; (void)allocator;
    assert(info->sType == VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR && info->connection != NULL && info->window == 7);
    ++platform_creates;
    *out = 71;
    return 0;
}
static void destroy_surface(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *allocator) {
    (void)instance; (void)allocator;
    assert(surface == 71);
    ++surface_destroys;
}
static VkResult support(VkPhysicalDevice physical, uint32_t queue, VkSurfaceKHR surface, VkBool32 *out) {
    (void)physical;
    assert(queue == 2 && surface == 71);
    *out = queue_supported;
    return 0;
}
static VkResult capabilities(VkPhysicalDevice physical, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *out) {
    (void)physical;
    assert(surface == 71);
    *out = (VkSurfaceCapabilitiesKHR){.minImageCount = 2, .maxImageCount = 4,
        .currentExtent = {UINT32_MAX, UINT32_MAX}, .minImageExtent = {1, 1}, .maxImageExtent = {64, 64},
        .maxImageArrayLayers = 1, .supportedTransforms = 1, .currentTransform = 1,
        .supportedCompositeAlpha = 1, .supportedUsageFlags = 19};
    return 0;
}
static VkResult formats(VkPhysicalDevice physical, VkSurfaceKHR surface, uint32_t *count, VkSurfaceFormatKHR *out) {
    (void)physical;
    assert(surface == 71 && *count >= 1);
    *count = 1;
    out[0] = (VkSurfaceFormatKHR){DOE_VK_FORMAT_RGBA8_UNORM, 0};
    return 0;
}
static VkResult modes(VkPhysicalDevice physical, VkSurfaceKHR surface, uint32_t *count, uint32_t *out) {
    (void)physical;
    assert(surface == 71 && *count >= 2);
    *count = 2;
    out[0] = DOE_VK_PRESENT_FIFO;
    out[1] = DOE_VK_PRESENT_MAILBOX;
    assert(DOE_VK_PRESENT_MAILBOX == 1 && DOE_VK_PRESENT_FIFO_RELAXED == 3);
    return 0;
}
static void format_properties(VkPhysicalDevice physical, uint32_t format, VkFormatProperties *out) {
    (void)physical;
    assert(format == DOE_VK_FORMAT_RGBA8_UNORM);
    *out = (VkFormatProperties){.optimalTilingFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT};
}
static VkResult image_properties(VkPhysicalDevice physical, uint32_t format, uint32_t type,
    uint32_t tiling, VkFlags usage, VkFlags flags, VkImageFormatProperties *out) {
    (void)physical; (void)format; (void)type; (void)tiling; (void)usage; (void)flags;
    *out = (VkImageFormatProperties){.maxExtent = {64, 64, 1}, .maxMipLevels = 1,
        .maxArrayLayers = 1, .sampleCounts = 1};
    return 0;
}
static VkResult create_swapchain(VkDevice device, const VkSwapchainCreateInfoKHR *info,
    const VkAllocationCallbacks *allocator, VkSwapchainKHR *out) {
    (void)device; (void)allocator;
    assert(info->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR && info->surface == 71);
    assert(info->minImageCount == 3 && info->imageExtent.width == 8 && info->imageExtent.height == 8);
    assert(info->imageFormat == DOE_VK_FORMAT_RGBA8_UNORM && info->presentMode == 1);
    assert(info->imageArrayLayers == 1 && info->oldSwapchain == expected_old_swapchain && info->imageSharingMode == 0);
    *out = 81;
    return 0;
}
static void destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(swapchain == 81);
    ++swapchain_destroys;
}
static VkResult images(VkDevice device, VkSwapchainKHR swapchain, uint32_t *count, VkImage *out) {
    (void)device;
    assert(swapchain == 81 && *count >= 3);
    *count = 3;
    out[0] = 91; out[1] = 92; out[2] = 93;
    return 0;
}
static VkResult create_fence(VkDevice device, const VkFenceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFence *out) {
    (void)device; (void)allocator;
    assert(info->sType == VK_STRUCTURE_TYPE_FENCE_CREATE_INFO && info->flags == 0);
    *out = next_fence++;
    return fence_result;
}
static void destroy_fence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(fence >= 99);
    ++destroyed_fences;
}
static VkResult acquire(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
    VkSemaphore semaphore, VkFence fence, uint32_t *index) {
    (void)device;
    assert(swapchain == 81 && timeout != UINT64_MAX && semaphore == 0 && fence >= 100);
    ++acquires;
    if (acquire_result == 0 || acquire_result == VK_SUBOPTIMAL_KHR) *index = 1;
    return acquire_result;
}
static VkResult wait_fence(VkDevice device, uint32_t count, const VkFence *fences, VkBool32 all, uint64_t timeout) {
    (void)device; (void)timeout;
    assert(count == 1 && *fences >= 99 && all == 1);
    return wait_result;
}
static VkResult create_semaphore(VkDevice device, const VkSemaphoreCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkSemaphore *out) {
    (void)device; (void)allocator;
    assert(info->sType == VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO && info->flags == 0);
    *out = 31;
    return 0;
}
static void destroy_semaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(semaphore == 31);
    ++destroyed_semaphores;
}
static VkResult present(VkQueue queue, const VkPresentInfoKHR *info) {
    (void)queue;
    assert(info->sType == VK_STRUCTURE_TYPE_PRESENT_INFO_KHR && info->waitSemaphoreCount == 1);
    assert(*info->pWaitSemaphores == 31 && info->swapchainCount == 1 && *info->pSwapchains == 81 && *info->pImageIndices == 1);
    const VkSwapchainPresentFenceInfoEXT *fence = info->pNext;
    assert(fence != NULL && fence->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT);
    assert(fence->swapchainCount == 1 && *fence->pFences >= 100);
    ++presents;
    return present_result;
}
static VkResult release_images(VkDevice device, const VkReleaseSwapchainImagesInfoEXT *info) {
    (void)device;
    assert(info->sType == VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_EXT);
    assert(info->swapchain == 81 && info->imageIndexCount == 1 && *info->pImageIndices == 1);
    ++releases;
    return release_result;
}
static VkResult create_view(VkDevice device, const VkImageViewCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkImageView *out) {
    (void)device; (void)allocator;
    assert(info->image == 92);
    *out = 51;
    return 0;
}
static void destroy_view(VkDevice device, VkImageView view, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(view == 51);
}
static void barrier(VkCommandBuffer command, VkFlags src, VkFlags dst, VkFlags flags,
    uint32_t memory_count, const void *memory, uint32_t buffer_count, const void *buffers,
    uint32_t image_count, const void *images_) {
    (void)command; (void)src; (void)dst; (void)flags; (void)memory; (void)buffers;
    assert(buffer_count == 0 && memory_count + image_count == 1);
    if (image_count != 0) {
        const VkImageMemoryBarrier *image = images_;
        assert(image->image == 92 && image->newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    ++barriers;
}
static VkResult end_command(VkCommandBuffer command) { (void)command; return 0; }
static VkResult submit(VkQueue queue, uint32_t count, const VkSubmitInfo *info, VkFence fence) {
    (void)queue;
    assert(count == 1 && info->commandBufferCount == 1 && fence == 99);
    assert(info->signalSemaphoreCount == 1 && *info->pSignalSemaphores == 31 && barriers >= 2);
    ++submits;
    return submit_result;
}
static doe_vk_commands *recording(doe_vk_context *context) {
    doe_vk_commands *commands = calloc(1, sizeof(*commands));
    assert(commands != NULL && doe_vk_context_retain(context) == DOE_VK_OK);
    *commands = (doe_vk_commands){.context = context, .state = DOE_VK_RECORDING, .uses_active = true, .fence = 99};
    return commands;
}
int main(void) {
    doe_vk_context context = {.references = 1};
    context.caps.queue_family = 2;
    context.caps.queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    context.caps.max_color_attachments = 8;
    context.caps.surface_platforms = DOE_VK_SURFACE_XCB;
    context.vk = (doe_vk_functions){.vkCreateXcbSurfaceKHR = create_xcb, .vkDestroySurfaceKHR = destroy_surface,
        .vkGetPhysicalDeviceSurfaceSupportKHR = support, .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = capabilities,
        .vkGetPhysicalDeviceSurfaceFormatsKHR = formats, .vkGetPhysicalDeviceSurfacePresentModesKHR = modes,
        .vkGetPhysicalDeviceFormatProperties = format_properties, .vkGetPhysicalDeviceImageFormatProperties = image_properties,
        .vkCreateSwapchainKHR = create_swapchain, .vkDestroySwapchainKHR = destroy_swapchain, .vkGetSwapchainImagesKHR = images,
        .vkCreateFence = create_fence, .vkDestroyFence = destroy_fence, .vkAcquireNextImageKHR = acquire,
        .vkWaitForFences = wait_fence, .vkCreateSemaphore = create_semaphore, .vkDestroySemaphore = destroy_semaphore,
        .vkQueuePresentKHR = present, .vkReleaseSwapchainImagesEXT = release_images,
        .vkCreateImageView = create_view, .vkDestroyImageView = destroy_view, .vkCmdPipelineBarrier = barrier,
        .vkEndCommandBuffer = end_command, .vkQueueSubmit = submit};
    const doe_vk_surface_desc desc = {.platform = DOE_VK_SURFACE_XCB, .display = &context, .window.xcb_window = 7};
    doe_vk_surface *surface = NULL;
    doe_vk_error error;
    queue_supported = false;
    assert(doe_vk_surface_create(&context, &desc, &surface, &error) == DOE_VK_UNSUPPORTED);
    assert(surface == NULL && context.references == 1 && platform_creates == 1 && surface_destroys == 1);
    queue_supported = true;
    assert(doe_vk_surface_create(&context, &desc, &surface, &error) == DOE_VK_OK);
    doe_vk_surface_config config = {.width = 8, .height = 8, .format = DOE_VK_FORMAT_RGBA8_UNORM,
        .usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT | DOE_VK_TEXTURE_COPY_SRC,
        .present_mode = DOE_VK_PRESENT_MAILBOX, .alpha_mode = 1};
    config.present_mode = DOE_VK_PRESENT_FIFO_RELAXED;
    assert(doe_vk_surface_configure(surface, &config, &error) == DOE_VK_UNSUPPORTED && surface->swapchain == 0);
    config.present_mode = DOE_VK_PRESENT_MAILBOX;
    assert(doe_vk_surface_configure(surface, &config, &error) == DOE_VK_OK);
    assert(doe_vk_surface_configure(surface, &config, &error) == DOE_VK_BUSY);
    doe_vk_surface *replacement = NULL;
    config.present_mode = DOE_VK_PRESENT_FIFO_RELAXED;
    assert(doe_vk_surface_replace(surface, &config, &replacement, &error) == DOE_VK_UNSUPPORTED);
    assert(!replacement && surface->swapchain_status == DOE_VK_OK && surface->source->references == 1);
    config.present_mode = DOE_VK_PRESENT_MAILBOX;
    expected_old_swapchain = surface->swapchain;
    assert(doe_vk_surface_replace(surface, &config, &replacement, &error) == DOE_VK_OK);
    assert(surface->swapchain_status == DOE_VK_SURFACE_OUTDATED && surface->source == replacement->source);
    assert(surface->source->references == 2 && surface_destroys == 1);
    assert(doe_vk_surface_release(surface) == DOE_VK_OK && surface_destroys == 1);
    surface = replacement;
    expected_old_swapchain = 0;
    doe_vk_texture *texture = NULL;
    bool suboptimal = true;
    assert(doe_vk_surface_acquire(surface, UINT64_MAX, &texture, &suboptimal, &error) == DOE_VK_INVALID_ARGUMENT);
    fence_result = -1;
    assert(doe_vk_surface_acquire(surface, 0, &texture, &suboptimal, &error) == DOE_VK_OUT_OF_MEMORY);
    assert(texture == NULL && surface->acquire_fence == 0 && acquires == 0);
    fence_result = 0;
    wait_result = 2;
    assert(doe_vk_surface_acquire(surface, 0, &texture, &suboptimal, &error) == DOE_VK_TIMEOUT);
    assert(texture == NULL && suboptimal && acquires == 1 && surface->acquire_pending);
    assert(doe_vk_surface_release(surface) == DOE_VK_BUSY);
    assert(doe_vk_surface_unconfigure(surface, &error) == DOE_VK_BUSY);
    assert(doe_vk_surface_retire(surface, 0, &error) == DOE_VK_TIMEOUT && acquires == 1);
    wait_result = 0;
    assert(doe_vk_surface_acquire(surface, 0, &texture, &suboptimal, &error) == DOE_VK_OK);
    assert(acquires == 1 && !suboptimal && texture->image == 92 && texture->borrowed_image && texture->external_alive);
    texture->initialized[0] = 1; /* Fault fixture starts after an independently completed clear. */
    doe_vk_commands *commands = recording(&context);
    assert(doe_vk_surface_prepare_present(surface, commands, &error) == DOE_VK_OK && surface->references == 2);
    assert(doe_vk_surface_present(surface, commands, &suboptimal, &error) == DOE_VK_INVALID_STATE);
    assert(doe_vk_commands_submit(commands, &error) == DOE_VK_OK && submits == 1);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    assert(doe_vk_surface_discard(surface, &error) == DOE_VK_BUSY);
    assert(doe_vk_commands_wait(commands, 0, &error) == DOE_VK_OK);
    doe_vk_commands *later = recording(&context);
    doe_vk_texture_use *not_published = NULL;
    assert(doe_vk_use_texture(later, texture, &not_published) == DOE_VK_BUSY && not_published == NULL);
    assert(doe_vk_commands_release(later) == DOE_VK_OK);
    present_result = -1;
    assert(doe_vk_surface_present(surface, commands, &suboptimal, &error) == DOE_VK_OUT_OF_MEMORY);
    assert(!surface->present_pending && texture->external_alive && surface->present_fence == 0);
    present_result = VK_ERROR_OUT_OF_DATE_KHR;
    assert(doe_vk_surface_present(surface, commands, &suboptimal, &error) == DOE_VK_SURFACE_OUTDATED);
    assert(surface->present_pending && !texture->external_alive && !suboptimal);
    assert(doe_vk_commands_wait(commands, 0, &error) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY); /* Render completion is not present retirement. */
    unsigned before = destroyed_semaphores;
    wait_result = 2;
    assert(doe_vk_surface_retire(surface, 0, &error) == DOE_VK_TIMEOUT);
    wait_result = -13;
    assert(doe_vk_surface_wait(surface, 0, &error) == DOE_VK_UNSUPPORTED);
    assert(surface->present_pending && destroyed_semaphores == before);
    wait_result = 0;
    assert(doe_vk_surface_retire_frame(surface, 0, &error) == DOE_VK_SURFACE_OUTDATED);
    assert(surface->swapchain == 81);
    assert(!surface->present_pending && !surface->acquired && destroyed_semaphores == before + 1);
    doe_vk_texture_view *view = NULL;
    const doe_vk_texture_view_desc view_desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1, DOE_VK_ASPECT_ALL, 0, {0}};
    assert(doe_vk_texture_view_create(texture, &view_desc, &view, &error) == DOE_VK_INVALID_STATE && view == NULL);
    assert(doe_vk_texture_retain(texture) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK && surface->references == 1);
    doe_vk_texture *kept = texture;
    assert(doe_vk_surface_acquire(surface, 0, &texture, &suboptimal, &error) == DOE_VK_OK);
    assert(texture != kept && !kept->external_alive);
    assert(doe_vk_surface_discard(surface, &error) == DOE_VK_OK);
    assert(doe_vk_surface_unconfigure(surface, &error) == DOE_VK_BUSY);
    doe_vk_texture_release(kept);
    acquire_result = VK_SUBOPTIMAL_KHR;
    assert(doe_vk_surface_acquire(surface, 0, &texture, &suboptimal, &error) == DOE_VK_OK && suboptimal);
    commands = recording(&context);
    assert(doe_vk_surface_prepare_present(surface, commands, &error) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK); /* Abandoned recording detaches the lease. */
    release_result = -1;
    assert(doe_vk_surface_discard(surface, &error) == DOE_VK_OUT_OF_MEMORY && surface->acquired);
    release_result = 0;
    assert(doe_vk_surface_discard(surface, &error) == DOE_VK_OK && !texture->external_alive);
    unsigned acquisition_count = acquires;
    assert(doe_vk_surface_retire(surface, 0, &error) == DOE_VK_OK && surface->swapchain == 0);
    assert(doe_vk_surface_retire(surface, 0, &error) == DOE_VK_OK && acquires == acquisition_count);
    /* A refused image release on a lost surface ends ownership: later
     * acquisition reports the loss without the native call, teardown works. */
    assert(doe_vk_surface_configure(surface, &config, &error) == DOE_VK_OK);
    acquire_result = 0;
    assert(doe_vk_surface_acquire(surface, 0, &texture, &suboptimal, &error) == DOE_VK_OK);
    release_result = -1000000000; /* VK_ERROR_SURFACE_LOST_KHR */
    assert(doe_vk_surface_discard(surface, &error) == DOE_VK_SURFACE_LOST && !surface->acquired);
    release_result = 0;
    acquisition_count = acquires;
    assert(doe_vk_surface_acquire(surface, 0, &texture, &suboptimal, &error) == DOE_VK_SURFACE_LOST);
    assert(acquires == acquisition_count && !texture->external_alive);
    assert(doe_vk_surface_retire(surface, 0, &error) == DOE_VK_OK && surface->swapchain == 0);
    assert(doe_vk_surface_configure(surface, &config, &error) == DOE_VK_OK);
    acquire_result = 0;
    assert(doe_vk_surface_acquire(surface, 0, &texture, &suboptimal, &error) == DOE_VK_OK);
    texture->initialized[0] = 1;
    commands = recording(&context);
    assert(doe_vk_surface_prepare_present(surface, commands, &error) == DOE_VK_OK);
    assert(doe_vk_commands_submit(commands, &error) == DOE_VK_OK);
    present_result = VK_SUBOPTIMAL_KHR;
    assert(doe_vk_surface_present(surface, commands, &suboptimal, &error) == DOE_VK_OK && suboptimal);
    assert(doe_vk_surface_retire_frame(surface, 0, &error) == DOE_VK_OK && surface->swapchain == 81);
    assert(doe_vk_surface_retire(surface, 0, &error) == DOE_VK_BUSY); /* Never releases caller commands. */
    assert(doe_vk_commands_wait(commands, 0, &error) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(doe_vk_surface_retire(surface, 0, &error) == DOE_VK_OK);
    assert(doe_vk_surface_configure(surface, &config, &error) == DOE_VK_OK);
    acquisition_count = acquires;
    acquire_result = -13;
    assert(doe_vk_surface_acquire(surface, 0, &texture, &suboptimal, &error) == DOE_VK_UNSUPPORTED);
    assert(surface->uncertain_acquire && surface->acquire_pending);
    assert(doe_vk_surface_acquire(surface, 0, &texture, &suboptimal, &error) == DOE_VK_NATIVE_FAILURE);
    assert(doe_vk_surface_release(surface) == DOE_VK_BUSY);
    assert(doe_vk_surface_retire(surface, 0, &error) == DOE_VK_NATIVE_FAILURE && acquires == acquisition_count + 1);
    context.lost = true;
    assert(doe_vk_surface_release(surface) == DOE_VK_OK);
    assert(context.references == 1 && swapchain_destroys == 5 && surface_destroys == 2);
    assert(presents == 3 && releases == 4 && destroyed_fences > 0);
    puts("PASS WSI fault-only native boundaries: exact queue/platform/mode admission, acquire retry and output preservation, existing submit signals semaphore, present OOM retry, separate unknown retirement, stale image and caller leases, real discard entrypoint, refused release on surface loss, canonical retirement never acquires a fresh image, terminal device loss; no window-system execution claim");
    return 0;
}
