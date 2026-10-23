/* SPDX-License-Identifier: MIT */
#include "../include/doe_vk_bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Existing Tint particles export_level fixture: four 2x2 weights are reduced
 * to one float mean and one normalized cumulative RGBA8 storage-image texel.
 * width=128; each block has weights (1,0,0,0), so mean=.25 and RGBA=(1,1,1,1). */
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
        printf("SKIP storage image: %s at %s native=%d\n", doe_vk_status_name(status), error.operation, error.native_result);
        free(spirv);
        return status == DOE_VK_UNSUPPORTED || status == DOE_VK_LOADER_UNAVAILABLE ? 77 : 1;
    }
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    printf("Storage image adapter: %s software=%u; retained Tint fixture, not C texture compiler or physical qualification\n", caps.device_name, caps.software_adapter);
    doe_vk_buffer *uniform = NULL;
    doe_vk_buffer *input = NULL;
    doe_vk_buffer *output = NULL;
    doe_vk_buffer *staging = NULL;
    doe_vk_buffer *readback = NULL;
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_UNIFORM, DOE_VK_MEMORY_HOST, &uniform, &error), "uniform");
    require(doe_vk_buffer_create(context, 1024, DOE_VK_BUFFER_STORAGE, DOE_VK_MEMORY_HOST, &input, &error), "input");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_STORAGE, DOE_VK_MEMORY_HOST, &output, &error), "output");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_COPY_SRC, DOE_VK_MEMORY_HOST, &staging, &error), "zero upload");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &readback, &error), "readback");
    uint32_t width = 128;
    require(doe_vk_buffer_write(uniform, 0, &width, sizeof(width)), "width");
    float weights[256] = {0};
    for (unsigned i = 0; i < 64; ++i) weights[i * 2] = 1.0f;
    require(doe_vk_buffer_write(input, 0, weights, sizeof(weights)), "weights");
    const doe_vk_texture_desc texture_desc = {
        .dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_RGBA8_UNORM,
        .width = 64, .height = 1, .depth_or_layers = 1, .mip_levels = 1,
        .usage = DOE_VK_TEXTURE_COPY_DST | DOE_VK_TEXTURE_COPY_SRC | DOE_VK_TEXTURE_STORAGE
    };
    doe_vk_texture *texture = NULL;
    doe_vk_texture_view *view = NULL;
    require(doe_vk_texture_create(context, &texture_desc, &texture, &error), "storage image");
    const doe_vk_texture_view_desc view_desc = { DOE_VK_VIEW_2D, 0, 1, 0, 1, DOE_VK_ASPECT_ALL};
    require(doe_vk_texture_view_create(texture, &view_desc, &view, &error), "storage view");
    const doe_vk_binding_layout layout[4] = {
        {0, 3, DOE_VK_BINDING_UNIFORM}, {0, 4, DOE_VK_BINDING_STORAGE},
        {0, 5, DOE_VK_BINDING_STORAGE}, {0, 7, DOE_VK_BINDING_STORAGE_TEXTURE}
    };
    doe_vk_pipeline *pipeline = NULL;
    require(doe_vk_pipeline_create(context, spirv, (size_t)length / 4, "export_level", layout, 4, &pipeline, &error), "storage pipeline");
    free(spirv);
    const doe_vk_binding_resource bindings[4] = {
        { .kind = DOE_VK_BINDING_UNIFORM, .value.buffer = { uniform, 0, 16 } },
        { .kind = DOE_VK_BINDING_STORAGE, .value.buffer = { input, 0, 1024 } },
        { .kind = DOE_VK_BINDING_STORAGE, .value.buffer = { output, 0, 256 } },
        { .kind = DOE_VK_BINDING_STORAGE_TEXTURE, .value.view = view }
    };
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "commands");
    const doe_vk_texture_region region = { .extent = {64, 1, 1} };
    const doe_vk_texture_buffer_layout packed = {0};
    require(doe_vk_commands_upload_texture(commands, staging, packed, texture, region), "initialize storage image");
    require(doe_vk_commands_dispatch_resources(commands, pipeline, bindings, 4, 1, 1, 1), "storage dispatch");
    require(doe_vk_commands_read_texture(commands, texture, region, readback, packed), "record shader image readback");
    doe_vk_buffer_release(uniform);
    doe_vk_buffer_release(input);
    doe_vk_buffer_release(staging);
    doe_vk_texture_view_release(view);
    doe_vk_texture_release(texture);
    doe_vk_pipeline_release(pipeline);
    require(doe_vk_commands_submit(commands, &error), "submit");
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), &error), "wait");
    float means[64];
    uint8_t pixels[256];
    require(doe_vk_buffer_read(output, 0, means, sizeof(means)), "means");
    require(doe_vk_buffer_read(readback, 0, pixels, sizeof(pixels)), "shader pixels");
    for (unsigned i = 0; i < 64; ++i) {
        if (means[i] != 0.25f) {
            fprintf(stderr, "mean mismatch at %u: %g != .25\n", i, means[i]);
            return 1;
        }
    }
    for (unsigned i = 0; i < sizeof(pixels); ++i) {
        if (pixels[i] != 255) {
            fprintf(stderr, "storage pixel mismatch at %u: %u != 255\n", i, pixels[i]);
            return 1;
        }
    }
    require(doe_vk_commands_release(commands), "retire");
    doe_vk_buffer_release(output);
    doe_vk_buffer_release(readback);
    doe_vk_context_release(context);
    puts("PASS real storage-image shader, upload/shader/readback visibility, exact64 means/256 texel bytes and early view/image release");
    return 0;
}
