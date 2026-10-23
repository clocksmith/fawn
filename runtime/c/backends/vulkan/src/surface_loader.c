/* SPDX-License-Identifier: MIT */
#include "surface_internal.h"

doe_vk_status doe_vk_surface_instance_extensions(doe_vk_context *context,
    uint32_t platforms, doe_vk_instance_extensions *out, doe_vk_error *error) {
    if (context == NULL || out == NULL || (platforms & ~UINT32_C(7)) != 0)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "surface platform selection", error);
    doe_vk_instance_extensions selection = {0};
    if (platforms == 0) {
        *out = selection;
        return DOE_VK_OK;
    }
    doe_vk_status status = doe_vk_load_symbol(context->library, "vkEnumerateInstanceExtensionProperties",
        &context->vk.vkEnumerateInstanceExtensionProperties, sizeof(context->vk.vkEnumerateInstanceExtensionProperties), error);
    if (status == DOE_VK_OK) status = doe_vk_load_symbol(context->library, "vkGetInstanceProcAddr",
        &context->vk.vkGetInstanceProcAddr, sizeof(context->vk.vkGetInstanceProcAddr), error);
    if (status == DOE_VK_OK) status = doe_vk_load_symbol(context->library, "vkGetDeviceProcAddr",
        &context->vk.vkGetDeviceProcAddr, sizeof(context->vk.vkGetDeviceProcAddr), error);
    if (status != DOE_VK_OK) return status;
    selection.names[selection.count++] = (const uint8_t *)"VK_KHR_surface";
    selection.names[selection.count++] = (const uint8_t *)"VK_KHR_get_surface_capabilities2";
    selection.names[selection.count++] = (const uint8_t *)"VK_EXT_surface_maintenance1";
    if (platforms & DOE_VK_SURFACE_WAYLAND) selection.names[selection.count++] = (const uint8_t *)"VK_KHR_wayland_surface";
    if (platforms & DOE_VK_SURFACE_XCB) selection.names[selection.count++] = (const uint8_t *)"VK_KHR_xcb_surface";
    if (platforms & DOE_VK_SURFACE_XLIB) selection.names[selection.count++] = (const uint8_t *)"VK_KHR_xlib_surface";
    bool present[DOE_VK_MAX_INSTANCE_EXTENSIONS];
    status = doe_vk_extensions_present(context, false, selection.names, selection.count, present, error);
    if (status != DOE_VK_OK) return status;
    for (uint32_t i = 0; i < selection.count; ++i) {
        if (!present[i]) return doe_vk_fail(DOE_VK_UNSUPPORTED, (const char *)selection.names[i], error);
    }
    *out = selection;
    return DOE_VK_OK;
}

static doe_vk_status load_proc(doe_vk_context *context, bool device, const char *name,
    void *destination, size_t size, doe_vk_error *error) {
    if (size != sizeof(doe_vk_proc)) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "WSI function pointer size", error);
    doe_vk_proc proc = device ? context->vk.vkGetDeviceProcAddr(context->device, name) :
        context->vk.vkGetInstanceProcAddr(context->instance, name);
    if (proc == NULL) return doe_vk_fail(DOE_VK_UNSUPPORTED, name, error);
    memcpy(destination, &proc, size);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_surface_load_functions(doe_vk_context *context,
    uint32_t platforms, bool device, doe_vk_error *error) {
    if (platforms == 0) return DOE_VK_OK;
    struct surface_symbol { const char *name; void *destination; size_t size; uint32_t platform; };
    const struct surface_symbol instance_symbols[] = {
        {"vkDestroySurfaceKHR", &context->vk.vkDestroySurfaceKHR, sizeof(context->vk.vkDestroySurfaceKHR), 7},
        {"vkGetPhysicalDeviceSurfaceSupportKHR", &context->vk.vkGetPhysicalDeviceSurfaceSupportKHR, sizeof(context->vk.vkGetPhysicalDeviceSurfaceSupportKHR), 7},
        {"vkGetPhysicalDeviceSurfaceCapabilitiesKHR", &context->vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR, sizeof(context->vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR), 7},
        {"vkGetPhysicalDeviceSurfaceFormatsKHR", &context->vk.vkGetPhysicalDeviceSurfaceFormatsKHR, sizeof(context->vk.vkGetPhysicalDeviceSurfaceFormatsKHR), 7},
        {"vkGetPhysicalDeviceSurfacePresentModesKHR", &context->vk.vkGetPhysicalDeviceSurfacePresentModesKHR, sizeof(context->vk.vkGetPhysicalDeviceSurfacePresentModesKHR), 7},
        {"vkCreateWaylandSurfaceKHR", &context->vk.vkCreateWaylandSurfaceKHR, sizeof(context->vk.vkCreateWaylandSurfaceKHR), DOE_VK_SURFACE_WAYLAND},
        {"vkCreateXcbSurfaceKHR", &context->vk.vkCreateXcbSurfaceKHR, sizeof(context->vk.vkCreateXcbSurfaceKHR), DOE_VK_SURFACE_XCB},
        {"vkCreateXlibSurfaceKHR", &context->vk.vkCreateXlibSurfaceKHR, sizeof(context->vk.vkCreateXlibSurfaceKHR), DOE_VK_SURFACE_XLIB}
    };
    const struct surface_symbol device_symbols[] = {
        {"vkCreateSwapchainKHR", &context->vk.vkCreateSwapchainKHR, sizeof(context->vk.vkCreateSwapchainKHR), 7},
        {"vkDestroySwapchainKHR", &context->vk.vkDestroySwapchainKHR, sizeof(context->vk.vkDestroySwapchainKHR), 7},
        {"vkGetSwapchainImagesKHR", &context->vk.vkGetSwapchainImagesKHR, sizeof(context->vk.vkGetSwapchainImagesKHR), 7},
        {"vkAcquireNextImageKHR", &context->vk.vkAcquireNextImageKHR, sizeof(context->vk.vkAcquireNextImageKHR), 7},
        {"vkQueuePresentKHR", &context->vk.vkQueuePresentKHR, sizeof(context->vk.vkQueuePresentKHR), 7},
        {"vkCreateSemaphore", &context->vk.vkCreateSemaphore, sizeof(context->vk.vkCreateSemaphore), 7},
        {"vkDestroySemaphore", &context->vk.vkDestroySemaphore, sizeof(context->vk.vkDestroySemaphore), 7},
        {"vkReleaseSwapchainImagesEXT", &context->vk.vkReleaseSwapchainImagesEXT, sizeof(context->vk.vkReleaseSwapchainImagesEXT), 7}
    };
    const struct surface_symbol *symbols = device ? device_symbols : instance_symbols;
    size_t count = device ? sizeof(device_symbols) / sizeof(device_symbols[0]) : sizeof(instance_symbols) / sizeof(instance_symbols[0]);
    for (size_t i = 0; i < count; ++i) {
        if (!(symbols[i].platform & platforms)) continue;
        doe_vk_status status = load_proc(context, device, symbols[i].name, symbols[i].destination, symbols[i].size, error);
        if (status != DOE_VK_OK) return status;
    }
    return DOE_VK_OK;
}
