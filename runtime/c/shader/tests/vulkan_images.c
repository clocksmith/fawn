/* SPDX-License-Identifier: MIT
 * Real C WGSL -> SPIR-V -> Vulkan image consumer. Software Vulkan is opt-in;
 * this focused integration check is not physical or full WebGPU qualification. */
#include "doe_shader.h"
#include "doe_vk_bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *allocate(void *user, size_t bytes, size_t alignment)
{
    (void)user;
    if (alignment > _Alignof(max_align_t)) return NULL;
    return malloc(bytes);
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
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--allow-software"))) return 2;
    const char source[] =
        "@group(0) @binding(0) var input_tex:texture_2d<f32>;"
        "@group(0) @binding(1) var output_tex:texture_storage_2d<rgba8unorm,write>;"
        "@group(0) @binding(2) var image_sampler:sampler;"
        "@compute @workgroup_size(8) fn copy_image(@builtin(global_invocation_id) id:vec3u){"
        "let coord=vec2i(i32(id.x),0);"
        "let dimensions=textureDimensions(input_tex);"
        "let loaded=textureLoad(input_tex,coord,0);"
        "let uv=(vec2f(63.0-f32(id.x),0.0)+vec2f(0.5))/vec2f(dimensions);"
        "let sampled=textureSampleLevel(input_tex,image_sampler,uv,0.0);"
        "textureStore(output_tex,coord,vec4f(loaded.r,sampled.g,loaded.b,sampled.a));}";
    static const char identity[] = "developer-source-image-integration";
    doe_shader_options shader_options = {
        .allocator = {NULL, allocate, deallocate},
        .capacities = {
            .typed = {.source_bytes=16384, .tokens=2048, .statements=128, .expressions=512,
                .types=32, .members=64, .resources=8, .access_steps=512, .constants=64,
                .constant_edges=128, .ir_instructions=2048, .ir_operands=512,
                .target_declarations=512, .traversal_items=97, .words=16384,
                .expression_depth=32, .control_depth=16, .type_syntax_depth=32},
            .entries=8, .entry_inputs=40, .entry_references=64},
        .selection = {.mode=DOE_COMPILER_SELECT_NONE},
        .compiler_identity=identity, .compiler_identity_size=sizeof(identity)-1
    };
    doe_shader *shader = NULL;
    doe_shader_error compile_error;
    if (doe_shader_create(&shader_options, source, sizeof(source)-1, &shader, &compile_error) != DOE_SHADER_OK) {
        fprintf(stderr, "WGSL compile: %s\n", compile_error.diagnostic.message);
        return 1;
    }
    doe_shader_view compiled;
    if (doe_shader_borrow(shader, &compiled) != DOE_SHADER_OK || compiled.handle_count != 3 ||
        compiled.counts.entries != 1 || compiled.entries[0].resource_reference_count != 3) return 1;
    doe_vk_options options = {.allow_cpu_adapter=argc == 2};
    doe_vk_context *context = NULL;
    doe_vk_error error;
    doe_vk_status status = doe_vk_context_create(&options, &context, &error);
    if (status != DOE_VK_OK) {
        printf("SKIP C image integration: %s at %s native=%d\n",
            doe_vk_status_name(status), error.operation, error.native_result);
        doe_shader_release(shader);
        return status == DOE_VK_UNSUPPORTED || status == DOE_VK_LOADER_UNAVAILABLE ? 77 : 1;
    }
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    printf("C image integration adapter: %s software=%u\n", caps.device_name, caps.software_adapter);
    uint8_t pixels[64][4];
    for (unsigned i=0; i<64; ++i) {
        pixels[i][0]=(uint8_t)(i*3);
        pixels[i][1]=(uint8_t)(255-i*2);
        pixels[i][2]=(uint8_t)(i+17);
        pixels[i][3]=(uint8_t)(i*4);
    }
    doe_vk_buffer *upload=NULL, *readback=NULL;
    require(doe_vk_buffer_create(context, sizeof(pixels), DOE_VK_BUFFER_COPY_SRC,
        DOE_VK_MEMORY_HOST, &upload, &error), "upload");
    require(doe_vk_buffer_create(context, sizeof(pixels), DOE_VK_BUFFER_COPY_DST,
        DOE_VK_MEMORY_HOST, &readback, &error), "readback");
    require(doe_vk_buffer_write(upload, 0, pixels, sizeof(pixels)), "pixels");
    doe_vk_texture_desc descriptor = {.dimension=DOE_VK_TEXTURE_2D,
        .format=DOE_VK_FORMAT_RGBA8_UNORM, .width=64, .height=1, .depth_or_layers=1,
        .mip_levels=1, .usage=DOE_VK_TEXTURE_COPY_DST|DOE_VK_TEXTURE_SAMPLED};
    doe_vk_texture *input=NULL, *output=NULL;
    doe_vk_texture_view *input_view=NULL, *output_view=NULL;
    require(doe_vk_texture_create(context, &descriptor, &input, &error), "input texture");
    descriptor.usage=DOE_VK_TEXTURE_COPY_DST|DOE_VK_TEXTURE_COPY_SRC|DOE_VK_TEXTURE_STORAGE;
    require(doe_vk_texture_create(context, &descriptor, &output, &error), "output texture");
    const doe_vk_texture_view_desc view_descriptor={DOE_VK_VIEW_2D,0,1,0,1, DOE_VK_ASPECT_ALL, 0, {0}};
    require(doe_vk_texture_view_create(input, &view_descriptor, &input_view, &error), "input view");
    require(doe_vk_texture_view_create(output, &view_descriptor, &output_view, &error), "output view");
    doe_vk_sampler *sampler=NULL;
    const doe_vk_sampler_desc sampler_descriptor={.max_lod=1};
    require(doe_vk_sampler_create(context, &sampler_descriptor, &sampler, &error), "sampler");
    const doe_vk_binding_layout layout[]={{0,0,DOE_VK_BINDING_SAMPLED_TEXTURE},
        {0,1,DOE_VK_BINDING_STORAGE_TEXTURE},{0,2,DOE_VK_BINDING_SAMPLER}};
    doe_vk_pipeline *pipeline=NULL;
    require(doe_vk_pipeline_create(context, compiled.words, compiled.counts.words,
        "copy_image", layout, 3, &pipeline, &error), "C image pipeline");
    doe_shader_release(shader);
    const doe_vk_binding_resource bindings[]={
        {.kind=DOE_VK_BINDING_SAMPLED_TEXTURE,.value.view=input_view},
        {.kind=DOE_VK_BINDING_STORAGE_TEXTURE,.value.view=output_view},
        {.kind=DOE_VK_BINDING_SAMPLER,.value.sampler=sampler}};
    doe_vk_commands *commands=NULL;
    require(doe_vk_commands_create(context, &commands, &error), "commands");
    const doe_vk_texture_region region={.extent={64,1,1}};
    const doe_vk_texture_buffer_layout packed={0};
    require(doe_vk_commands_upload_texture(commands, upload, packed, input, region), "input upload");
    require(doe_vk_commands_upload_texture(commands, upload, packed, output, region), "output initialize");
    /* Nine groups deliberately include eight out-of-range stores. */
    require(doe_vk_commands_dispatch_resources(commands, pipeline, bindings, 3, 9, 1, 1), "image dispatch");
    require(doe_vk_commands_read_texture(commands, output, region, readback, packed), "image readback");
    doe_vk_buffer_release(upload);
    doe_vk_texture_view_release(input_view);
    doe_vk_texture_view_release(output_view);
    doe_vk_texture_release(input);
    doe_vk_texture_release(output);
    doe_vk_sampler_release(sampler);
    doe_vk_pipeline_release(pipeline);
    require(doe_vk_commands_submit(commands, &error), "submit");
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), &error), "complete");
    uint8_t result[64][4];
    require(doe_vk_buffer_read(readback, 0, result, sizeof(result)), "read pixels");
    for (unsigned i=0; i<64; ++i) {
        const uint8_t expected[]={pixels[i][0],pixels[63-i][1],pixels[i][2],pixels[63-i][3]};
        for (unsigned j=0; j<4; ++j) if (result[i][j] != expected[j]) {
            fprintf(stderr, "pixel %u channel %u: %u != %u\n", i,j,result[i][j],expected[j]);
            return 1;
        }
    }
    require(doe_vk_commands_release(commands), "retire");
    doe_vk_buffer_release(readback);
    doe_vk_context_release(context);
    puts("PASS C WGSL image load/sample/store, reversed-coordinate exact256-byte oracle, OOB stores and early release");
    return 0;
}
