#include "../include/doe_vk.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    doe_vk_context *context = NULL;
    doe_vk_options options = { .allow_cpu_adapter = argc == 2 && strcmp(argv[1], "--allow-software") == 0 };
    doe_vk_error error;
    doe_vk_status status = doe_vk_context_create(&options, &context, &error);
    if (status != DOE_VK_OK) {
        printf("Vulkan unavailable: %s at %s (native=%d); no execution claim\n",
            doe_vk_status_name(status), error.operation, error.native_result);
        return status == DOE_VK_UNSUPPORTED || status == DOE_VK_LOADER_UNAVAILABLE ? 77 : 1;
    }
    doe_vk_caps caps;
    status = doe_vk_context_caps(context, &caps);
    if (status != DOE_VK_OK) return 1;
    printf("Vulkan discovered: %s vendor=%u device=%u queue=%u software=%u; discovery is not qualification\n",
        caps.device_name, caps.vendor_id, caps.device_id, caps.queue_family, caps.software_adapter);
    doe_vk_context_release(context);
    return 0;
}
