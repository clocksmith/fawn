#include "../include/doe_vk_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(doe_vk_status status, const char *operation) {
    if (status == DOE_VK_OK) return;
    fprintf(stderr, "%s: %s\n", operation, doe_vk_status_name(status));
    exit(1);
}

static doe_vk_shader load_shader(const char *path, const char *entry) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) exit(2);
    long size = ftell(file);
    if (size < 20 || size % 4 != 0 || size > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) exit(2);
    uint32_t *words = malloc((size_t)size);
    if (words == NULL || fread(words, 1, (size_t)size, file) != (size_t)size) exit(2);
    fclose(file);
    return (doe_vk_shader){words, (size_t)size / 4, entry};
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) return 2;
    doe_vk_shader vertex = load_shader(argv[1], "vs_main");
    doe_vk_shader fragment = load_shader(argv[2], "fs_main");
    doe_vk_options options = { .require_graphics_queue = true,
        .allow_cpu_adapter = argc == 4 && strcmp(argv[3], "--allow-software") == 0 };
    doe_vk_context *context = NULL;
    doe_vk_error error;
    doe_vk_status status = doe_vk_context_create(&options, &context, &error);
    if (status != DOE_VK_OK) {
        printf("SKIP offscreen render: %s at %s native=%d\n", doe_vk_status_name(status), error.operation, error.native_result);
        free((void *)vertex.words);
        free((void *)fragment.words);
        return status == DOE_VK_UNSUPPORTED || status == DOE_VK_LOADER_UNAVAILABLE ? 77 : 1;
    }
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    printf("Render adapter: %s software=%u; retained Tint graphics, not C graphics compiler or hardware qualification\n", caps.device_name, caps.software_adapter);
    doe_vk_buffer *uniform = NULL, *vertices = NULL, *readback = NULL;
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_UNIFORM, DOE_VK_MEMORY_HOST, &uniform, &error), "uniform");
    require(doe_vk_buffer_create(context, 108, DOE_VK_BUFFER_VERTEX, DOE_VK_MEMORY_HOST, &vertices, &error), "vertices");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &readback, &error), "readback");
    /* Identity MVP, zero camera-right/up, zero quad coordinates: the retained
     * particle vertex shader directly uses position; fragment emits input color. */
    float parameters[24] = {0};
    parameters[0] = parameters[5] = parameters[10] = parameters[15] = 1;
    const float triangle[3][9] = {
        {-1, -1, 0, 1, 0, 1, 1, 0, 0},
        {3, -1, 0, 1, 0, 1, 1, 0, 0},
        {-1, 3, 0, 1, 0, 1, 1, 0, 0}
    };
    require(doe_vk_buffer_write(uniform, 0, parameters, sizeof(parameters)), "parameters");
    require(doe_vk_buffer_write(vertices, 0, triangle, sizeof(triangle)), "triangle");
    const doe_vk_texture_desc texture_desc = { .dimension = DOE_VK_TEXTURE_2D,
        .format = DOE_VK_FORMAT_RGBA8_UNORM, .width = 8, .height = 8, .depth_or_layers = 1,
        .mip_levels = 1, .usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT | DOE_VK_TEXTURE_COPY_SRC };
    doe_vk_texture *texture = NULL;
    doe_vk_texture_view *view = NULL;
    require(doe_vk_texture_create(context, &texture_desc, &texture, &error), "target");
    const doe_vk_texture_view_desc view_desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1};
    require(doe_vk_texture_view_create(texture, &view_desc, &view, &error), "target view");
    const doe_vk_vertex_layout vertex_layout = {36, false};
    const doe_vk_vertex_attribute attributes[3] = {
        {0, 0, 0, DOE_VK_VERTEX_FLOAT32X3}, {1, 0, 12, DOE_VK_VERTEX_FLOAT32X4}, {2, 0, 28, DOE_VK_VERTEX_FLOAT32X2}
    };
    const doe_vk_binding_layout layout = {0, 0, DOE_VK_BINDING_UNIFORM};
    const doe_vk_render_pipeline_desc desc = { .vertex = vertex, .fragment = fragment,
        .color_format = DOE_VK_FORMAT_RGBA8_UNORM, .topology = DOE_VK_TRIANGLES, .color_write_mask = 15,
        .vertex_buffer_count = 1, .attribute_count = 3, .vertex_buffers = &vertex_layout, .attributes = attributes,
        .binding_count = 1, .bindings = &layout };
    doe_vk_pipeline *pipeline = NULL;
    require(doe_vk_render_pipeline_create(context, &desc, &pipeline, &error), "graphics pipeline");
    free((void *)vertex.words);
    free((void *)fragment.words);
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "commands");
    const doe_vk_clear_color blue = {.float32 = {0, 0, 1, 1}};
    const doe_vk_binding_resource binding = { .kind = DOE_VK_BINDING_UNIFORM, .value.buffer = {uniform, 0, 96} };
    const doe_vk_buffer_binding vertex_binding = {vertices, 0, sizeof(triangle)};
    doe_vk_render_draw draw = { .vertex_count = 3, .instance_count = 1,
        .viewport = {0, 0, 8, 8, 0, 1}, .scissor = {0, 0, 4, 8} };
    doe_vk_render_pass_draw pass_draws[2] = {
        {pipeline, &binding, 1, &vertex_binding, 1, draw},
        {pipeline, &binding, 1, &vertex_binding, 1, draw}
    };
    pass_draws[1].draw.scissor[0] = 4;
    pass_draws[1].draw.scissor[3] = 4;
    require(doe_vk_commands_render_pass(commands, view, &blue, pass_draws, 2), "one clear/two-draw pass");
    const doe_vk_texture_region region = {.extent = {8, 8, 1}};
    const doe_vk_texture_buffer_layout packed = {0};
    require(doe_vk_commands_read_texture(commands, texture, region, readback, packed), "render readback");
    doe_vk_buffer_release(uniform);
    doe_vk_buffer_release(vertices);
    doe_vk_texture_view_release(view);
    doe_vk_texture_release(texture);
    doe_vk_pipeline_release(pipeline);
    require(doe_vk_commands_submit(commands, &error), "submit");
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), &error), "wait");
    uint8_t pixels[8][8][4];
    require(doe_vk_buffer_read(readback, 0, pixels, sizeof(pixels)), "pixels");
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
        uint8_t expected[4] = {(x < 4 || y < 4) ? 255 : 0, 0, 255, 255};
        if (memcmp(pixels[y][x], expected, 4) != 0) {
            fprintf(stderr, "pixel mismatch at %u,%u: %u,%u,%u,%u\n", x, y,
                pixels[y][x][0], pixels[y][x][1], pixels[y][x][2], pixels[y][x][3]);
            return 1;
        }
    }
    require(doe_vk_commands_release(commands), "retire");
    doe_vk_buffer_release(readback);
    doe_vk_context_release(context);
    puts("PASS one real offscreen clear/two-draw pass, dynamic scissors, uniform descriptors, exact64-pixel oracle and early native caller release");
    return 0;
}
