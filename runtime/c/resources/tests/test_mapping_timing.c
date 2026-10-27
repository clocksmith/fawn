/* SPDX-License-Identifier: MIT
 * Host owner controls: recording may precede mapped-at-creation unmap. Queue
 * admission still rejects mapped, pending and destroyed retained buffers. */
#include "fixture.h"
#include "../src/internal.h"

enum submission_case { UNMAPPED, MAPPED, DESTROYED, PENDING };
static doe_resource_buffer *mapped_buffer(fixture *f, native_token *token, uint64_t usage)
{
    doe_resource_config c = config(f);
    doe_resource_buffer_desc desc = {64, usage, true};
    doe_resource_buffer *result;
    assert(doe_resource_buffer_create(&c, &desc, native(f, token), &result) == DOE_RESOURCE_OK);
    return result;
}
static void submit_case(fixture *f, doe_resource_encoder *encoder, doe_resource_buffer *buffer,
    native_token *token, enum submission_case scenario)
{
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    if (scenario != MAPPED) assert(doe_resource_buffer_unmap(buffer) == DOE_RESOURCE_OK);
    if (scenario == DESTROYED) doe_resource_buffer_destroy(buffer);
    /* White-box defensive queue-state control, not a claim that primary GPU
     * usages admit public MapRead/MapWrite (descriptor admission forbids that). */
    if (scenario == PENDING) buffer->map_pending = true;
    doe_resource_buffer_release(buffer);
    assert(!token->releases);
    doe_resource_status expected = scenario == UNMAPPED ? DOE_RESOURCE_OK :
        scenario == DESTROYED ? DOE_RESOURCE_UNAVAILABLE : DOE_RESOURCE_STATE;
    assert(doe_resource_commands_begin_submit(commands) == expected);
    if (expected == DOE_RESOURCE_OK)
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    assert(token->releases == 1);
    empty(f);
}
static void compute(bool uniform, enum submission_case scenario)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token token = {0}, pipeline_token = {0};
    doe_resource_buffer *buffer = mapped_buffer(&f, &token,
        uniform ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage);
    doe_resource_layout_entry entry = {.kind=DOE_BIND_BUFFER, .visibility=WGPUShaderStage_Compute,
        .buffer_type=uniform ? WGPUBufferBindingType_Uniform : WGPUBufferBindingType_Storage};
    doe_resource_layout *layout;
    assert(doe_resource_layout_create(&c, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    doe_resource_binding binding = {.kind=DOE_BIND_BUFFER, .resource.buffer={buffer, 0, 64}};
    doe_resource_binding_limits limits = {16, 16, 256, 256};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(layout, &binding, 1, &limits, &group) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout *pipeline_layout;
    assert(doe_resource_pipeline_layout_create(&c, &layout, 1, 0, &pipeline_layout) == DOE_RESOURCE_OK);
    uint32_t workgroup[] = {1, 1, 1};
    doe_resource_pipeline *pipeline;
    assert(doe_resource_pipeline_create(&c, pipeline_layout, native(&f, &pipeline_token), workgroup, &pipeline) == DOE_RESOURCE_OK);
    doe_resource_encoder *encoder;
    doe_resource_compute_pass *pass;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_compute(encoder, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_compute_pass_set_pipeline(pass, pipeline) == DOE_RESOURCE_OK);
    doe_resource_dispatch_group dispatch = {group, NULL, 0};
    assert(doe_resource_compute_pass_set_group(pass, 0, &dispatch) == DOE_RESOURCE_OK);
    assert(doe_resource_compute_pass_dispatch(pass, 1, 1, 1) == DOE_RESOURCE_OK);
    assert(doe_resource_compute_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_compute_pass_release(pass);
    doe_resource_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(pipeline_layout);
    doe_resource_layout_release(layout);
    doe_resource_bind_group_release(group);
    submit_case(&f, encoder, buffer, &token, scenario);
    assert(pipeline_token.releases == 1);
}
static doe_resource_view *attachment(fixture *f, native_token tokens[2])
{
    doe_resource_config c = config(f);
    doe_resource_texture_desc desc = texture_desc();
    desc.usage = WGPUTextureUsage_RenderAttachment;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    doe_resource_view *view;
    doe_resource_view_desc view_desc = {0};
    assert(doe_resource_texture_create(&c, &desc, &limits, native(f, &tokens[0]), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &view_desc, native(f, &tokens[1]), &view) == DOE_RESOURCE_OK);
    doe_resource_texture_release(texture);
    return view;
}
static void render(unsigned role, bool bundled, enum submission_case scenario)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token token = {0}, pipeline_token = {0}, view_tokens[2] = {{0}};
    uint64_t usage = role == 0 ? WGPUBufferUsage_Vertex : role == 1 ? WGPUBufferUsage_Index : WGPUBufferUsage_Indirect;
    doe_resource_buffer *buffer = mapped_buffer(&f, &token, usage);
    doe_resource_pipeline_layout *layout;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
    doe_resource_render_pipeline_desc desc = {.color_format=WGPUTextureFormat_RGBA8Unorm, .samples=1};
    if (role == 0) {
        desc.vertex_count = 1;
        desc.vertices[0] = (doe_resource_vertex_layout){.stride=4, .last_attribute_end=4,
            .step_mode=WGPUVertexStepMode_Vertex};
    }
    doe_resource_render_pipeline *pipeline;
    assert(doe_resource_render_pipeline_create(&c, layout, &desc, native(&f, &pipeline_token), &pipeline) == DOE_RESOURCE_OK);
    doe_resource_view *view = attachment(&f, view_tokens);
    doe_resource_render_targets targets = {.color=view, .color_load=WGPULoadOp_Clear, .color_store=WGPUStoreOp_Store};
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    if (bundled) {
        doe_resource_render_bundle_desc signature = {.color_format=desc.color_format, .samples=1};
        doe_resource_bundle_encoder *builder;
        assert(doe_resource_bundle_encoder_create(&c, &signature, &builder) == DOE_RESOURCE_OK);
        assert(doe_resource_bundle_encoder_set_pipeline(builder, pipeline) == DOE_RESOURCE_OK);
        if (role == 0) assert(doe_resource_bundle_encoder_set_vertex(builder, 0, buffer, 0, 64) == DOE_RESOURCE_OK);
        if (role == 1) assert(doe_resource_bundle_encoder_set_index(builder, buffer, WGPUIndexFormat_Uint16, 0, 64) == DOE_RESOURCE_OK);
        if (role == 2) assert(doe_resource_bundle_encoder_draw_indirect(builder, buffer, 0, false) == DOE_RESOURCE_OK);
        else assert(doe_resource_bundle_encoder_draw(builder, role == 1, 3, 1, 0, 0, 0) == DOE_RESOURCE_OK);
        doe_resource_render_bundle *bundle;
        assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OK);
        doe_resource_bundle_encoder_release(builder);
        assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_OK);
        doe_resource_render_bundle_release(bundle);
    } else {
        assert(doe_resource_render_pass_set_pipeline(pass, pipeline) == DOE_RESOURCE_OK);
        if (role == 0) assert(doe_resource_render_pass_set_vertex(pass, 0, buffer, 0, 64) == DOE_RESOURCE_OK);
        if (role == 1) assert(doe_resource_render_pass_set_index(pass, buffer, WGPUIndexFormat_Uint16, 0, 64) == DOE_RESOURCE_OK);
        if (role == 2) assert(doe_resource_render_pass_draw_indirect(pass, buffer, 0, false) == DOE_RESOURCE_OK);
        else if (role == 1) assert(doe_resource_render_pass_draw_indexed(pass, 3, 1, 0, 0, 0) == DOE_RESOURCE_OK);
        else assert(doe_resource_render_pass_draw(pass, 3, 1, 0, 0) == DOE_RESOURCE_OK);
    }
    assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_render_pass_release(pass);
    doe_resource_render_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(layout);
    doe_resource_view_release(view);
    submit_case(&f, encoder, buffer, &token, scenario);
    assert(pipeline_token.releases == 1 && view_tokens[0].releases == 1 && view_tokens[1].releases == 1);
}
static void revalidate_open_submission(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token source_token = {0}, target_token = {0};
    doe_resource_buffer *source = buffer(&f,&source_token,64,WGPUBufferUsage_CopySrc);
    doe_resource_buffer *target = buffer(&f,&target_token,64,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    doe_resource_encoder *encoder = NULL;
    doe_resource_commands *commands = NULL;
    assert(doe_resource_encoder_create(&c,&encoder) == DOE_RESOURCE_OK);
    const doe_resource_buffer_copy copy = {source,target,0,0,64};
    assert(doe_resource_encoder_copy_buffer(encoder,&copy,DOE_RESOURCE_DISJOINT_RANGES) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_finish(encoder,&commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_commands_validate_submission(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    const unsigned references = f.device_references;
    assert(doe_resource_commands_validate_submission(commands) == DOE_RESOURCE_OK);
    assert(doe_resource_commands_validate_submission(commands) == DOE_RESOURCE_OK);
    uint64_t generation = 0;
    assert(doe_resource_buffer_map_begin(target,WGPUMapMode_Read,0,64,&generation) == DOE_RESOURCE_OK);
    assert(doe_resource_commands_validate_submission(commands) == DOE_RESOURCE_STATE);
    assert(doe_resource_buffer_map_complete(target,generation,false,NULL) == DOE_RESOURCE_EXECUTION_FAILED);
    assert(doe_resource_commands_validate_submission(commands) == DOE_RESOURCE_OK);
    f.alive = false;
    assert(doe_resource_commands_validate_submission(commands) == DOE_RESOURCE_UNAVAILABLE);
    f.alive = true;
    doe_resource_buffer_destroy(target);
    assert(doe_resource_commands_validate_submission(commands) == DOE_RESOURCE_UNAVAILABLE);
    assert(f.device_references == references);
    assert(doe_resource_commands_settle(commands,DOE_RESOURCE_EXECUTION_FAILED) == DOE_RESOURCE_EXECUTION_FAILED);
    assert(doe_resource_commands_validate_submission(commands) == DOE_RESOURCE_STATE);
    doe_resource_commands_release(commands);
    doe_resource_buffer_release(source); doe_resource_buffer_release(target);
    assert(source_token.releases == 1 && target_token.releases == 1);
    empty(&f);
}

int main(void)
{
    revalidate_open_submission();
    for (enum submission_case scenario = UNMAPPED; scenario <= PENDING; scenario++) {
        compute(false, scenario);
        compute(true, scenario);
        for (unsigned role = 0; role < 3; role++) {
            render(role, false, scenario);
            render(role, true, scenario);
        }
    }
    puts("PASS: 32 mapped recording/submission timing and retention controls");
    return 0;
}
