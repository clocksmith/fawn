/* SPDX-License-Identifier: MIT */
#include "surface_internal.h"

doe_vk_status doe_vk_surface_fence(doe_vk_surface *surface, VkFence *out, doe_vk_error *error) {
    const VkFenceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = 0;
    doe_vk_status status = doe_vk_result(surface->context, surface->context->vk.vkCreateFence(surface->context->device,
        &create, NULL, &fence), "surface vkCreateFence", error);
    if (status == DOE_VK_OK) *out = fence;
    return status;
}

static doe_vk_status frame_texture(doe_vk_surface *surface, doe_vk_error *error) {
    if (surface->texture != NULL) {
        if (surface->texture->references != 1 || surface->texture->recording_owner != NULL)
            return doe_vk_fail(DOE_VK_BUSY, "previous surface texture leases", error);
        doe_vk_texture_release(surface->texture);
        surface->texture = NULL;
    }
    doe_vk_texture *texture = calloc(1, sizeof(*texture));
    if (texture == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "acquisition texture owner", error);
    texture->initialized = calloc(1, 1);
    if (texture->initialized == NULL) {
        free(texture);
        return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "acquisition texture state", error);
    }
    doe_vk_status status = doe_vk_context_retain(surface->context);
    if (status != DOE_VK_OK) {
        free(texture->initialized);
        free(texture);
        return doe_vk_fail(status, "acquisition texture context", error);
    }
    texture->context = surface->context;
    texture->references = 1;
    texture->texel_bytes = 4;
    texture->subresource_count = 1;
    texture->borrowed_image = true;
    texture->desc = (doe_vk_texture_desc){.dimension = DOE_VK_TEXTURE_2D, .format = surface->config.format,
        .width = surface->config.width, .height = surface->config.height, .depth_or_layers = 1,
        .mip_levels = 1, .sample_count = 1, .usage = surface->config.usage};
    surface->texture = texture;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_surface_acquire(doe_vk_surface *surface, uint64_t timeout_ns,
    doe_vk_texture **out, bool *suboptimal, doe_vk_error *error) {
    if (surface == NULL || out == NULL || suboptimal == NULL || timeout_ns == UINT64_MAX)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "bounded surface acquisition", error);
    doe_vk_context *context = surface->context;
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "acquire context", error);
    if (surface->swapchain == 0) return doe_vk_fail(DOE_VK_INVALID_STATE, "surface not configured", error);
    if (surface->acquired || surface->present_pending || surface->commands != NULL)
        return doe_vk_fail(DOE_VK_BUSY, "surface frame still owned", error);
    if (!surface->acquire_pending) {
        doe_vk_status status = frame_texture(surface, error);
        if (status != DOE_VK_OK) return status;
        status = doe_vk_surface_fence(surface, &surface->acquire_fence, error);
        if (status != DOE_VK_OK) return status;
        uint32_t index = UINT32_MAX;
        VkResult result = context->vk.vkAcquireNextImageKHR(context->device, surface->swapchain,
            timeout_ns, 0, surface->acquire_fence, &index);
        if (result == 0 || result == VK_SUBOPTIMAL_KHR) {
            surface->image_index = index;
            surface->acquire_suboptimal = result == VK_SUBOPTIMAL_KHR;
            surface->acquire_pending = true;
            surface->uncertain_acquire = index >= surface->image_count;
        } else {
            status = doe_vk_result(context, result, "vkAcquireNextImageKHR", error);
            if (result == 1 || result == 2 || result == -1 || result == -2 || result == -4 ||
                result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
                context->vk.vkDestroyFence(context->device, surface->acquire_fence, NULL);
                surface->acquire_fence = 0;
            } else {
                surface->acquire_pending = true;
                surface->uncertain_acquire = true;
            }
            return status;
        }
    }
    /* The acquire timeout and fence timeout are individually bounded. Total
     * call wait is at most twice timeout_ns; caller wall-clock bounds still apply. */
    doe_vk_status status = doe_vk_result(context, context->vk.vkWaitForFences(context->device, 1,
        &surface->acquire_fence, 1, timeout_ns), "acquire fence", error);
    if (status != DOE_VK_OK) return status;
    if (surface->uncertain_acquire)
        return doe_vk_fail(DOE_VK_NATIVE_FAILURE, "acquire result unknown; ownership retained", error);
    context->vk.vkDestroyFence(context->device, surface->acquire_fence, NULL);
    surface->acquire_fence = 0;
    surface->acquire_pending = false;
    surface->acquired = true;
    surface->texture->image = surface->images[surface->image_index];
    surface->texture->external_alive = true;
    *out = surface->texture;
    *suboptimal = surface->acquire_suboptimal;
    return doe_vk_fail(DOE_VK_OK, "surface image acquired", error);
}

doe_vk_status doe_vk_surface_prepare_present(doe_vk_surface *surface,
    doe_vk_commands *commands, doe_vk_error *error) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "prepare present commands", error);
    if (surface == NULL || surface->context != commands->context)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "prepare present context", error);
    if (!surface->acquired || surface->acquire_pending || surface->present_pending ||
        surface->commands != NULL || commands->surface != NULL)
        return doe_vk_fail(DOE_VK_BUSY, "prepare present frame ownership", error);
    if (surface->texture->recording_owner != NULL && surface->texture->recording_owner != commands)
        return doe_vk_fail(DOE_VK_BUSY, "surface image recorded elsewhere", error);
    VkSemaphore semaphore = 0;
    const VkSemaphoreCreateInfo create = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    status = doe_vk_result(surface->context, surface->context->vk.vkCreateSemaphore(surface->context->device,
        &create, NULL, &semaphore), "render-finished semaphore", error);
    if (status != DOE_VK_OK) return status;
    status = doe_vk_surface_retain(surface);
    if (status != DOE_VK_OK) {
        surface->context->vk.vkDestroySemaphore(surface->context->device, semaphore, NULL);
        return doe_vk_fail(status, "present command lease", error);
    }
    surface->render_finished = semaphore;
    surface->commands = commands;
    surface->texture->external_owner = commands;
    commands->surface = surface;
    return doe_vk_fail(DOE_VK_OK, "present attached to existing commands", error);
}

doe_vk_status doe_vk_surface_submit_prepare(doe_vk_commands *commands, doe_vk_error *error) {
    doe_vk_surface *surface = commands->surface;
    if (surface == NULL) return DOE_VK_OK;
    if (surface->commands != commands || !surface->acquired || !surface->texture->external_alive)
        return doe_vk_fail(DOE_VK_INVALID_STATE, "present frame changed before submit", error);
    doe_vk_texture_view *view = NULL;
    const doe_vk_texture_view_desc desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1, DOE_VK_ASPECT_ALL};
    doe_vk_status status = doe_vk_texture_view_create(surface->texture, &desc, &view, error);
    if (status != DOE_VK_OK) return status;
    status = doe_vk_commands_initialize_attachment(commands, view);
    doe_vk_texture_view_release(view);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "present initializes undefined attachment", error);
    doe_vk_texture_use *use = NULL;
    status = doe_vk_use_texture(commands, surface->texture, &use);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "present image lease", error);
    doe_vk_texture_transition(commands, use, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_surface_present(doe_vk_surface *surface,
    doe_vk_commands *commands, bool *suboptimal, doe_vk_error *error) {
    if (surface == NULL || commands == NULL || suboptimal == NULL || commands->surface != surface || surface->commands != commands)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "present matching command lease", error);
    doe_vk_context *context = surface->context;
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "present context", error);
    if (!surface->acquired || surface->present_pending ||
        (commands->state != DOE_VK_SUBMITTED && commands->state != DOE_VK_COMPLETE))
        return doe_vk_fail(DOE_VK_INVALID_STATE, "present requires successful render submission", error);
    doe_vk_status status = doe_vk_surface_fence(surface, &surface->present_fence, error);
    if (status != DOE_VK_OK) return status;
    const VkSwapchainPresentFenceInfoEXT fence = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
        .swapchainCount = 1, .pFences = &surface->present_fence};
    const VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .pNext = &fence,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &surface->render_finished,
        .swapchainCount = 1, .pSwapchains = &surface->swapchain, .pImageIndices = &surface->image_index};
    VkResult result = context->vk.vkQueuePresentKHR(context->queue, &present);
    if (result == -1 || result == -2) {
        /* Vulkan guarantees failed enqueue leaves synchronization unaffected. */
        context->vk.vkDestroyFence(context->device, surface->present_fence, NULL);
        surface->present_fence = 0;
        return doe_vk_result(context, result, "vkQueuePresentKHR enqueue", error);
    }
    status = result == VK_SUBOPTIMAL_KHR ? DOE_VK_OK : doe_vk_result(context, result, "vkQueuePresentKHR", error);
    surface->present_status = status;
    surface->present_native_result = result;
    surface->present_pending = true;
    surface->texture->external_alive = false;
    if (status == DOE_VK_OK) {
        *suboptimal = result == VK_SUBOPTIMAL_KHR;
        return doe_vk_fail(DOE_VK_OK, "presentation enqueued, retirement pending", error);
    }
    return status;
}

doe_vk_status doe_vk_surface_wait(doe_vk_surface *surface, uint64_t timeout_ns, doe_vk_error *error) {
    if (surface == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "surface wait", error);
    doe_vk_context *context = surface->context;
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "surface wait context", error);
    if (!surface->present_pending) return doe_vk_fail(DOE_VK_OK, "no pending presentation", error);
    doe_vk_status status = doe_vk_result(context, context->vk.vkWaitForFences(context->device, 1,
        &surface->present_fence, 1, timeout_ns), "presentation retirement fence", error);
    if (status != DOE_VK_OK) return status;
    surface->present_pending = false;
    surface->acquired = false;
    doe_vk_surface_sync_destroy(surface);
    /* This retires presentation resources, not display timing or the separate
     * command-completion state. That owner must still observe its own fence. */
    if (surface->present_status != DOE_VK_OK)
        return doe_vk_result(context, surface->present_native_result, "presentation resources retired after failed present", error);
    return doe_vk_fail(DOE_VK_OK, "presentation resources retired", error);
}

doe_vk_status doe_vk_surface_discard(doe_vk_surface *surface, doe_vk_error *error) {
    if (surface == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "surface discard", error);
    doe_vk_context *context = surface->context;
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "surface discard context", error);
    if (!surface->acquired || surface->acquire_pending || surface->present_pending)
        return doe_vk_fail(DOE_VK_INVALID_STATE, "surface discard acquisition", error);
    if (surface->texture->recording_owner != NULL || (surface->commands != NULL &&
        surface->commands->state != DOE_VK_COMPLETE && surface->commands->state != DOE_VK_FAILED))
        return doe_vk_fail(DOE_VK_BUSY, "surface render work unresolved", error);
    const VkReleaseSwapchainImagesInfoEXT release = {.sType = VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_EXT,
        .swapchain = surface->swapchain, .imageIndexCount = 1, .pImageIndices = &surface->image_index};
    doe_vk_status status = doe_vk_result(context, context->vk.vkReleaseSwapchainImagesEXT(context->device, &release),
        "vkReleaseSwapchainImagesEXT", error);
    if (status != DOE_VK_OK) return status;
    surface->acquired = false;
    surface->texture->external_alive = false;
    doe_vk_surface_sync_destroy(surface);
    return doe_vk_fail(DOE_VK_OK, "acquired image discarded", error);
}

doe_vk_status doe_vk_surface_commands_release(doe_vk_commands *commands) {
    doe_vk_surface *surface = commands->surface;
    if (surface == NULL) return DOE_VK_OK;
    if (!surface->context->lost && (surface->present_pending ||
        (surface->acquired && (commands->state == DOE_VK_COMPLETE || commands->state == DOE_VK_FAILED)))) return DOE_VK_BUSY;
    if (surface->commands == commands) surface->commands = NULL;
    if (surface->texture != NULL && surface->texture->external_owner == commands) surface->texture->external_owner = NULL;
    if (!surface->present_pending) doe_vk_surface_sync_destroy(surface);
    /* A public reference cannot be dropped while the surface is busy, so the
     * command lease is never the final reference. No recursive teardown. */
    --surface->references;
    commands->surface = NULL;
    return DOE_VK_OK;
}


doe_vk_status doe_vk_surface_retire_frame(doe_vk_surface *surface,
    uint64_t timeout_ns, doe_vk_error *error) {
    if (surface == NULL || timeout_ns == UINT64_MAX)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "bounded surface retirement", error);
    if (surface->context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "frame retirement context", error);
    if (surface->acquire_pending) {
        doe_vk_texture *borrowed = NULL;
        bool suboptimal = false;
        doe_vk_status status = doe_vk_surface_acquire(surface, timeout_ns, &borrowed, &suboptimal, error);
        if (status != DOE_VK_OK) return status;
    }
    if (surface->present_pending) {
        doe_vk_status status = doe_vk_surface_wait(surface, timeout_ns, error);
        if (status != DOE_VK_OK) return status;
    }
    if (surface->acquired) {
        doe_vk_status status = doe_vk_surface_discard(surface, error);
        if (status != DOE_VK_OK) return status;
    }
    return doe_vk_fail(DOE_VK_OK, "surface frame retired", error);
}


doe_vk_status doe_vk_surface_retire(doe_vk_surface *surface,
    uint64_t timeout_ns, doe_vk_error *error) {
    if (surface == NULL || timeout_ns == UINT64_MAX)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "bounded surface retirement", error);
    if (!surface->context->lost) {
        doe_vk_status status = doe_vk_surface_retire_frame(surface, timeout_ns, error);
        if (status != DOE_VK_OK) return status;
    }
    return doe_vk_surface_unconfigure(surface, error);
}
