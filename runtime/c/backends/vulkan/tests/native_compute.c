/* SPDX-License-Identifier: MIT */
#include "../include/doe_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(doe_vk_status status, const char *operation) {
    if (status == DOE_VK_OK) return;
    fprintf(stderr, "%s: %s\n", operation, doe_vk_status_name(status));
    exit(1);
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) return 2;
    long length = ftell(file);
    if (length < 20 || length % 4 != 0 || length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) return 2;
    uint32_t *spirv = malloc((size_t)length);
    if (spirv == NULL || fread(spirv, 1, (size_t)length, file) != (size_t)length) return 2;
    fclose(file);
    doe_vk_options options = { .allow_cpu_adapter = argc == 3 && strcmp(argv[2], "--allow-software") == 0 };
    doe_vk_context *context = NULL;
    doe_vk_error error;
    doe_vk_status status = doe_vk_context_create(&options, &context, &error);
    if (status != DOE_VK_OK) {
        printf("SKIP native execution: %s at %s, native=%d\n", doe_vk_status_name(status), error.operation, error.native_result);
        free(spirv);
        return status == DOE_VK_UNSUPPORTED || status == DOE_VK_LOADER_UNAVAILABLE ? 77 : 1;
    }
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "capabilities");
    printf("Native Vulkan test adapter: %s; vendor=%u device=%u driver=%u api=%u ordinal=%u software=%u; not physical qualification\n",
        caps.device_name, caps.vendor_id, caps.device_id, caps.driver_version, caps.api_version, caps.adapter_ordinal, caps.software_adapter);
    doe_vk_buffer *storage = NULL;
    doe_vk_buffer *readback = NULL;
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_COPY_SRC,
        DOE_VK_MEMORY_HOST, &storage, &error), "storage create");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_COPY_DST,
        DOE_VK_MEMORY_HOST, &readback, &error), "readback create");
    uint32_t initial[64];
    for (uint32_t i = 0; i < 64; ++i) initial[i] = i;
    require(doe_vk_buffer_write(storage, 0, initial, sizeof(initial)), "host write");
    doe_vk_pipeline *pipeline = NULL;
    doe_vk_binding_layout layout = { 0, 0, DOE_VK_BINDING_STORAGE };
    require(doe_vk_pipeline_create(context, spirv, (size_t)length / 4, "main", &layout, 1,
        &pipeline, &error), "compute pipeline");
    free(spirv);
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "commands create");
    doe_vk_buffer_binding binding = { storage, 0, 256 };
    require(doe_vk_commands_dispatch(commands, pipeline, &binding, 1, 64, 1, 1), "first dispatch");
    require(doe_vk_commands_dispatch(commands, pipeline, &binding, 1, 64, 1, 1), "dependent dispatch");
    require(doe_vk_commands_fill(commands, readback, 0, 256, UINT32_C(0xdeadbeef)), "readback fill");
    require(doe_vk_commands_copy(commands, storage, 0, readback, 0, 256), "readback copy");
    doe_vk_pipeline_release(pipeline);
    doe_vk_buffer_release(storage);
    require(doe_vk_commands_submit(commands, &error), "submit");
    if (doe_vk_commands_release(commands) != DOE_VK_BUSY) return 1;
    uint32_t output[64] = {0};
    if (doe_vk_buffer_read(readback, 0, output, sizeof(output)) != DOE_VK_BUSY) return 1;
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), &error), "wait");
    require(doe_vk_buffer_read(readback, 0, output, sizeof(output)), "readback");
    require(doe_vk_commands_release(commands), "commands release");
    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t expected = (i * 3u + 7u) * 3u + 7u;
        if (output[i] != expected) {
            fprintf(stderr, "oracle mismatch at %u: got %u expected %u\n", i, output[i], expected);
            return 1;
        }
    }
    doe_vk_buffer_release(readback);
    doe_vk_context_release(context);
    puts("PASS real Vulkan compute, dependent visibility, fill/copy, readback, early caller release");
    return 0;
}
