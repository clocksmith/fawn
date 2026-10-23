/* SPDX-License-Identifier: MIT */
#include "surface_internal.h"

bool doe_vk_surface_busy(const doe_vk_surface *surface) {
    return surface->commands != NULL || (surface->texture != NULL &&
        (surface->texture->references != 1 || surface->texture->recording_owner != NULL)) ||
        (!surface->context->lost && (surface->acquired || surface->acquire_pending || surface->present_pending));
}

void doe_vk_surface_sync_destroy(doe_vk_surface *surface) {
    doe_vk_context *context = surface->context;
    if (surface->acquire_fence != 0) context->vk.vkDestroyFence(context->device, surface->acquire_fence, NULL);
    if (surface->present_fence != 0) context->vk.vkDestroyFence(context->device, surface->present_fence, NULL);
    if (surface->render_finished != 0) context->vk.vkDestroySemaphore(context->device, surface->render_finished, NULL);
    surface->acquire_fence = surface->present_fence = surface->render_finished = 0;
}

doe_vk_status doe_vk_surface_retain(doe_vk_surface *surface) {
    if (surface == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (surface->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++surface->references;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_surface_unconfigure(doe_vk_surface *surface, doe_vk_error *error) {
    if (surface == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "surface unconfigure", error);
    if (doe_vk_surface_busy(surface)) return doe_vk_fail(DOE_VK_BUSY, "surface references unresolved", error);
    doe_vk_texture_release(surface->texture);
    surface->texture = NULL;
    doe_vk_surface_sync_destroy(surface);
    if (surface->swapchain != 0)
        surface->context->vk.vkDestroySwapchainKHR(surface->context->device, surface->swapchain, NULL);
    surface->swapchain = 0;
    surface->image_count = 0;
    surface->acquired = surface->acquire_pending = surface->present_pending = false;
    return doe_vk_fail(DOE_VK_OK, "surface unconfigured", error);
}

doe_vk_status doe_vk_surface_release(doe_vk_surface *surface) {
    if (surface == NULL) return DOE_VK_OK;
    if (doe_vk_surface_busy(surface)) return DOE_VK_BUSY;
    if (--surface->references != 0) return DOE_VK_OK;
    doe_vk_context *context = surface->context;
    doe_vk_status status = doe_vk_surface_unconfigure(surface, NULL);
    if (status != DOE_VK_OK) { /* Defensive: serialization is part of this ABI. */
        ++surface->references;
        return status;
    }
    if (surface->surface != 0) context->vk.vkDestroySurfaceKHR(context->instance, surface->surface, NULL);
    free(surface);
    doe_vk_context_release(context);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_surface_create(doe_vk_context *context,
    const doe_vk_surface_desc *desc, doe_vk_surface **out, doe_vk_error *error) {
    if (context == NULL || desc == NULL || out == NULL || desc->display == NULL ||
        desc->platform == 0 || (desc->platform & (desc->platform - 1)) != 0 || (desc->platform & ~UINT32_C(7)) != 0)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "surface descriptor", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "surface context", error);
    if (!(context->caps.surface_platforms & desc->platform))
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "surface platform not enabled", error);
    if ((desc->platform == DOE_VK_SURFACE_WAYLAND && desc->window.wayland_surface == NULL) ||
        (desc->platform == DOE_VK_SURFACE_XCB && desc->window.xcb_window == 0) ||
        (desc->platform == DOE_VK_SURFACE_XLIB && desc->window.xlib_window == 0))
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "surface window", error);
    doe_vk_surface *surface = calloc(1, sizeof(*surface));
    if (surface == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "surface owner", error);
    doe_vk_status status = doe_vk_context_retain(context);
    if (status != DOE_VK_OK) {
        free(surface);
        return doe_vk_fail(status, "surface context retain", error);
    }
    surface->context = context;
    surface->references = 1;
    VkResult result;
    VkSurfaceKHR native_surface = 0;
    if (desc->platform == DOE_VK_SURFACE_WAYLAND) {
        const VkWaylandSurfaceCreateInfoKHR create = {.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .display = desc->display, .surface = desc->window.wayland_surface};
        result = context->vk.vkCreateWaylandSurfaceKHR(context->instance, &create, NULL, &native_surface);
    } else if (desc->platform == DOE_VK_SURFACE_XCB) {
        const VkXcbSurfaceCreateInfoKHR create = {.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
            .connection = desc->display, .window = desc->window.xcb_window};
        result = context->vk.vkCreateXcbSurfaceKHR(context->instance, &create, NULL, &native_surface);
    } else {
        const VkXlibSurfaceCreateInfoKHR create = {.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .dpy = desc->display, .window = desc->window.xlib_window};
        result = context->vk.vkCreateXlibSurfaceKHR(context->instance, &create, NULL, &native_surface);
    }
    status = doe_vk_result(context, result, "native platform surface create", error);
    if (status == DOE_VK_OK) {
        surface->surface = native_surface;
        VkBool32 supported = 0;
        status = doe_vk_result(context, context->vk.vkGetPhysicalDeviceSurfaceSupportKHR(context->physical,
            context->caps.queue_family, surface->surface, &supported), "vkGetPhysicalDeviceSurfaceSupportKHR", error);
        if (status == DOE_VK_OK && !supported) status = doe_vk_fail(DOE_VK_UNSUPPORTED, "selected queue cannot present", error);
    }
    if (status != DOE_VK_OK) {
        (void)doe_vk_surface_release(surface);
        return status;
    }
    *out = surface;
    return doe_vk_fail(DOE_VK_OK, "surface created", error);
}

doe_vk_status doe_vk_surface_capabilities(doe_vk_surface *surface,
    doe_vk_surface_caps *out, doe_vk_error *error) {
    if (surface == NULL || out == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "surface capabilities", error);
    doe_vk_context *context = surface->context;
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "surface capabilities context", error);
    VkSurfaceCapabilitiesKHR native = {0};
    doe_vk_status status = doe_vk_result(context, context->vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        context->physical, surface->surface, &native), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", error);
    if (status != DOE_VK_OK) return status;
    VkBool32 supported = 0;
    status = doe_vk_result(context, context->vk.vkGetPhysicalDeviceSurfaceSupportKHR(context->physical,
        context->caps.queue_family, surface->surface, &supported), "vkGetPhysicalDeviceSurfaceSupportKHR", error);
    if (status != DOE_VK_OK) return status;
    doe_vk_surface_caps caps = {.min_image_count = native.minImageCount, .max_image_count = native.maxImageCount,
        .current_extent = {native.currentExtent.width, native.currentExtent.height},
        .min_extent = {native.minImageExtent.width, native.minImageExtent.height},
        .max_extent = {native.maxImageExtent.width, native.maxImageExtent.height},
        .supported_usage = native.supportedUsageFlags, .supported_alpha = native.supportedCompositeAlpha,
        .current_transform = native.currentTransform, .present_supported = supported != 0};
    VkSurfaceFormatKHR formats[DOE_VK_MAX_SURFACE_FORMATS];
    caps.format_count = DOE_VK_MAX_SURFACE_FORMATS;
    VkResult result = context->vk.vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical, surface->surface,
        &caps.format_count, formats);
    if (result == 5 || caps.format_count > DOE_VK_MAX_SURFACE_FORMATS)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "surface format capacity", error);
    if (result != 0) return doe_vk_result(context, result, "vkGetPhysicalDeviceSurfaceFormatsKHR", error);
    for (uint32_t i = 0; i < caps.format_count; ++i)
        caps.formats[i] = (doe_vk_surface_format){formats[i].format, formats[i].colorSpace};
    caps.present_mode_count = DOE_VK_MAX_PRESENT_MODES;
    result = context->vk.vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical, surface->surface,
        &caps.present_mode_count, caps.present_modes);
    if (result == 5 || caps.present_mode_count > DOE_VK_MAX_PRESENT_MODES)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "surface present-mode capacity", error);
    if (result != 0) return doe_vk_result(context, result, "vkGetPhysicalDeviceSurfacePresentModesKHR", error);
    *out = caps;
    return doe_vk_fail(DOE_VK_OK, "surface capabilities ready", error);
}

static doe_vk_status validate_config(doe_vk_surface *surface, const doe_vk_surface_config *config,
    const doe_vk_surface_caps *caps, uint32_t *count, doe_vk_error *error) {
    if (config->width == 0 || config->height == 0 || !(config->usage & DOE_VK_TEXTURE_COLOR_ATTACHMENT) ||
        (config->usage & ~UINT32_C(31)) != 0 || config->alpha_mode == 0 ||
        (config->alpha_mode & (config->alpha_mode - 1)) != 0 || config->present_mode > DOE_VK_PRESENT_FIFO_RELAXED)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "surface configuration", error);
    if (!caps->present_supported || (config->usage & ~caps->supported_usage) != 0 ||
        !(config->alpha_mode & caps->supported_alpha))
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "surface usage or alpha", error);
    const uint32_t dimensions[2] = {config->width, config->height};
    for (unsigned i = 0; i < 2; ++i) {
        if (dimensions[i] < caps->min_extent[i] || dimensions[i] > caps->max_extent[i] ||
            (caps->current_extent[i] != UINT32_MAX && dimensions[i] != caps->current_extent[i]))
            return doe_vk_fail(DOE_VK_UNSUPPORTED, "surface extent", error);
    }
    if (config->format != DOE_VK_FORMAT_RGBA8_UNORM && config->format != DOE_VK_FORMAT_RGBA8_SRGB &&
        config->format != DOE_VK_FORMAT_BGRA8_UNORM && config->format != DOE_VK_FORMAT_BGRA8_SRGB)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "surface format subset", error);
    bool format = false, mode = false;
    for (uint32_t i = 0; i < caps->format_count; ++i)
        if ((caps->formats[i].format == config->format || caps->formats[i].format == 0) && caps->formats[i].color_space == 0) format = true;
    for (uint32_t i = 0; i < caps->present_mode_count; ++i)
        if (caps->present_modes[i] == (uint32_t)config->present_mode) mode = true;
    if (!format || !mode) return doe_vk_fail(DOE_VK_UNSUPPORTED, "surface format/color-space/present mode", error);
    doe_vk_format_caps support;
    doe_vk_status status = doe_vk_texture_format_caps(surface->context, config->format, &support);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "surface texture format", error);
    if (!support.color_attachment || ((config->usage & DOE_VK_TEXTURE_COPY_SRC) && !support.copy_src) ||
        ((config->usage & DOE_VK_TEXTURE_COPY_DST) && !support.copy_dst) ||
        ((config->usage & DOE_VK_TEXTURE_SAMPLED) && !support.sampled) ||
        ((config->usage & DOE_VK_TEXTURE_STORAGE) && !support.storage))
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "surface format usages", error);
    uint32_t images = config->image_count;
    if (images == 0) {
        if (caps->min_image_count == UINT32_MAX) return doe_vk_fail(DOE_VK_UNSUPPORTED, "surface minimum image count", error);
        images = caps->min_image_count + 1;
        if (caps->max_image_count != 0 && images > caps->max_image_count) images = caps->max_image_count;
    }
    if (images == 0 || images < caps->min_image_count || images > DOE_VK_MAX_SWAPCHAIN_IMAGES ||
        (caps->max_image_count != 0 && images > caps->max_image_count))
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "surface image count", error);
    *count = images;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_surface_configure(doe_vk_surface *surface,
    const doe_vk_surface_config *config, doe_vk_error *error) {
    if (surface == NULL || config == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "surface configure", error);
    if (surface->swapchain != 0 || doe_vk_surface_busy(surface))
        return doe_vk_fail(DOE_VK_BUSY, "unconfigure completed surface before replacement", error);
    doe_vk_surface_caps caps;
    doe_vk_status status = doe_vk_surface_capabilities(surface, &caps, error);
    if (status != DOE_VK_OK) return status;
    uint32_t requested = 0;
    status = validate_config(surface, config, &caps, &requested, error);
    if (status != DOE_VK_OK) return status;
    const VkSwapchainCreateInfoKHR create = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface->surface, .minImageCount = requested, .imageFormat = config->format,
        .imageExtent = {config->width, config->height}, .imageArrayLayers = 1, .imageUsage = config->usage,
        .preTransform = caps.current_transform, .compositeAlpha = config->alpha_mode,
        .presentMode = (uint32_t)config->present_mode, .clipped = 1};
    doe_vk_context *context = surface->context;
    VkSwapchainKHR swapchain = 0;
    status = doe_vk_result(context, context->vk.vkCreateSwapchainKHR(context->device, &create, NULL, &swapchain), "vkCreateSwapchainKHR", error);
    if (status != DOE_VK_OK) return status;
    uint32_t count = DOE_VK_MAX_SWAPCHAIN_IMAGES;
    VkImage images[DOE_VK_MAX_SWAPCHAIN_IMAGES];
    VkResult result = context->vk.vkGetSwapchainImagesKHR(context->device, swapchain, &count, images);
    if (result == 5 || count > DOE_VK_MAX_SWAPCHAIN_IMAGES || count == 0)
        status = doe_vk_fail(DOE_VK_UNSUPPORTED, "swapchain image capacity", error);
    else status = doe_vk_result(context, result, "vkGetSwapchainImagesKHR", error);
    if (status != DOE_VK_OK) {
        context->vk.vkDestroySwapchainKHR(context->device, swapchain, NULL);
        return status;
    }
    surface->swapchain = swapchain;
    surface->image_count = count;
    memcpy(surface->images, images, count * sizeof(*images));
    surface->config = *config;
    return doe_vk_fail(DOE_VK_OK, "surface configured", error);
}
