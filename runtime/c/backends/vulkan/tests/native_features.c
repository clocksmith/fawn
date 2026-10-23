/* SPDX-License-Identifier: MIT */
#include "../include/doe_vk.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "--allow-software") != 0) {
        fputs("usage: native_features --allow-software (explicit ICD selected externally)\n", stderr);
        return 2;
    }
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
        doe_vk_options options = {.allow_cpu_adapter = true, .require_graphics_queue = true,
            .enable_shader_demote_if_supported = enabled != 0};
        doe_vk_context *context = NULL;
        doe_vk_error error = {0};
        doe_vk_status status = doe_vk_context_create(&options, &context, &error);
        if (status != DOE_VK_OK) {
            fprintf(stderr, "device creation: %s at %s (%d)\n",
                doe_vk_status_name(status), error.operation, error.native_result);
            return 1;
        }
        doe_vk_caps caps;
        status = doe_vk_context_caps(context, &caps);
        if (status != DOE_VK_OK || !caps.software_adapter ||
            caps.shader_demote_to_helper_invocation != (enabled != 0)) {
            doe_vk_context_release(context);
            fputs("explicit software demote device selection mismatch\n", stderr);
            return 1;
        }
        printf("PASS software device %s, requested=%u enabled=%u; device creation only, not shader or physical qualification\n",
            caps.device_name, enabled, caps.shader_demote_to_helper_invocation);
        doe_vk_context_release(context);
    }
    return 0;
}
