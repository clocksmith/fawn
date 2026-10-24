/* SPDX-License-Identifier: MIT */
#include "../include/doe_vk_texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(doe_vk_status status, const char *operation) {
    if (status == DOE_VK_OK) return;
    fprintf(stderr, "%s: %s\n", operation, doe_vk_status_name(status));
    exit(1);
}

int main(int argc, char **argv) {
    doe_vk_options options = { .allow_cpu_adapter = argc == 2 && strcmp(argv[1], "--allow-software") == 0 };
    doe_vk_context *context = NULL;
    doe_vk_error error;
    doe_vk_status status = doe_vk_context_create(&options, &context, &error);
    if (status != DOE_VK_OK) {
        printf("SKIP texture execution: %s at %s native=%d\n", doe_vk_status_name(status), error.operation, error.native_result);
        return status == DOE_VK_UNSUPPORTED || status == DOE_VK_LOADER_UNAVAILABLE ? 77 : 1;
    }
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "capabilities");
    printf("Texture native adapter: %s software=%u; no physical qualification\n", caps.device_name, caps.software_adapter);
    const doe_vk_texture_desc desc = {
        .dimension = DOE_VK_TEXTURE_2D,
        .format = DOE_VK_FORMAT_RGBA8_UNORM,
        .width = 4, .height = 4, .depth_or_layers = 2, .mip_levels = 2,
        .usage = DOE_VK_TEXTURE_COPY_SRC | DOE_VK_TEXTURE_COPY_DST | DOE_VK_TEXTURE_SAMPLED
    };
    doe_vk_texture *source = NULL;
    doe_vk_texture *destination = NULL;
    require(doe_vk_texture_create(context, &desc, &source, &error), "source image");
    require(doe_vk_texture_create(context, &desc, &destination, &error), "destination image");
    const doe_vk_texture_view_desc view_desc = { DOE_VK_VIEW_2D_ARRAY, 0, 2, 0, 2, DOE_VK_ASPECT_ALL, 0, {0}};
    doe_vk_texture_view *view = NULL;
    require(doe_vk_texture_view_create(source, &view_desc, &view, &error), "view");
    const doe_vk_sampler_desc sampler_desc = {
        .address_u = DOE_VK_ADDRESS_CLAMP, .address_v = DOE_VK_ADDRESS_CLAMP,
        .address_w = DOE_VK_ADDRESS_CLAMP, .max_lod = 1
    };
    doe_vk_sampler *sampler = NULL;
    require(doe_vk_sampler_create(context, &sampler_desc, &sampler, &error), "sampler");
    doe_vk_sampler_release(sampler);
    doe_vk_buffer *upload = NULL;
    doe_vk_buffer *readback = NULL;
    require(doe_vk_buffer_create(context, 512, DOE_VK_BUFFER_COPY_SRC, DOE_VK_MEMORY_HOST, &upload, &error), "upload buffer");
    require(doe_vk_buffer_create(context, 512, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &readback, &error), "readback buffer");
    unsigned char input[512] = {0};
    for (unsigned layer = 0; layer < 2; ++layer) {
        for (unsigned y = 0; y < 4; ++y) {
            for (unsigned x = 0; x < 16; ++x) input[layer * 192 + y * 32 + x] = (unsigned char)(layer * 64 + y * 16 + x);
        }
        for (unsigned y = 0; y < 2; ++y) {
            for (unsigned x = 0; x < 8; ++x) input[320 + layer * 48 + y * 16 + x] = (unsigned char)(200 + layer * 16 + y * 8 + x);
        }
    }
    require(doe_vk_buffer_write(upload, 0, input, sizeof(input)), "upload bytes");
    doe_vk_texture_region mip0 = { .extent = {4, 4, 2} };
    doe_vk_texture_region mip1 = { .mip = 1, .extent = {2, 2, 2} };
    doe_vk_texture_buffer_layout layout0 = { 0, 32, 6 };
    doe_vk_texture_buffer_layout layout1 = { 320, 16, 3 };
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "uninitialized check recording");
    if (doe_vk_commands_read_texture(commands, source, mip0, readback, layout0) != DOE_VK_INVALID_STATE) return 1;
    require(doe_vk_commands_release(commands), "discard uninitialized check");
    require(doe_vk_commands_create(context, &commands, &error), "recording");
    require(doe_vk_commands_upload_texture(commands, upload, layout0, source, mip0), "upload array mip0");
    require(doe_vk_commands_upload_texture(commands, upload, layout1, source, mip1), "upload array mip1");
    require(doe_vk_commands_copy_texture(commands, source, mip0, destination, mip0), "image copy");
    require(doe_vk_commands_read_texture(commands, destination, mip0, readback, layout0), "read array mip0");
    require(doe_vk_commands_read_texture(commands, source, mip1, readback, layout1), "read array mip1");
    doe_vk_commands *competing = NULL;
    require(doe_vk_commands_create(context, &competing, &error), "competing recording");
    if (doe_vk_commands_upload_texture(competing, upload, layout0, source, mip0) != DOE_VK_BUSY) return 1;
    require(doe_vk_commands_release(competing), "discard competing recording");
    doe_vk_texture_view_release(view);
    doe_vk_texture_release(source);
    doe_vk_texture_release(destination);
    doe_vk_buffer_release(upload);
    require(doe_vk_commands_submit(commands, &error), "submit texture work");
    if (doe_vk_commands_release(commands) != DOE_VK_BUSY) return 1;
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), &error), "wait texture work");
    unsigned char output[512];
    require(doe_vk_buffer_read(readback, 0, output, sizeof(output)), "texture readback");
    require(doe_vk_commands_release(commands), "retire texture work");
    for (unsigned i = 0; i < sizeof(input); ++i) {
        if (input[i] != output[i]) {
            fprintf(stderr, "pixel/padding mismatch byte %u: %u != %u\n", i, output[i], input[i]);
            return 1;
        }
    }
    doe_vk_buffer_release(readback);
    doe_vk_context_release(context);
    puts("PASS real Vulkan image/view/sampler creation, two-mip two-layer padded upload/copy/readback, initialization rejection and retained lifetime");
    return 0;
}
