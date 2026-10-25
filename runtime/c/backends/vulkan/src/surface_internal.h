/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_SURFACE_INTERNAL_H
#define DOE_VK_SURFACE_INTERNAL_H
#include "texture_internal.h"
#include "../include/doe_vk_surface.h"
enum { DOE_VK_MAX_SWAPCHAIN_IMAGES = 8, DOE_VK_MAX_INSTANCE_EXTENSIONS = 6 };
typedef struct doe_vk_instance_extensions {
    const uint8_t *names[DOE_VK_MAX_INSTANCE_EXTENSIONS];
    uint32_t count;
} doe_vk_instance_extensions;
typedef struct doe_vk_surface_source {
    VkSurfaceKHR handle;
    uint32_t references; /* Swapchain generations sharing this platform surface. */
} doe_vk_surface_source;
struct doe_vk_surface {
    doe_vk_context *context;
    uint32_t references;
    doe_vk_surface_source *source;
    VkSwapchainKHR swapchain;
    VkImage images[DOE_VK_MAX_SWAPCHAIN_IMAGES];
    uint32_t image_count, image_index;
    doe_vk_surface_config config;
    doe_vk_texture *texture;
    doe_vk_texture *retired_frames;
    doe_vk_commands *commands;
    VkFence acquire_fence, present_fence;
    VkSemaphore render_finished;
    bool acquire_pending, acquired, present_pending, acquire_suboptimal;
    bool uncertain_acquire;
    /* OK, or the surface loss/out-of-date status with which the swapchain
     * refused to release an image: only reconfiguration or teardown remain. */
    doe_vk_status swapchain_status;
    doe_vk_status present_status;
    VkResult present_native_result;
};
doe_vk_status doe_vk_extensions_present(doe_vk_context *context, bool device,
    const uint8_t *const *names, size_t count, bool *present, doe_vk_error *error);
doe_vk_status doe_vk_surface_instance_extensions(doe_vk_context *context,
    uint32_t platforms, doe_vk_instance_extensions *out, doe_vk_error *error);
doe_vk_status doe_vk_surface_load_functions(doe_vk_context *context,
    uint32_t platforms, bool device, doe_vk_error *error);
doe_vk_status doe_vk_surface_submit_prepare(doe_vk_commands *commands, doe_vk_error *error);
doe_vk_status doe_vk_surface_commands_release(doe_vk_commands *commands);
bool doe_vk_surface_busy(const doe_vk_surface *surface);
void doe_vk_surface_sync_destroy(doe_vk_surface *surface);
doe_vk_status doe_vk_surface_fence(doe_vk_surface *surface, VkFence *out, doe_vk_error *error);
void doe_vk_surface_collect_frames(doe_vk_surface *surface);
#endif
