/* SPDX-License-Identifier: MIT */
#include "../include/doe_vk_bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Retained Tint particles import_level fixture: group0 binding3 uniform width,
 * binding4 readonly storage, binding5 float output, binding6 sampled texture.
 * Local size64; each invocation writes the fetched texture alpha to output. */
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
        printf("SKIP image descriptors: %s at %s native=%d\n", doe_vk_status_name(status), error.operation, error.native_result);
        free(spirv);
        return status == DOE_VK_UNSUPPORTED || status == DOE_VK_LOADER_UNAVAILABLE ? 77 : 1;
    }
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    printf("Image descriptor adapter: %s software=%u; retained Tint fixture, not C texture compiler or physical qualification\n", caps.device_name, caps.software_adapter);
    doe_vk_buffer *uniform = NULL;
    doe_vk_buffer *unused = NULL;
    doe_vk_buffer *output = NULL;
    doe_vk_buffer *upload = NULL;
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_UNIFORM, DOE_VK_MEMORY_HOST, &uniform, &error), "uniform");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_STORAGE, DOE_VK_MEMORY_HOST, &unused, &error), "readonly");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_STORAGE, DOE_VK_MEMORY_HOST, &output, &error), "output");
    require(doe_vk_buffer_create(context, 1024, DOE_VK_BUFFER_COPY_SRC, DOE_VK_MEMORY_HOST, &upload, &error), "upload");
    uint32_t width = 64;
    require(doe_vk_buffer_write(uniform, 0, &width, sizeof(width)), "width");
    float pixels[64][4];
    for (unsigned i = 0; i < 64; ++i) {
        pixels[i][0] = 1;
        pixels[i][1] = 2;
        pixels[i][2] = 3;
        pixels[i][3] = (float)i * 0.25f;
    }
    require(doe_vk_buffer_write(upload, 0, pixels, sizeof(pixels)), "pixels");
    const doe_vk_texture_desc texture_desc = {
        .dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_RGBA32_FLOAT,
        .width = 64, .height = 1, .depth_or_layers = 1, .mip_levels = 1,
        .usage = DOE_VK_TEXTURE_COPY_DST | DOE_VK_TEXTURE_SAMPLED
    };
    doe_vk_texture *texture = NULL;
    doe_vk_texture_view *view = NULL;
    require(doe_vk_texture_create(context, &texture_desc, &texture, &error), "sampled image");
    const doe_vk_texture_view_desc view_desc = { DOE_VK_VIEW_2D, 0, 1, 0, 1, DOE_VK_ASPECT_ALL, 0};
    require(doe_vk_texture_view_create(texture, &view_desc, &view, &error), "sampled view");
    /* Extra unused sampler binding exercises descriptor retention, not sampling semantics. */
    doe_vk_sampler *sampler = NULL;
    const doe_vk_sampler_desc sampler_desc = { .max_lod = 1 };
    require(doe_vk_sampler_create(context, &sampler_desc, &sampler, &error), "sampler");
    const doe_vk_binding_layout layout[5] = {
        {0, 3, DOE_VK_BINDING_UNIFORM}, {0, 4, DOE_VK_BINDING_STORAGE},
        {0, 5, DOE_VK_BINDING_STORAGE}, {0, 6, DOE_VK_BINDING_SAMPLED_TEXTURE},
        {0, 8, DOE_VK_BINDING_SAMPLER}
    };
    doe_vk_pipeline *pipeline = NULL;
    require(doe_vk_pipeline_create(context, spirv, (size_t)length / 4, "import_level", layout, 5, &pipeline, &error), "image compute pipeline");
    free(spirv);
    const doe_vk_binding_resource bindings[5] = {
        { .kind = DOE_VK_BINDING_UNIFORM, .value.buffer = { uniform, 0, 16 } },
        { .kind = DOE_VK_BINDING_STORAGE, .value.buffer = { unused, 0, 256 } },
        { .kind = DOE_VK_BINDING_STORAGE, .value.buffer = { output, 0, 256 } },
        { .kind = DOE_VK_BINDING_SAMPLED_TEXTURE, .value.view = view },
        { .kind = DOE_VK_BINDING_SAMPLER, .value.sampler = sampler }
    };
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "image commands");
    const doe_vk_texture_region region = { .extent = {64, 1, 1} };
    const doe_vk_texture_buffer_layout packed = {0};
    require(doe_vk_commands_upload_texture(commands, upload, packed, texture, region), "image upload");
    require(doe_vk_commands_dispatch_resources(commands, pipeline, bindings, 5, 1, 1, 1), "sampled texture dispatch");
    doe_vk_buffer_release(uniform);
    doe_vk_buffer_release(unused);
    doe_vk_buffer_release(upload);
    doe_vk_sampler_release(sampler);
    doe_vk_texture_view_release(view);
    doe_vk_texture_release(texture);
    doe_vk_pipeline_release(pipeline);
    require(doe_vk_commands_submit(commands, &error), "submit");
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), &error), "wait");
    float values[64];
    require(doe_vk_buffer_read(output, 0, values, sizeof(values)), "sampled image result");
    for (unsigned i = 0; i < 64; ++i) {
        if (values[i] != pixels[i][3]) {
            fprintf(stderr, "sampled alpha mismatch at %u: %g != %g\n", i, values[i], pixels[i][3]);
            return 1;
        }
    }
    require(doe_vk_commands_release(commands), "retire");
    doe_vk_buffer_release(output);
    doe_vk_context_release(context);
    puts("PASS real sparse mixed uniform/storage/sampled-image descriptors, transfer-to-shader visibility, exact64-float alpha oracle and early view/image release");
    return 0;
}
