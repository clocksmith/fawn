/* SPDX-License-Identifier: MIT */
#include "fixture.h"

static doe_resource_render_pipeline *make_pipeline(fixture *f, native_token *token, bool vertex)
{
    doe_resource_config c = config(f);
    doe_resource_pipeline_layout *layout;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
    doe_resource_render_pipeline_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm,
        .samples = 1, .vertex_count = vertex ? 1 : 0, .vertices = {{16, 12, WGPUVertexStepMode_Vertex}}};
    doe_resource_render_pipeline *pipeline;
    assert(doe_resource_render_pipeline_create(&c, layout, &desc, native(f, token), &pipeline) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout_release(layout);
    return pipeline;
}

static doe_resource_bundle_encoder *make_encoder(fixture *f)
{
    doe_resource_config c = config(f);
    doe_resource_render_bundle_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 1};
    doe_resource_bundle_encoder *encoder;
    assert(doe_resource_bundle_encoder_create(&c, &desc, &encoder) == DOE_RESOURCE_OK);
    return encoder;
}

static void test_bundle_replay_lifetimes(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token pipeline_token = {0}, pass_pipeline_token = {0}, vertex_token = {0};
    native_token image_token = {0}, view_token = {0};
    doe_resource_render_pipeline *pipeline = make_pipeline(&f, &pipeline_token, true);
    doe_resource_render_pipeline *caller_pipeline = make_pipeline(&f, &pass_pipeline_token, false);
    doe_resource_buffer *vertices = buffer(&f, &vertex_token, 64, WGPUBufferUsage_Vertex);
    doe_resource_bundle_encoder *bundle_encoder = make_encoder(&f);
    assert(doe_resource_bundle_encoder_set_pipeline(bundle_encoder, pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_vertex(bundle_encoder, 0, vertices, 0, 64) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_draw(bundle_encoder, false, 3, 1, 0, 0, 0) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_vertex(bundle_encoder, 0, vertices, 16, 48) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_draw(bundle_encoder, false, 2, 1, 0, 0, 0) == DOE_RESOURCE_OK);
    doe_resource_render_bundle *bundle;
    assert(doe_resource_bundle_encoder_finish(bundle_encoder, &bundle) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_draw(bundle_encoder, false, 1, 1, 0, 0, 0) == DOE_RESOURCE_STATE);
    doe_resource_bundle_encoder_retain(bundle_encoder);
    doe_resource_bundle_encoder_release(bundle_encoder);
    doe_resource_bundle_encoder_release(bundle_encoder);
    doe_resource_render_pipeline_release(pipeline);
    doe_resource_buffer_release(vertices);
    assert(!pipeline_token.releases && !vertex_token.releases);
    doe_resource_render_bundle_retain(bundle);
    doe_resource_render_bundle_release(bundle);

    doe_resource_texture_desc desc = texture_desc();
    desc.usage = WGPUTextureUsage_RenderAttachment;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    doe_resource_view *view;
    doe_resource_view_desc view_desc = {0};
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &image_token), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &view_desc, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
    doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear,
        .color_store = WGPUStoreOp_Store, .max_draw_count = 5, .has_max_draw_count = true};
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_set_pipeline(pass, caller_pipeline) == DOE_RESOURCE_OK);
    doe_resource_render_state state = *doe_resource_render_pass_state(pass);
    state.blend[0] = 0.25;
    assert(doe_resource_render_pass_set_state(pass, &state) == DOE_RESOURCE_OK);
    doe_resource_render_bundle *replays[] = {bundle, bundle};
    assert(doe_resource_render_pass_execute_bundles(pass, replays, 2) == DOE_RESOURCE_OK);
    doe_resource_render_bundle_release(bundle);
    assert(!pipeline_token.releases && !vertex_token.releases);
    /* Dynamic state survives; pipeline bindings must be established again. */
    assert(doe_resource_render_pass_state(pass)->blend[0] == 0.25);
    assert(doe_resource_render_pass_set_pipeline(pass, caller_pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_draw(pass, 3, 1, 0, 0) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_render_pass_release(pass);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    size_t count;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    assert(count == 7);
    for (size_t i = 1; i < 5; i++) {
        const doe_resource_render_packet *packet = doe_resource_render_record_borrow(records[i].value.render);
        assert(packet->pipeline == pipeline && packet->state.blend[0] == 0.25);
        assert(packet->vertices[0].offset == (i % 2 ? 0 : 16));
        assert(!packet->boundary_only && !packet->first_in_pass && !packet->last_in_pass);
    }
    assert(doe_resource_render_record_borrow(records[5].value.render)->pipeline == caller_pipeline);
    doe_resource_render_pipeline_release(caller_pipeline);
    doe_resource_view_release(view);
    doe_resource_texture_release(texture);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(!pipeline_token.releases && !vertex_token.releases && !image_token.releases);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(pipeline_token.releases == 1 && pass_pipeline_token.releases == 1 && vertex_token.releases == 1);
    assert(image_token.releases == 1 && view_token.releases == 1);
    empty(&f);
}

static void test_bundle_failed_publication(void)
{
    fixture f = fresh();
    native_token pipeline_token = {0}, vertex_token = {0};
    doe_resource_render_pipeline *pipeline = make_pipeline(&f, &pipeline_token, true);
    doe_resource_buffer *vertices = buffer(&f, &vertex_token, 64, WGPUBufferUsage_Vertex);
    doe_resource_bundle_encoder *encoder = make_encoder(&f);
    assert(doe_resource_bundle_encoder_set_pipeline(encoder, pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_draw(encoder, false, 3, 1, 0, 0, 0) == DOE_RESOURCE_UNAVAILABLE);
    doe_resource_render_bundle *bundle = NULL;
    assert(doe_resource_bundle_encoder_finish(encoder, &bundle) == DOE_RESOURCE_UNAVAILABLE && !bundle);
    doe_resource_bundle_encoder_release(encoder);

    encoder = make_encoder(&f);
    assert(doe_resource_bundle_encoder_set_pipeline(encoder, pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_vertex(encoder, 0, vertices, 0, 64) == DOE_RESOURCE_OK);
    f.fail_at = f.allocations;
    assert(doe_resource_bundle_encoder_draw(encoder, false, 3, 1, 0, 0, 0) == DOE_RESOURCE_OUT_OF_MEMORY);
    assert(doe_resource_bundle_encoder_finish(encoder, &bundle) == DOE_RESOURCE_OUT_OF_MEMORY && !bundle);
    doe_resource_bundle_encoder_release(encoder);
    f.fail_at = SIZE_MAX;

    encoder = make_encoder(&f);
    assert(doe_resource_bundle_encoder_set_pipeline(encoder, pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_vertex(encoder, 0, vertices, 0, 64) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_draw(encoder, false, 3, 1, 0, 0, 0) == DOE_RESOURCE_OK);
    f.fail_at = f.allocations;
    assert(doe_resource_bundle_encoder_finish(encoder, &bundle) == DOE_RESOURCE_OUT_OF_MEMORY && !bundle);
    doe_resource_render_pipeline_release(pipeline);
    doe_resource_buffer_release(vertices);
    assert(!pipeline_token.releases && !vertex_token.releases);
    doe_resource_bundle_encoder_release(encoder);
    assert(pipeline_token.releases == 1 && vertex_token.releases == 1);
    empty(&f);
}

static void test_bundle_budget(void)
{
    fixture f = fresh();
    doe_resource_bundle_encoder *encoder = make_encoder(&f);
    for (size_t i = 0; i < 4096; i++)
        assert(doe_resource_bundle_encoder_set_group(encoder, 0, NULL) == DOE_RESOURCE_OK);
    doe_resource_render_bundle *bundle;
    assert(doe_resource_bundle_encoder_finish(encoder, &bundle) == DOE_RESOURCE_OK);
    doe_resource_bundle_encoder_release(encoder);
    doe_resource_render_bundle_release(bundle);
    empty(&f);
    encoder = make_encoder(&f);
    for (size_t i = 0; i < 4096; i++)
        assert(doe_resource_bundle_encoder_set_group(encoder, 0, NULL) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_group(encoder, 0, NULL) == DOE_RESOURCE_RANGE);
    bundle = NULL;
    assert(doe_resource_bundle_encoder_finish(encoder, &bundle) == DOE_RESOURCE_RANGE && !bundle);
    doe_resource_bundle_encoder_release(encoder);
    empty(&f);
}

static void test_bundle_replay_rejection(void)
{
    for (unsigned scenario = 0; scenario < 3; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token pipeline_token = {0}, vertex_token = {0}, image_token = {0}, view_token = {0};
        doe_resource_render_pipeline *pipeline = make_pipeline(&f, &pipeline_token, true);
        doe_resource_buffer *vertices = buffer(&f, &vertex_token, 64, WGPUBufferUsage_Vertex);
        doe_resource_bundle_encoder *builder = make_encoder(&f);
        assert(doe_resource_bundle_encoder_set_pipeline(builder, pipeline) == DOE_RESOURCE_OK);
        assert(doe_resource_bundle_encoder_set_vertex(builder, 0, vertices, 0, 64) == DOE_RESOURCE_OK);
        assert(doe_resource_bundle_encoder_draw(builder, false, 3, 1, 0, 0, 0) == DOE_RESOURCE_OK);
        doe_resource_render_bundle *bundle;
        assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OK);
        doe_resource_bundle_encoder_release(builder);
        doe_resource_texture_desc desc = texture_desc();
        desc.usage = WGPUTextureUsage_RenderAttachment;
        if (scenario == 0) desc.format = WGPUTextureFormat_RGBA16Float;
        doe_resource_texture_limits limits = texture_limits();
        doe_resource_texture *texture;
        doe_resource_view *view;
        doe_resource_view_desc view_desc = {0};
        assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &image_token), &texture) == DOE_RESOURCE_OK);
        assert(doe_resource_view_create(texture, &view_desc, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
        doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store};
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
        if (scenario == 1) doe_resource_buffer_destroy(vertices);
        if (scenario == 2) f.fail_at = f.allocations;
        doe_resource_status expected = scenario == 0 ? DOE_RESOURCE_LAYOUT :
            scenario == 1 ? DOE_RESOURCE_UNAVAILABLE : DOE_RESOURCE_OUT_OF_MEMORY;
        assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == expected);
        doe_resource_commands *commands = NULL;
        assert(doe_resource_encoder_finish(encoder, &commands) == expected && !commands);
        doe_resource_render_pass_release(pass);
        doe_resource_encoder_release(encoder);
        doe_resource_render_bundle_release(bundle);
        doe_resource_render_pipeline_release(pipeline);
        doe_resource_buffer_release(vertices);
        doe_resource_view_release(view);
        doe_resource_texture_release(texture);
        assert(pipeline_token.releases == 1 && vertex_token.releases == 1);
        assert(image_token.releases == 1 && view_token.releases == 1);
        empty(&f);
    }
}

/* A destroyed sampled texture in an executed bundle is an error at submit,
 * not at executeBundles or finish, matching a direct setBindGroup. */
static void test_bundle_destroyed_texture_rejects_at_submit(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token sampled_token = {0}, sampled_view_token = {0}, image_token = {0}, view_token = {0};
    doe_resource_texture_desc desc = texture_desc();
    desc.usage = WGPUTextureUsage_TextureBinding;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_view_desc view_desc = {0};
    doe_resource_texture *sampled;
    doe_resource_view *sampled_view;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &sampled_token), &sampled) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(sampled, &view_desc, native(&f, &sampled_view_token), &sampled_view) == DOE_RESOURCE_OK);
    doe_resource_layout_entry entry = {.kind = DOE_BIND_TEXTURE, .visibility = WGPUShaderStage_Fragment,
        .sample_type = WGPUTextureSampleType_Float, .view_dimension = WGPUTextureViewDimension_2D};
    doe_resource_layout *layout;
    assert(doe_resource_layout_create(&c, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    doe_resource_binding binding = {.kind = DOE_BIND_TEXTURE, .resource.view = sampled_view};
    doe_resource_binding_limits binding_limits = {16, 16, 256, 256};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(layout, &binding, 1, &binding_limits, &group) == DOE_RESOURCE_OK);
    doe_resource_dispatch_group input = {.group = group};
    doe_resource_bundle_encoder *builder = make_encoder(&f);
    assert(doe_resource_bundle_encoder_set_group(builder, 0, &input) == DOE_RESOURCE_OK);
    doe_resource_render_bundle *bundle;
    assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OK);
    doe_resource_bundle_encoder_release(builder);
    doe_resource_texture_destroy(sampled);

    desc.usage = WGPUTextureUsage_RenderAttachment;
    doe_resource_texture *texture;
    doe_resource_view *view;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &image_token), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &view_desc, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
    doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store};
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK && commands);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_UNAVAILABLE);
    doe_resource_commands_release(commands);
    doe_resource_render_pass_release(pass);
    doe_resource_encoder_release(encoder);
    doe_resource_render_bundle_release(bundle);
    doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    doe_resource_view_release(view);
    doe_resource_texture_release(texture);
    doe_resource_view_release(sampled_view);
    doe_resource_texture_release(sampled);
    assert(sampled_token.releases == 1 && sampled_view_token.releases == 1);
    assert(image_token.releases == 1 && view_token.releases == 1);
    empty(&f);
}

static void test_bundle_state_reset(void)
{
    for (unsigned nonempty = 0; nonempty < 2; nonempty++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token pipeline_token = {0}, image_token = {0}, view_token = {0};
        doe_resource_render_pipeline *pipeline = make_pipeline(&f, &pipeline_token, false);
        doe_resource_bundle_encoder *builder = make_encoder(&f);
        assert(doe_resource_bundle_encoder_set_pipeline(builder, pipeline) == DOE_RESOURCE_OK);
        assert(doe_resource_bundle_encoder_draw(builder, false, 3, 1, 0, 0, 0) == DOE_RESOURCE_OK);
        doe_resource_render_bundle *bundle;
        assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OK);
        doe_resource_bundle_encoder_release(builder);
        doe_resource_texture_desc desc = texture_desc();
        desc.usage = WGPUTextureUsage_RenderAttachment;
        doe_resource_texture_limits limits = texture_limits();
        doe_resource_texture *texture;
        doe_resource_view *view;
        doe_resource_view_desc view_desc = {0};
        assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &image_token), &texture) == DOE_RESOURCE_OK);
        assert(doe_resource_view_create(texture, &view_desc, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
        doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store};
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
        assert(doe_resource_render_pass_set_pipeline(pass, pipeline) == DOE_RESOURCE_OK);
        doe_resource_render_state state = *doe_resource_render_pass_state(pass);
        state.blend[1] = 0.5;
        state.stencil_reference = 7;
        assert(doe_resource_render_pass_set_state(pass, &state) == DOE_RESOURCE_OK);
        assert(doe_resource_render_pass_execute_bundles(pass, nonempty ? &bundle : NULL, nonempty) == DOE_RESOURCE_OK);
        assert(doe_resource_render_pass_state(pass)->blend[1] == 0.5 &&
            doe_resource_render_pass_state(pass)->stencil_reference == 7);
        assert(doe_resource_render_pass_draw(pass, 3, 1, 0, 0) == DOE_RESOURCE_UNAVAILABLE);
        doe_resource_render_pass_release(pass);
        doe_resource_encoder_release(encoder);
        doe_resource_render_bundle_release(bundle);
        doe_resource_render_pipeline_release(pipeline);
        doe_resource_view_release(view);
        doe_resource_texture_release(texture);
        empty(&f);
    }
}

static void test_empty_bundle_read_only_contract(void)
{
    for (unsigned scenario = 0; scenario < 3; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token image_tokens[2] = {{0}, {0}}, view_tokens[2] = {{0}, {0}};
        doe_resource_texture *images[2];
        doe_resource_view *views[2];
        doe_resource_texture_limits limits = texture_limits();
        doe_resource_view_desc view_desc = {0};
        for (size_t i = 0; i < 2; i++) {
            doe_resource_texture_desc desc = texture_desc();
            desc.usage = WGPUTextureUsage_RenderAttachment;
            if (i) desc.format = WGPUTextureFormat_Depth24PlusStencil8;
            assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &image_tokens[i]), &images[i]) == DOE_RESOURCE_OK);
            assert(doe_resource_view_create(images[i], &view_desc, native(&f, &view_tokens[i]), &views[i]) == DOE_RESOURCE_OK);
        }
        doe_resource_render_bundle_desc signature = {.color_format = WGPUTextureFormat_RGBA8Unorm,
            .depth_stencil_format = WGPUTextureFormat_Depth24PlusStencil8, .samples = 1,
            .depth_read_only = scenario != 0, .stencil_read_only = scenario != 1};
        doe_resource_bundle_encoder *builder;
        doe_resource_render_bundle *bundle;
        assert(doe_resource_bundle_encoder_create(&c, &signature, &builder) == DOE_RESOURCE_OK);
        assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OK);
        doe_resource_bundle_encoder_release(builder);
        doe_resource_render_targets targets = {.color = views[0], .depth_stencil = views[1],
            .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store,
            .depth_read_only = true, .stencil_read_only = true};
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
        doe_resource_status status = doe_resource_render_pass_execute_bundles(pass, &bundle, 1);
        assert(status == (scenario == 2 ? DOE_RESOURCE_OK : DOE_RESOURCE_LAYOUT));
        if (scenario == 2) assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
        doe_resource_render_pass_release(pass);
        doe_resource_encoder_release(encoder);
        doe_resource_render_bundle_release(bundle);
        for (size_t i = 0; i < 2; i++) {
            doe_resource_view_release(views[i]);
            doe_resource_texture_release(images[i]);
            assert(image_tokens[i].releases == 1 && view_tokens[i].releases == 1);
        }
        empty(&f);
    }
}

int main(void)
{
    test_bundle_replay_lifetimes();
    test_bundle_failed_publication();
    test_bundle_budget();
    test_bundle_replay_rejection();
    test_bundle_destroyed_texture_rejects_at_submit();
    test_bundle_state_reset();
    test_empty_bundle_read_only_contract();
    puts("render bundle owner: 7 host suites passed; no render GPU execution claimed");
    return 0;
}
