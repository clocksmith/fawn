/* SPDX-License-Identifier: MIT
 * Actual C WGSL graphics compilation and asymmetric Vulkan readback. Software
 * adapters are explicit; no target-opcode replay or physical qualification. */
#include "doe_shader.h"
#include "doe_vk_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *allocate(void *user, size_t bytes, size_t alignment)
{
    (void)user;
    return alignment <= _Alignof(max_align_t) ? malloc(bytes) : NULL;
}
static void deallocate(void *user, void *memory, size_t bytes, size_t alignment)
{
    (void)user;
    (void)bytes;
    (void)alignment;
    free(memory);
}
static void require(doe_vk_status status, const char *operation)
{
    if (status == DOE_VK_OK) return;
    fprintf(stderr, "%s: %s\n", operation, doe_vk_status_name(status));
    exit(1);
}
int main(int argc, char **argv)
{
    bool software = false, demote = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--allow-software")) software = true;
        else if (!strcmp(argv[i], "--demote")) demote = true;
        else return 2;
    }
    char source[1024];
    const int source_bytes = snprintf(source, sizeof(source),
        "struct Output{@builtin(position) position:vec4f,"
        "@location(0) @interpolate(flat) tag:u32}"
        "@vertex fn vertex(@location(0) point:vec2f)->Output{"
        "return Output(vec4f(point,0.0,1.0),7u);}"
        "@fragment fn fragment(input:Output)->@location(0) vec4f{"
        "%sif(input.tag==7u){return vec4f(1.0,0.0,1.0,1.0);}return vec4f(0.0);}",
        demote ? "if(input.position.x>=4.0){discard;}" : "");
    if (source_bytes < 0 || (size_t)source_bytes >= sizeof(source)) return 2;
    const char identity[] = "developer-source-graphics-integration";
    const doe_shader_options shader_options = {
        .allocator = {NULL, allocate, deallocate},
        .capacities = {
            .typed = {.source_bytes=16384, .tokens=2048, .statements=128, .expressions=512,
                .types=32, .members=64, .resources=8, .access_steps=512, .constants=64,
                .constant_edges=128, .ir_instructions=2048, .ir_operands=512,
                .target_declarations=512, .traversal_items=97, .words=16384,
                .expression_depth=32, .control_depth=16, .type_syntax_depth=32},
            .entries=8, .entry_inputs=40, .entry_references=64},
        .selection = {.mode=DOE_COMPILER_SELECT_NONE},
        .compiler_identity=identity, .compiler_identity_size=sizeof(identity)-1,
        .interface_attributes=32, .stage_interfaces=32
    };
    doe_shader *shader = NULL;
    doe_shader_error compile_error;
    if (doe_shader_create(&shader_options, source, (size_t)source_bytes, &shader, &compile_error) != DOE_SHADER_OK) {
        fprintf(stderr, "WGSL compile: %s\n", compile_error.diagnostic.message);
        return 1;
    }
    doe_shader_view compiled;
    if (doe_shader_borrow(shader, &compiled) != DOE_SHADER_OK || compiled.counts.entries != 2 ||
        compiled.interface_count != 6 || compiled.entries[0].stage != DOE_COMPILER_STAGE_VERTEX ||
        compiled.entries[1].stage != DOE_COMPILER_STAGE_FRAGMENT) return 1;
    const doe_vk_options options = {.allow_cpu_adapter=software, .require_graphics_queue=true,
        .enable_shader_demote_if_supported=demote};
    doe_vk_context *context = NULL;
    doe_vk_error error;
    const doe_vk_status status = doe_vk_context_create(&options, &context, &error);
    if (status != DOE_VK_OK) {
        printf("SKIP C graphics integration: %s at %s native=%d\n",
            doe_vk_status_name(status), error.operation, error.native_result);
        doe_shader_release(shader);
        return status == DOE_VK_UNSUPPORTED || status == DOE_VK_LOADER_UNAVAILABLE ? 77 : 1;
    }
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    if (demote && !caps.shader_demote_to_helper_invocation) {
        puts("SKIP demote draw: requested optional feature unavailable");
        doe_shader_release(shader);
        doe_vk_context_release(context);
        return 77;
    }
    printf("C graphics integration adapter: %s software=%u\n", caps.device_name, caps.software_adapter);
    const float positions[6][2] = {{-1,0},{1,0},{-1,1},{-1,1},{1,0},{1,1}};
    doe_vk_buffer *vertices = NULL, *readback = NULL;
    require(doe_vk_buffer_create(context, sizeof(positions), DOE_VK_BUFFER_VERTEX,
        DOE_VK_MEMORY_HOST, &vertices, &error), "vertices");
    require(doe_vk_buffer_write(vertices, 0, positions, sizeof(positions)), "positions");
    require(doe_vk_buffer_create(context, 256, DOE_VK_BUFFER_COPY_DST,
        DOE_VK_MEMORY_HOST, &readback, &error), "readback");
    const doe_vk_texture_desc target_desc = {.dimension=DOE_VK_TEXTURE_2D,
        .format=DOE_VK_FORMAT_RGBA8_UNORM, .width=8, .height=8, .depth_or_layers=1,
        .mip_levels=1, .usage=DOE_VK_TEXTURE_COLOR_ATTACHMENT|DOE_VK_TEXTURE_COPY_SRC};
    doe_vk_texture *target = NULL;
    doe_vk_texture_view *view = NULL;
    require(doe_vk_texture_create(context, &target_desc, &target, &error), "target");
    const doe_vk_texture_view_desc view_desc = {DOE_VK_VIEW_2D,0,1,0,1, DOE_VK_ASPECT_ALL, 0, {0}};
    require(doe_vk_texture_view_create(target, &view_desc, &view, &error), "view");
    const doe_vk_vertex_layout vertex_layout = {sizeof(positions[0]),false};
    const doe_vk_vertex_attribute attribute = {0,0,0,DOE_VK_VERTEX_FLOAT32X2};
    const doe_vk_render_pipeline_desc pipeline_desc = {
        .vertex = {compiled.words,compiled.counts.words,"vertex"},
        .fragment = {compiled.words,compiled.counts.words,"fragment"},
        .color_format=DOE_VK_FORMAT_RGBA8_UNORM, .topology=DOE_VK_TRIANGLES,
        .color_write_mask=15, .vertex_buffer_count=1, .attribute_count=1,
        .vertex_buffers=&vertex_layout, .attributes=&attribute
    };
    doe_vk_pipeline *pipeline = NULL;
    require(doe_vk_render_pipeline_create(context, &pipeline_desc, &pipeline, &error), "C graphics pipeline");
    doe_shader_release(shader);
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "commands");
    const doe_vk_clear_color blue = {.float32={0,0,1,1}};
    require(doe_vk_commands_clear_color(commands, view, blue), "clear");
    const doe_vk_buffer_binding vertex_binding = {vertices,0,sizeof(positions)};
    const doe_vk_render_draw draw = {.vertex_count=6, .instance_count=1,
        .viewport={0,0,8,8,0,1}, .scissor={0,0,8,8},
        .viewport_convention=DOE_VK_VIEWPORT_WEBGPU};
    require(doe_vk_commands_draw(commands,pipeline,view,NULL,0,&vertex_binding,1,&draw), "draw");
    const doe_vk_texture_region region = {.extent={8,8,1}};
    const doe_vk_texture_buffer_layout packed = {0};
    require(doe_vk_commands_read_texture(commands,target,region,readback,packed), "readback");
    doe_vk_buffer_release(vertices);
    doe_vk_texture_view_release(view);
    doe_vk_texture_release(target);
    doe_vk_pipeline_release(pipeline);
    require(doe_vk_commands_submit(commands,&error), "submit");
    require(doe_vk_commands_wait(commands,UINT64_C(5000000000),&error), "wait");
    uint8_t pixels[8][8][4];
    require(doe_vk_buffer_read(readback,0,pixels,sizeof(pixels)), "pixels");
    for (unsigned y=0; y<8; ++y) for (unsigned x=0; x<8; ++x) {
        const uint8_t expected[4] = {y<4 && (!demote || x<4) ? 255 : 0,0,255,255};
        if (memcmp(pixels[y][x],expected,4)) {
            fprintf(stderr,"orientation/color mismatch %u,%u: %u,%u,%u,%u\n",x,y,
                pixels[y][x][0],pixels[y][x][1],pixels[y][x][2],pixels[y][x][3]);
            return 1;
        }
    }
    require(doe_vk_commands_release(commands), "retire");
    doe_vk_buffer_release(readback);
    doe_vk_context_release(context);
    if (demote) puts("PASS actual C discard draw: explicit enabled capability, asymmetric pixel suppression; derivative/helper execution parity not claimed");
    puts("PASS C WGSL graphics: asymmetric top-half pixels, flat stage IO, exact256-byte oracle and early caller release");
    return 0;
}
