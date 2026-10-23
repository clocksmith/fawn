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
    if (caps.max_vertex_input_bindings < 2 || caps.max_vertex_input_attributes < 3 ||
        caps.max_vertex_input_binding_stride < 36 || caps.max_vertex_input_attribute_offset < 28 ||
        caps.max_vertex_output_components == 0 || caps.max_fragment_input_components == 0) return 1;
    doe_vk_buffer *uniform = NULL, *vertices = NULL, *readback = NULL, *blend_readback = NULL;
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_UNIFORM, DOE_VK_MEMORY_HOST, &uniform, &error), "uniform");
    require(doe_vk_buffer_create(context, 108, DOE_VK_BUFFER_VERTEX, DOE_VK_MEMORY_HOST, &vertices, &error), "vertices");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &readback, &error), "readback");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &blend_readback, &error), "blend readback");
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
    const doe_vk_vertex_layout vertex_layout[2] = {{0, false}, {36, false}};
    const doe_vk_vertex_attribute attributes[3] = {
        {0, 1, 0, DOE_VK_VERTEX_FLOAT32X3}, {1, 1, 12, DOE_VK_VERTEX_FLOAT32X4}, {2, 1, 28, DOE_VK_VERTEX_FLOAT32X2}
    };
    const doe_vk_binding_layout layout = {0, 0, DOE_VK_BINDING_UNIFORM};
    const doe_vk_render_pipeline_desc desc = { .blend_enabled = true,
        .blend_constant = {1, 0, 1, 1}, .color_source_factor = 10, .alpha_source_factor = 1, .vertex = vertex, .fragment = fragment,
        .color_format = DOE_VK_FORMAT_RGBA8_UNORM, .topology = DOE_VK_TRIANGLES, .color_write_mask = 15,
        .vertex_buffer_count = 2, .attribute_count = 3, .vertex_buffers = vertex_layout, .attributes = attributes,
        .binding_count = 1, .bindings = &layout };
    doe_vk_pipeline *pipeline = NULL;
    require(doe_vk_render_pipeline_create(context, &desc, &pipeline, &error), "graphics pipeline");
    doe_vk_texture_desc depth_desc = texture_desc;
    depth_desc.format = DOE_VK_FORMAT_D32_FLOAT_S8_UINT;
    depth_desc.usage = DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT;
    doe_vk_texture *depth_texture = NULL;
    doe_vk_texture_view *depth_view = NULL;
    doe_vk_buffer *depth_readback = NULL;
    require(doe_vk_texture_create(context, &depth_desc, &depth_texture, &error), "depth/stencil target");
    require(doe_vk_texture_view_create(depth_texture, &view_desc, &depth_view, &error), "depth/stencil view");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_COPY_DST,
        DOE_VK_MEMORY_HOST, &depth_readback, &error), "depth pixels");
    doe_vk_render_pipeline_desc depth_pipeline_desc = desc;
    depth_pipeline_desc.depth_stencil_format = depth_desc.format;
    depth_pipeline_desc.depth_test = true;
    depth_pipeline_desc.depth_write = true;
    depth_pipeline_desc.depth_compare = 1;
    depth_pipeline_desc.stencil_test = true;
    depth_pipeline_desc.stencil_read_mask = 255;
    depth_pipeline_desc.stencil_write_mask = 255;
    depth_pipeline_desc.stencil_front = (doe_vk_stencil_face){0, 2, 0, 7};
    depth_pipeline_desc.stencil_back = depth_pipeline_desc.stencil_front;
    doe_vk_pipeline *depth_write = NULL, *depth_read = NULL;
    require(doe_vk_render_pipeline_create(context, &depth_pipeline_desc, &depth_write, &error), "depth/stencil write pipeline");
    depth_pipeline_desc.depth_write = false;
    depth_pipeline_desc.stencil_write_mask = 0;
    depth_pipeline_desc.stencil_front = (doe_vk_stencil_face){0, 0, 0, 2};
    depth_pipeline_desc.stencil_back = depth_pipeline_desc.stencil_front;
    require(doe_vk_render_pipeline_create(context, &depth_pipeline_desc, &depth_read, &error), "depth/stencil read pipeline");
    free((void *)vertex.words);
    free((void *)fragment.words);
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "commands");
    const doe_vk_clear_color blue = {.float32 = {0, 0, 1, 1}};
    const doe_vk_binding_resource binding = { .kind = DOE_VK_BINDING_UNIFORM, .value.buffer = {uniform, 0, 96} };
    const doe_vk_buffer_binding vertex_binding[2] = {{NULL, 0, 0}, {vertices, 0, sizeof(triangle)}};
    doe_vk_render_draw draw = { .vertex_count = 3, .instance_count = 1,
        .viewport = {0, 0, 8, 8, 0, 1}, .scissor = {0, 0, 4, 8} };
    doe_vk_render_pass_draw pass_draws[2] = {
        {pipeline, &binding, 1, vertex_binding, 2, draw},
        {pipeline, &binding, 1, vertex_binding, 2, draw}
    };
    pass_draws[1].draw.scissor[0] = 4;
    pass_draws[1].draw.scissor[3] = 4;
    require(doe_vk_commands_render_pass(commands, view, &blue, pass_draws, 2), "one clear/two-draw pass");
    doe_vk_query_pool *queries = NULL;
    doe_vk_buffer *query_output = NULL;
    require(doe_vk_occlusion_pool_create(context, 4, &queries, &error), "occlusion pool");
    require(doe_vk_buffer_create(context, 64, DOE_VK_BUFFER_COPY_DST,
        DOE_VK_MEMORY_HOST, &query_output, &error), "query output");
    uint64_t sentinels[8];
    for (unsigned i = 0; i < 8; ++i) sentinels[i] = UINT64_MAX;
    require(doe_vk_buffer_write(query_output, 0, sentinels, sizeof(sentinels)), "query sentinels");
    doe_vk_render_pass_op ops[8] = {
        {.kind = DOE_VK_RENDER_BEGIN_OCCLUSION, .value.query_index = 0},
        {.kind = DOE_VK_RENDER_DRAW, .value.draw = pass_draws[0]},
        {.kind = DOE_VK_RENDER_END_OCCLUSION},
        {.kind = DOE_VK_RENDER_BEGIN_OCCLUSION, .value.query_index = 1},
        {.kind = DOE_VK_RENDER_END_OCCLUSION},
        {.kind = DOE_VK_RENDER_BEGIN_OCCLUSION, .value.query_index = 2},
        {.kind = DOE_VK_RENDER_DRAW, .value.draw = pass_draws[1]},
        {.kind = DOE_VK_RENDER_END_OCCLUSION}
    };
    ops[6].value.draw.draw.scissor[2] = 0;
    require(doe_vk_commands_render_pass_ops(commands, view, NULL, queries, ops, 8), "hit/empty/clipped occlusion pass");
    require(doe_vk_commands_resolve_queries(commands, queries, 0, 4, query_output, 0), "first occlusion resolve");
    const doe_vk_render_pass_op reused[2] = {
        {.kind = DOE_VK_RENDER_BEGIN_OCCLUSION, .value.query_index = 0},
        {.kind = DOE_VK_RENDER_END_OCCLUSION}
    };
    require(doe_vk_commands_render_pass_ops(commands, view, NULL, queries, reused, 2), "later pass index reuse");
    require(doe_vk_commands_resolve_queries(commands, queries, 0, 4, query_output, 32), "reused occlusion resolve");
    doe_vk_query_pool_release(queries);
    const doe_vk_texture_region region = {.extent = {8, 8, 1}};
    const doe_vk_texture_buffer_layout packed = {0};
    require(doe_vk_commands_read_texture(commands, texture, region, readback, packed), "render readback");
    pass_draws[0].draw.override_blend_constant = true;
    pass_draws[0].draw.blend_constant[0] = 1;
    pass_draws[0].draw.blend_constant[3] = 1;
    pass_draws[1].draw.override_blend_constant = true;
    pass_draws[1].draw.blend_constant[2] = 1;
    pass_draws[1].draw.blend_constant[3] = 1;
    pass_draws[1].draw.scissor[3] = 8;
    require(doe_vk_commands_render_pass(commands, view, NULL, pass_draws, 2), "two dynamic blend constants");
    require(doe_vk_commands_read_texture(commands, texture, region, blend_readback, packed), "blend pixels");
    doe_vk_render_pass_desc depth_pass = {.color = view, .clear_color = &blue,
        .depth_stencil = depth_view, .clear_depth = true, .clear_stencil = true,
        .depth_clear_value = 1, .stencil_clear_value = 0};
    doe_vk_render_pass_op depth_ops[4] = {0};
    for (unsigned i = 0; i < 4; ++i) {
        depth_ops[i].kind = DOE_VK_RENDER_DRAW;
        depth_ops[i].value.draw = pass_draws[0];
        depth_ops[i].value.draw.pipeline = depth_write;
        depth_ops[i].value.draw.draw.scissor[2] = 8;
        depth_ops[i].value.draw.draw.blend_constant[0] = 1;
        depth_ops[i].value.draw.draw.blend_constant[2] = 0;
    }
    depth_ops[0].value.draw.draw.viewport[4] = 0.75f;
    depth_ops[0].value.draw.draw.stencil_reference = 5;
    depth_ops[1].value.draw.draw.viewport[4] = 0.25f;
    depth_ops[1].value.draw.draw.scissor[2] = 4;
    depth_ops[1].value.draw.draw.blend_constant[0] = 0;
    depth_ops[1].value.draw.draw.blend_constant[2] = 1;
    depth_ops[1].value.draw.draw.stencil_reference = 9;
    depth_ops[2].value.draw.draw.viewport[4] = 0.9f;
    depth_ops[2].value.draw.draw.blend_constant[2] = 1;
    depth_ops[2].value.draw.draw.stencil_reference = 11;
    require(doe_vk_commands_render_pass_full(commands, &depth_pass, NULL, depth_ops, 3), "depth LESS and stencil replace");
    depth_pass.clear_color = NULL;
    depth_pass.clear_depth = depth_pass.clear_stencil = false;
    depth_pass.depth_read_only = depth_pass.stencil_read_only = true;
    require(doe_vk_commands_render_pass_full(commands, &depth_pass, NULL, NULL, 0), "empty read-only depth/stencil LOAD");
    for (unsigned i = 0; i < 4; ++i) depth_ops[i].value.draw.pipeline = depth_read;
    depth_ops[0].value.draw.draw.scissor[2] = 4;
    depth_ops[0].value.draw.draw.viewport[4] = 0.1f;
    depth_ops[0].value.draw.draw.stencil_reference = 9;
    depth_ops[1].value.draw.draw.scissor[0] = 4;
    depth_ops[1].value.draw.draw.viewport[4] = 0.5f;
    depth_ops[1].value.draw.draw.stencil_reference = 5;
    depth_ops[2].value.draw.draw.scissor[2] = 4;
    depth_ops[2].value.draw.draw.viewport[4] = 0.5f;
    depth_ops[2].value.draw.draw.stencil_reference = 9;
    depth_ops[3].value.draw.draw.viewport[4] = 0.1f;
    depth_ops[3].value.draw.draw.stencil_reference = 7;
    depth_ops[3].value.draw.draw.blend_constant[2] = 1;
    require(doe_vk_commands_render_pass_full(commands, &depth_pass, NULL, depth_ops, 4), "loaded depth and stencil failures preserve pixels");
    require(doe_vk_commands_read_texture(commands, texture, region, depth_readback, packed), "depth/stencil pixels");
    doe_vk_pipeline_release(depth_write);
    doe_vk_pipeline_release(depth_read);
    doe_vk_texture_view_release(depth_view);
    doe_vk_texture_release(depth_texture);
    doe_vk_buffer_release(uniform);
    doe_vk_buffer_release(vertices);
    doe_vk_texture_view_release(view);
    doe_vk_texture_release(texture);
    doe_vk_pipeline_release(pipeline);
    require(doe_vk_commands_submit(commands, &error), "submit");
    if (doe_vk_commands_release(commands) != DOE_VK_BUSY) return 1;
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
    require(doe_vk_buffer_read(blend_readback, 0, pixels, sizeof(pixels)), "dynamic blend readback");
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
        const uint8_t expected[4] = {x < 4 ? 255 : 0, 0, x < 4 ? 0 : 255, 255};
        if (memcmp(pixels[y][x], expected, 4) != 0) {
            fprintf(stderr, "dynamic blend mismatch at %u,%u\n", x, y);
            return 1;
        }
    }
    require(doe_vk_buffer_read(depth_readback, 0, pixels, sizeof(pixels)), "depth/stencil readback");
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
        const uint8_t expected[4] = {x < 4 ? 255 : 0, 0, x < 4 ? 0 : 255, 255};
        if (memcmp(pixels[y][x], expected, 4) != 0) {
            fprintf(stderr, "depth/stencil mismatch at %u,%u: %u,%u,%u,%u\n", x, y,
                pixels[y][x][0], pixels[y][x][1], pixels[y][x][2], pixels[y][x][3]);
            return 1;
        }
    }
    uint64_t values[8];
    require(doe_vk_buffer_read(query_output, 0, values, sizeof(values)), "occlusion results");
    if (values[0] == 0) {
        fputs("native occlusion hit unexpectedly zero\n", stderr);
        return 1;
    }
    for (unsigned i = 1; i < 8; ++i) if (values[i] != 0) {
        fprintf(stderr, "native empty/clipped/unwritten/reused slot %u not zero\n", i);
        return 1;
    }
    require(doe_vk_commands_release(commands), "retire");
    doe_vk_buffer_release(query_output);
    doe_vk_buffer_release(readback);
    doe_vk_buffer_release(blend_readback);
    doe_vk_buffer_release(depth_readback);
    doe_vk_context_release(context);
    puts("PASS one real offscreen clear/two-draw pass, dynamic scissors, an unused NULL vertex slot, uniform descriptors, exact64-pixel raw and dynamic-blend oracles, real occlusion hit/empty/clipped/unwritten results and later-pass index reuse, early native caller release");
    puts("PASS real D32S8 clear, independent depth LESS/stencil EQUAL tests, dynamic references, empty read-only LOAD, exact64-pixel depth/stencil oracle and early leases");
    return 0;
}
