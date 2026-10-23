/* SPDX-License-Identifier: MIT */
#include "../include/doe_vk_surface.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "--allow-software") != 0) return 2;
    const uint32_t platforms[] = {0, DOE_VK_SURFACE_WAYLAND, DOE_VK_SURFACE_XCB, DOE_VK_SURFACE_XLIB};
    const char *names[] = {"offscreen baseline", "Wayland", "XCB", "Xlib"};
    for (unsigned i = 0; i < 4; ++i) {
        const doe_vk_options options = {.allow_cpu_adapter = true, .require_graphics_queue = true,
            .enable_shader_demote_if_supported = true, .surface_platforms = platforms[i]};
        doe_vk_context *context = NULL;
        doe_vk_error error;
        doe_vk_status status = doe_vk_context_create(&options, &context, &error);
        if (status == DOE_VK_OK) {
            doe_vk_caps caps;
            if (doe_vk_context_caps(context, &caps) != DOE_VK_OK) return 1;
            printf("%s enabled mask=%u adapter=%s software=%u; discovery only, no presentation executed\n",
                names[i], caps.surface_platforms, caps.device_name, caps.software_adapter);
            doe_vk_context_release(context);
        } else {
            printf("%s unavailable: %s at %s native=%d; no substitute surface\n", names[i],
                doe_vk_status_name(status), error.operation, error.native_result);
            if (i == 0 || (status != DOE_VK_UNSUPPORTED && status != DOE_VK_LOADER_UNAVAILABLE)) return 1;
        }
    }
    return 0;
}
