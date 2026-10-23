/* SPDX-License-Identifier: MIT */
#include "fixture.h"

static void test_shader_and_compute_pass(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token shader_token = {0}, pipeline_token = {0}, indirect_token = {0};
    doe_resource_shader *shader;
    assert(doe_resource_shader_create(&c, native(&f, &shader_token), &shader) == DOE_RESOURCE_OK);
    assert(doe_resource_shader_device(shader) == &f && doe_resource_shader_native(shader) == &shader_token);
    doe_resource_shader_retain(shader);
    doe_resource_shader_release(shader);
    assert(!shader_token.releases);
    doe_resource_shader_release(shader);
    assert(shader_token.releases == 1);
    doe_resource_pipeline_layout *layout;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
    assert(doe_resource_pipeline_layout_count(layout) == 0 && !doe_resource_pipeline_layout_group(layout, 0));
    doe_resource_pipeline *pipeline;
    uint32_t workgroup[] = {1, 1, 1};
    assert(doe_resource_pipeline_create(&c, layout, native(&f, &pipeline_token), workgroup, &pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_pipeline_get_layout(pipeline) == layout);
    doe_resource_encoder *encoder;
    doe_resource_compute_pass *pass;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_compute(encoder, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_compute_pass_set_pipeline(pass, pipeline) == DOE_RESOURCE_OK);
    doe_resource_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(layout);
    doe_resource_compute_pass_retain(pass);
    doe_resource_compute_pass_release(pass);
    assert(doe_resource_compute_pass_dispatch(pass, 2, 3, 4) == DOE_RESOURCE_OK);
    doe_resource_buffer *arguments = buffer(&f, &indirect_token, 12, WGPUBufferUsage_Indirect);
    assert(doe_resource_compute_pass_dispatch_indirect(pass, arguments, 0) == DOE_RESOURCE_OK);
    doe_resource_buffer_release(arguments);
    assert(doe_resource_compute_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_compute_pass_release(pass);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_commands_device(commands) == &f);
    doe_resource_commands_release(commands);
    assert(pipeline_token.releases == 1 && indirect_token.releases == 1);
    empty(&f);

    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_compute(encoder, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_STATE);
    doe_resource_encoder_release(encoder);
    doe_resource_compute_pass_release(pass);
    empty(&f);
}

static void test_query_ownership(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token query_token = {0}, buffer_token = {0};
    doe_resource_query_set *set;
    assert(doe_resource_query_set_create(&c, WGPUQueryType_Timestamp, 8,
        native(&f, &query_token), &set) == DOE_RESOURCE_OK);
    doe_resource_buffer *dst = buffer(&f, &buffer_token, 512, WGPUBufferUsage_QueryResolve);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_write_timestamp(encoder, set, 7, DOE_TIMESTAMP_COMMAND) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_resolve_queries(encoder, set, 4, 4, dst, 256) == DOE_RESOURCE_OK);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_query_set_release(set);
    doe_resource_buffer_release(dst);
    assert(!query_token.releases && !buffer_token.releases);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_EXECUTION_FAILED) == DOE_RESOURCE_EXECUTION_FAILED);
    assert(query_token.releases == 1 && buffer_token.releases == 1);
    empty(&f);

    query_token = (native_token){0};
    buffer_token = (native_token){0};
    assert(doe_resource_query_set_create(&c, WGPUQueryType_Timestamp, UINT32_MAX,
        native(&f, &query_token), &set) == DOE_RESOURCE_OK);
    dst = buffer(&f, &buffer_token, 512, WGPUBufferUsage_QueryResolve);
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    size_t before = f.allocations;
    assert(doe_resource_encoder_resolve_queries(encoder, set, UINT32_MAX - 2, 4, dst, 0) == DOE_RESOURCE_RANGE);
    assert(f.allocations == before);
    doe_resource_encoder_release(encoder);
    doe_resource_query_set_release(set);
    doe_resource_buffer_release(dst);
    empty(&f);
}

static void test_external_texture(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token texture_token = {0}, view_token = {0};
    doe_resource_texture_desc desc = texture_desc();
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &texture_token), &texture) == DOE_RESOURCE_OK);
    doe_resource_view_desc view_desc = {0};
    doe_resource_view *view;
    assert(doe_resource_view_create(texture, &view_desc, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
    doe_resource_external_texture *external;
    assert(doe_resource_external_texture_create(&c, view, NULL, &external) == DOE_RESOURCE_OK);
    assert(doe_resource_external_texture_plane(external, 0) == view);
    doe_resource_layout_entry entry = {.binding = 3, .visibility = WGPUShaderStage_Fragment,
        .kind = DOE_BIND_EXTERNAL_TEXTURE};
    doe_resource_layout *layout;
    assert(doe_resource_layout_create(&c, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    doe_resource_binding binding = {.binding = 3, .kind = DOE_BIND_EXTERNAL_TEXTURE, .resource.external_texture = external};
    doe_resource_binding_limits binding_limits = {16, 16, 1024, 1024};
    doe_resource_bind_group *group;
    doe_resource_external_texture_expire(external);
    assert(doe_resource_bind_group_create(layout, &binding, 1, &binding_limits, &group) == DOE_RESOURCE_UNAVAILABLE);
    assert(doe_resource_external_texture_refresh(external) == DOE_RESOURCE_OK);
    assert(doe_resource_bind_group_create(layout, &binding, 1, &binding_limits, &group) == DOE_RESOURCE_OK);
    doe_resource_binding output;
    doe_resource_external_texture_expire(external);
    assert(doe_resource_bind_group_resolve(group, NULL, 0, &output, 1) == DOE_RESOURCE_UNAVAILABLE);
    assert(doe_resource_external_texture_refresh(external) == DOE_RESOURCE_OK);
    assert(doe_resource_bind_group_resolve(group, NULL, 0, &output, 1) == DOE_RESOURCE_OK);
    doe_resource_view_release(view);
    doe_resource_texture_release(texture);
    doe_resource_external_texture_release(external);
    doe_resource_layout_release(layout);
    assert(!texture_token.releases && !view_token.releases);
    doe_resource_bind_group_release(group);
    assert(texture_token.releases == 1 && view_token.releases == 1);
    empty(&f);
}

static void test_render_recording(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token texture_token = {0}, view_token = {0}, pipeline_token = {0};
    native_token vertex_token = {0}, index_token = {0}, query_token = {0};
    doe_resource_texture_desc desc = texture_desc();
    desc.usage = WGPUTextureUsage_RenderAttachment;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &texture_token), &texture) == DOE_RESOURCE_OK);
    doe_resource_view_desc view_desc = {0};
    doe_resource_view *view;
    assert(doe_resource_view_create(texture, &view_desc, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
    doe_resource_query_set *query;
    assert(doe_resource_query_set_create(&c, WGPUQueryType_Occlusion, 4, native(&f, &query_token), &query) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout *layout;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
    doe_resource_render_pipeline_desc pipeline_desc = {.color_format = desc.format, .samples = 1, .vertex_count = 1,
        .vertices = {{16, 12, WGPUVertexStepMode_Vertex}}};
    doe_resource_render_pipeline *pipeline;
    assert(doe_resource_render_pipeline_create(&c, layout, &pipeline_desc, native(&f, &pipeline_token), &pipeline) == DOE_RESOURCE_OK);
    doe_resource_buffer *vertices = buffer(&f, &vertex_token, 64, WGPUBufferUsage_Vertex);
    doe_resource_buffer *indices = buffer(&f, &index_token, 6, WGPUBufferUsage_Index);
    doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear,
        .color_store = WGPUStoreOp_Store, .occlusion_queries = query, .max_draw_count = 2};
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_set_pipeline(pass, pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_set_vertex(pass, 0, vertices, 0, UINT64_MAX) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_begin_occlusion(pass, 2) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_draw(pass, 3, 1, 0, 0) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_end_occlusion(pass) == DOE_RESOURCE_OK);
    doe_resource_render_state state = *doe_resource_render_pass_state(pass);
    state.blend[0] = 0.5;
    assert(doe_resource_render_pass_set_state(pass, &state) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_set_index(pass, indices, WGPUIndexFormat_Uint16, 0, 6) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_draw_indexed(pass, 3, 1, 0, 0, 0) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_render_pass_release(pass);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    size_t count;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    assert(count == 6 && records[0].kind == DOE_COMMAND_RENDER);
    assert(records[1].kind == DOE_COMMAND_BEGIN_OCCLUSION && records[3].kind == DOE_COMMAND_END_OCCLUSION);
    const doe_resource_render_packet *begin = doe_resource_render_record_borrow(records[0].value.render);
    const doe_resource_render_packet *first = doe_resource_render_record_borrow(records[2].value.render);
    const doe_resource_render_packet *last = doe_resource_render_record_borrow(records[4].value.render);
    assert(begin->first_in_pass && begin->boundary_only && !begin->last_in_pass);
    assert(!first->first_in_pass && !first->last_in_pass && first->occlusion_active && first->occlusion_index == 2);
    const doe_resource_render_packet *end = doe_resource_render_record_borrow(records[5].value.render);
    assert(!last->first_in_pass && !last->last_in_pass && last->indexed && !last->occlusion_active);
    assert(end->last_in_pass && end->boundary_only && !end->first_in_pass);
    assert(first->state.blend[0] == 0 && last->state.blend[0] == 0.5);
    doe_resource_view_release(view);
    doe_resource_texture_release(texture);
    doe_resource_query_set_release(query);
    doe_resource_render_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(layout);
    doe_resource_buffer_release(vertices);
    doe_resource_buffer_release(indices);
    assert(!texture_token.releases && !pipeline_token.releases && !vertex_token.releases && !query_token.releases);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(texture_token.releases == 1 && view_token.releases == 1 && pipeline_token.releases == 1);
    assert(vertex_token.releases == 1 && index_token.releases == 1 && query_token.releases == 1);
    empty(&f);
}

static void test_clear_and_abandoned_pass(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token texture_token = {0}, view_token = {0};
    doe_resource_texture_desc desc = texture_desc();
    desc.usage = WGPUTextureUsage_RenderAttachment;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    doe_resource_view *view;
    doe_resource_view_desc view_desc = {0};
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &texture_token), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &view_desc, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
    doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store};
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    doe_resource_render_pass_release(pass);
    assert(doe_resource_encoder_status(encoder) == DOE_RESOURCE_STATE);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_render_pass_release(pass);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    size_t count;
    const doe_resource_command *record = doe_resource_commands_borrow(commands, &count);
    const doe_resource_render_packet *packet = doe_resource_render_record_borrow(record[0].value.render);
    assert(count == 2 && packet->boundary_only && packet->first_in_pass && !packet->last_in_pass);
    packet = doe_resource_render_record_borrow(record[1].value.render);
    assert(packet->boundary_only && !packet->first_in_pass && packet->last_in_pass);
    doe_resource_commands_release(commands);
    doe_resource_encoder_release(encoder);
    doe_resource_view_release(view);
    doe_resource_texture_release(texture);
    empty(&f);
}

static void test_texture_blit(void)
{
    fixture f = fresh(), other = fresh();
    doe_resource_config c = config(&f);
    native_token source_token = {0}, destination_token = {0};
    doe_resource_texture_desc desc = texture_desc();
    desc.depth_or_layers = 4;
    desc.mip_levels = 2;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *source, *destination;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &source_token), &source) == DOE_RESOURCE_OK);
    desc.format = WGPUTextureFormat_RGBA8UnormSrgb;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &destination_token), &destination) == DOE_RESOURCE_OK);
    doe_resource_texture_blit copy = {.source = {.texture = source}, .destination = {.texture = destination},
        .width = 8, .height = 8, .depth_or_layers = 2};
    doe_resource_texture_blit normalized = {0};
    assert(doe_resource_texture_blit_validate(&f, &copy, DOE_RESOURCE_COPY_WEBGPU, &normalized) == DOE_RESOURCE_OK);
    assert(normalized.source.aspect == WGPUTextureAspect_All && normalized.destination.aspect == WGPUTextureAspect_All);
    assert(doe_resource_texture_blit_validate(&other, &copy, DOE_RESOURCE_COPY_WEBGPU, &normalized) == DOE_RESOURCE_DEVICE_MISMATCH);
    copy.destination.texture = source;
    assert(doe_resource_texture_blit_validate(&f, &copy, DOE_RESOURCE_COPY_WEBGPU, &normalized) == DOE_RESOURCE_ALIAS);
    copy.destination.origin[2] = 2;
    assert(doe_resource_texture_blit_validate(&f, &copy, DOE_RESOURCE_COPY_WEBGPU, &normalized) == DOE_RESOURCE_OK);
    copy.destination.origin[2] = 3;
    assert(doe_resource_texture_blit_validate(&f, &copy, DOE_RESOURCE_COPY_WEBGPU, &normalized) == DOE_RESOURCE_RANGE);
    copy.destination.origin[2] = 0;
    copy.destination.mip = 1;
    copy.width = copy.height = 4;
    assert(doe_resource_texture_blit_validate(&f, &copy, DOE_RESOURCE_COPY_WEBGPU, &normalized) == DOE_RESOURCE_OK);
    copy.destination.texture = destination;
    copy.source.aspect = WGPUTextureAspect_DepthOnly;
    assert(doe_resource_texture_blit_validate(&f, &copy, DOE_RESOURCE_COPY_WEBGPU, &normalized) == DOE_RESOURCE_ASPECT);
    assert(normalized.source.aspect == WGPUTextureAspect_All);
    copy.source.aspect = WGPUTextureAspect_All;
    doe_resource_encoder *encoder;
    doe_resource_commands *commands;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_blit_texture(encoder, &copy, DOE_RESOURCE_COPY_WEBGPU) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_texture_release(source);
    doe_resource_texture_release(destination);
    assert(!source_token.releases && !destination_token.releases);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(source_token.releases == 1 && destination_token.releases == 1);
    empty(&f);
}

static void test_pass_rejection_and_unbind(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_encoder *encoder;
    doe_resource_compute_pass *pass;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_compute(encoder, &pass) == DOE_RESOURCE_OK);
    doe_resource_layout *layout;
    doe_resource_bind_group *group;
    doe_resource_binding_limits limits = {16, 16, 1024, 1024};
    assert(doe_resource_layout_create(&c, NULL, 0, false, &layout) == DOE_RESOURCE_OK);
    assert(doe_resource_bind_group_create(layout, NULL, 0, &limits, &group) == DOE_RESOURCE_OK);
    doe_resource_dispatch_group binding = {.group = group};
    assert(doe_resource_compute_pass_set_group(pass, 0, &binding) == DOE_RESOURCE_OK);
    doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    size_t live = f.live;
    binding.group = NULL;
    assert(doe_resource_compute_pass_set_group(pass, 0, &binding) == DOE_RESOURCE_OK);
    assert(f.live == live - 2);
    assert(doe_resource_compute_pass_set_group(pass, 0, NULL) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_invalidate(encoder, DOE_RESOURCE_PENDING) == DOE_RESOURCE_INVALID_ARGUMENT);
    assert(doe_resource_encoder_status(encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_invalidate(encoder, DOE_RESOURCE_UNSUPPORTED) == DOE_RESOURCE_UNSUPPORTED);
    assert(doe_resource_encoder_invalidate(encoder, DOE_RESOURCE_RANGE) == DOE_RESOURCE_UNSUPPORTED);
    doe_resource_commands *commands = NULL;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_UNSUPPORTED && !commands);
    doe_resource_encoder_release(encoder);
    doe_resource_compute_pass_release(pass);
    empty(&f);
}

int main(void)
{
    test_pass_rejection_and_unbind();
    test_texture_blit();
    test_shader_and_compute_pass();
    test_query_ownership();
    test_external_texture();
    test_render_recording();
    test_clear_and_abandoned_pass();
    puts("extended resource owner: 7 host suites passed; no render/query GPU execution claimed");
    return 0;
}
