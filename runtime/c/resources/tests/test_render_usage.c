/* SPDX-License-Identifier: MIT */
#include "fixture.h"

static doe_resource_view *make_view(fixture *f, native_token *image_token, native_token *view_token,
    WGPUTextureFormat format)
{
    doe_resource_config c = config(f);
    doe_resource_texture_desc desc = texture_desc();
    desc.format = format;
    desc.usage |= WGPUTextureUsage_RenderAttachment;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *image;
    doe_resource_view *view;
    doe_resource_view_desc view_desc = {0};
    assert(doe_resource_texture_create(&c, &desc, &limits, native(f, image_token), &image) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(image, &view_desc, native(f, view_token), &view) == DOE_RESOURCE_OK);
    doe_resource_texture_release(image);
    return view;
}

static doe_resource_render_pass *begin(fixture *f, doe_resource_view *view, doe_resource_encoder **encoder)
{
    doe_resource_config c = config(f);
    doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear,
        .color_store = WGPUStoreOp_Store};
    doe_resource_render_pass *pass;
    assert(doe_resource_encoder_create(&c, encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_render(*encoder, &targets, &pass) == DOE_RESOURCE_OK);
    return pass;
}

static doe_resource_commands *finish(doe_resource_encoder *encoder, doe_resource_render_pass *pass)
{
    assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_render_pass_release(pass);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    return commands;
}

static doe_resource_bind_group *make_group(fixture *f, const doe_resource_layout_entry *entries,
    const doe_resource_binding *bindings, size_t count, doe_resource_layout **layout)
{
    doe_resource_config c = config(f);
    assert(doe_resource_layout_create(&c, entries, count, false, layout) == DOE_RESOURCE_OK);
    doe_resource_binding_limits limits = {16, 16, 256, 256};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(*layout, bindings, count, &limits, &group) == DOE_RESOURCE_OK);
    return group;
}

static doe_resource_bind_group *buffer_group(fixture *f, doe_resource_buffer *storage,
    doe_resource_layout **layout)
{
    doe_resource_layout_entry entry = {.kind = DOE_BIND_BUFFER, .visibility = WGPUShaderStage_Fragment,
        .buffer_type = WGPUBufferBindingType_Storage};
    doe_resource_binding binding = {.kind = DOE_BIND_BUFFER, .resource.buffer = {storage, 0, 64}};
    return make_group(f, &entry, &binding, 1, layout);
}

static doe_resource_bundle_encoder *bundle_encoder(fixture *f)
{
    doe_resource_config c = config(f);
    doe_resource_render_bundle_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 1};
    doe_resource_bundle_encoder *encoder;
    assert(doe_resource_bundle_encoder_create(&c, &desc, &encoder) == DOE_RESOURCE_OK);
    return encoder;
}

static void test_overwritten_setters(void)
{
    for (unsigned scenario = 0; scenario < 4; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token image_token = {0}, view_token = {0}, tokens[2] = {{0}};
        doe_resource_view *view = make_view(&f, &image_token, &view_token, WGPUTextureFormat_RGBA8Unorm);
        doe_resource_buffer_desc desc = {64, WGPUBufferUsage_Vertex | WGPUBufferUsage_Index, scenario == 2};
        doe_resource_buffer *first;
        assert(doe_resource_buffer_create(&c, &desc, native(&f, &tokens[0]), &first) == DOE_RESOURCE_OK);
        doe_resource_buffer *last = buffer(&f, &tokens[1], 64, desc.usage);
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass = begin(&f, view, &encoder);
        assert(doe_resource_render_pass_set_vertex(pass, 7, first, 0, 64) == DOE_RESOURCE_OK);
        assert(doe_resource_render_pass_set_vertex(pass, 7, last, 0, 64) == DOE_RESOURCE_OK);
        assert(doe_resource_render_pass_set_index(pass, first, WGPUIndexFormat_Uint16, 0, 64) == DOE_RESOURCE_OK);
        assert(doe_resource_render_pass_set_index(pass, last, WGPUIndexFormat_Uint16, 0, 64) == DOE_RESOURCE_OK);
        /* Clearing all logical state still cannot erase historical usage. */
        assert(doe_resource_render_pass_execute_bundles(pass, NULL, 0) == DOE_RESOURCE_OK);
        doe_resource_commands *commands = finish(encoder, pass);
        if (scenario == 1) doe_resource_buffer_destroy(first);
        doe_resource_buffer_release(first);
        doe_resource_buffer_release(last);
        doe_resource_view_release(view);
        assert(!tokens[0].releases && !tokens[1].releases);
        doe_resource_status expected = scenario == 1 ? DOE_RESOURCE_UNAVAILABLE :
            scenario == 2 ? DOE_RESOURCE_STATE : DOE_RESOURCE_OK;
        assert(doe_resource_commands_begin_submit(commands) == expected);
        if (expected == DOE_RESOURCE_OK) {
            doe_resource_commands_release(commands);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
            assert(!tokens[0].releases && !tokens[1].releases);
            assert(doe_resource_commands_settle(commands, scenario == 3 ? DOE_RESOURCE_EXECUTION_FAILED : DOE_RESOURCE_OK) ==
                (scenario == 3 ? DOE_RESOURCE_EXECUTION_FAILED : DOE_RESOURCE_OK));
        } else doe_resource_commands_release(commands);
        assert(tokens[0].releases == 1 && tokens[1].releases == 1);
        assert(image_token.releases == 1 && view_token.releases == 1);
        empty(&f);
    }
}

static void test_history_conflicts(void)
{
    for (unsigned scenario = 0; scenario < 4; scenario++) {
        fixture f = fresh();
        native_token image_token = {0}, view_token = {0}, token = {0};
        doe_resource_view *view = make_view(&f, &image_token, &view_token, WGPUTextureFormat_RGBA8Unorm);
        doe_resource_buffer *storage = buffer(&f, &token, 256,
            WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex | WGPUBufferUsage_Index);
        doe_resource_layout *layout;
        doe_resource_bind_group *group = buffer_group(&f, storage, &layout);
        doe_resource_dispatch_group input = {group, NULL, 0};
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass = begin(&f, view, &encoder);
        if (scenario < 2) {
            assert(doe_resource_render_pass_set_group(pass, 3, &input) == DOE_RESOURCE_OK);
            assert(doe_resource_render_pass_set_group(pass, 3, NULL) == DOE_RESOURCE_OK);
            size_t allocations = f.allocations;
            doe_resource_status status = scenario == 0 ?
                doe_resource_render_pass_set_vertex(pass, 7, storage, 128, 64) :
                doe_resource_render_pass_set_index(pass, storage, WGPUIndexFormat_Uint32, 128, 64);
            assert(status == DOE_RESOURCE_USAGE && allocations == f.allocations);
        } else {
            assert(doe_resource_render_pass_set_vertex(pass, 7, storage, 128, 64) == DOE_RESOURCE_OK);
            assert(doe_resource_render_pass_execute_bundles(pass, NULL, 0) == DOE_RESOURCE_OK);
            if (scenario == 2) assert(doe_resource_render_pass_set_group(pass, 0, &input) == DOE_RESOURCE_USAGE);
            else {
                doe_resource_bundle_encoder *builder = bundle_encoder(&f);
                assert(doe_resource_bundle_encoder_set_group(builder, 0, &input) == DOE_RESOURCE_OK);
                assert(doe_resource_bundle_encoder_set_group(builder, 0, NULL) == DOE_RESOURCE_OK);
                doe_resource_render_bundle *bundle;
                assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OK);
                doe_resource_bundle_encoder_release(builder);
                /* Even a no-draw bundle carries usage from unused setters. */
                assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_USAGE);
                doe_resource_render_bundle_release(bundle);
            }
        }
        doe_resource_commands *sentinel = (void *)(uintptr_t)1;
        assert(doe_resource_encoder_finish(encoder, &sentinel) == DOE_RESOURCE_USAGE && sentinel == (void *)(uintptr_t)1);
        doe_resource_render_pass_release(pass);
        doe_resource_encoder_release(encoder);
        doe_resource_bind_group_release(group);
        doe_resource_layout_release(layout);
        doe_resource_buffer_release(storage);
        doe_resource_view_release(view);
        assert(token.releases == 1);
        empty(&f);
    }
}

static void test_attachment_scopes(void)
{
    for (unsigned scenario = 0; scenario < 5; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token image_tokens[2] = {{0}}, view_tokens[3] = {{0}};
        doe_resource_view *color = make_view(&f, &image_tokens[0], &view_tokens[0], WGPUTextureFormat_RGBA8Unorm);
        doe_resource_view *depth = NULL, *sample = color;
        doe_resource_render_targets targets = {.color = color, .color_load = WGPULoadOp_Clear,
            .color_store = WGPUStoreOp_Store};
        if (scenario) {
            depth = make_view(&f, &image_tokens[1], &view_tokens[1], WGPUTextureFormat_Depth24PlusStencil8);
            targets.depth_stencil = depth;
            targets.depth_read_only = scenario != 1;
            targets.stencil_read_only = scenario == 4;
            if (!targets.depth_read_only) {
                targets.depth_load = WGPULoadOp_Clear;
                targets.depth_store = WGPUStoreOp_Store;
            }
            if (!targets.stencil_read_only) {
                targets.stencil_load = WGPULoadOp_Clear;
                targets.stencil_store = WGPUStoreOp_Store;
            }
            doe_resource_view_desc desc = {.aspect = scenario >= 3 ? WGPUTextureAspect_StencilOnly : WGPUTextureAspect_DepthOnly};
            assert(doe_resource_view_create(doe_resource_view_texture(depth), &desc,
                native(&f, &view_tokens[2]), &sample) == DOE_RESOURCE_OK);
        }
        doe_resource_layout_entry entry = {.kind = DOE_BIND_TEXTURE, .visibility = WGPUShaderStage_Fragment,
            .sample_type = scenario >= 3 ? WGPUTextureSampleType_Uint :
                scenario ? WGPUTextureSampleType_Depth : WGPUTextureSampleType_Float};
        doe_resource_binding binding = {.kind = DOE_BIND_TEXTURE, .resource.view = sample};
        doe_resource_layout *layout;
        doe_resource_bind_group *group = make_group(&f, &entry, &binding, 1, &layout);
        doe_resource_dispatch_group input = {group, NULL, 0};
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
        doe_resource_status expected = scenario == 2 || scenario == 4 ? DOE_RESOURCE_OK : DOE_RESOURCE_USAGE;
        assert(doe_resource_render_pass_set_group(pass, 0, &input) == expected);
        if (expected == DOE_RESOURCE_OK) {
            doe_resource_commands *commands = finish(encoder, pass);
            assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
            doe_resource_commands_release(commands);
        } else {
            doe_resource_render_pass_release(pass);
            doe_resource_encoder_release(encoder);
        }
        doe_resource_bind_group_release(group);
        doe_resource_layout_release(layout);
        if (scenario) doe_resource_view_release(sample);
        doe_resource_view_release(depth);
        doe_resource_view_release(color);
        empty(&f);
    }
}

static void test_draw_aliases(void)
{
    for (unsigned scenario = 0; scenario < 3; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token image_token = {0}, view_token = {0}, storage_token = {0}, pipeline_token = {0};
        doe_resource_view *view = make_view(&f, &image_token, &view_token, WGPUTextureFormat_RGBA8Unorm);
        doe_resource_buffer *storage = buffer(&f, &storage_token, 256, WGPUBufferUsage_Storage);
        doe_resource_layout_entry entries[2] = {
            {.binding = 0, .kind = DOE_BIND_BUFFER, .visibility = WGPUShaderStage_Fragment, .buffer_type = WGPUBufferBindingType_Storage},
            {.binding = 1, .kind = DOE_BIND_BUFFER, .visibility = WGPUShaderStage_Fragment, .buffer_type = WGPUBufferBindingType_Storage}
        };
        doe_resource_binding bindings[2] = {
            {.binding = 0, .kind = DOE_BIND_BUFFER, .resource.buffer = {storage, 0, 64}},
            {.binding = 1, .kind = DOE_BIND_BUFFER, .resource.buffer = {storage, scenario == 1 ? 64 : 0, 64}}
        };
        size_t count = scenario == 2 ? 1 : 2;
        doe_resource_layout *layout;
        doe_resource_bind_group *group = make_group(&f, entries, bindings, count, &layout);
        doe_resource_pipeline_layout *pipeline_layout;
        assert(doe_resource_pipeline_layout_create(&c, &layout, 1, 0, &pipeline_layout) == DOE_RESOURCE_OK);
        doe_resource_render_pipeline_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 1};
        doe_resource_render_pipeline *pipeline;
        assert(doe_resource_render_pipeline_create(&c, pipeline_layout, &desc,
            native(&f, &pipeline_token), &pipeline) == DOE_RESOURCE_OK);
        doe_resource_bundle_encoder *builder = bundle_encoder(&f);
        doe_resource_dispatch_group input = {group, NULL, 0};
        assert(doe_resource_bundle_encoder_set_pipeline(builder, pipeline) == DOE_RESOURCE_OK);
        assert(doe_resource_bundle_encoder_set_group(builder, 0, &input) == DOE_RESOURCE_OK);
        doe_resource_status expected = scenario == 0 ? DOE_RESOURCE_ALIAS : DOE_RESOURCE_OK;
        assert(doe_resource_bundle_encoder_draw(builder, false, 3, 1, 0, 0, 0) == expected);
        if (expected == DOE_RESOURCE_OK) {
            /* Same writable resources may be used by successive draws/bundles. */
            assert(doe_resource_bundle_encoder_draw(builder, false, 3, 1, 0, 0, 0) == DOE_RESOURCE_OK);
            doe_resource_render_bundle *bundle;
            assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OK);
            doe_resource_encoder *encoder;
            doe_resource_render_pass *pass = begin(&f, view, &encoder);
            doe_resource_render_bundle *replays[] = {bundle, bundle};
            assert(doe_resource_render_pass_execute_bundles(pass, replays, 2) == DOE_RESOURCE_OK);
            doe_resource_render_bundle_release(bundle);
            doe_resource_commands *commands = finish(encoder, pass);
            assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
            doe_resource_commands_release(commands);
        }
        doe_resource_bundle_encoder_release(builder);
        doe_resource_render_pipeline_release(pipeline);
        doe_resource_pipeline_layout_release(pipeline_layout);
        doe_resource_bind_group_release(group);
        doe_resource_layout_release(layout);
        doe_resource_buffer_release(storage);
        doe_resource_view_release(view);
        empty(&f);
    }
}

static void test_unused_external_expiry(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token image_tokens[2] = {{0}}, view_tokens[2] = {{0}};
    doe_resource_view *color = make_view(&f, &image_tokens[0], &view_tokens[0], WGPUTextureFormat_RGBA8Unorm);
    doe_resource_view *plane = make_view(&f, &image_tokens[1], &view_tokens[1], WGPUTextureFormat_RGBA8Unorm);
    doe_resource_external_texture *external;
    assert(doe_resource_external_texture_create(&c, plane, NULL, &external) == DOE_RESOURCE_OK);
    doe_resource_layout_entry entry = {.kind = DOE_BIND_EXTERNAL_TEXTURE, .visibility = WGPUShaderStage_Fragment};
    doe_resource_binding binding = {.kind = DOE_BIND_EXTERNAL_TEXTURE, .resource.external_texture = external};
    doe_resource_layout *layout;
    doe_resource_bind_group *group = make_group(&f, &entry, &binding, 1, &layout);
    doe_resource_dispatch_group input = {group, NULL, 0};
    doe_resource_bundle_encoder *builder = bundle_encoder(&f);
    assert(doe_resource_bundle_encoder_set_group(builder, 0, &input) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_group(builder, 0, NULL) == DOE_RESOURCE_OK);
    doe_resource_render_bundle *bundle;
    assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OK);
    doe_resource_bundle_encoder_release(builder);
    doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    doe_resource_external_texture_expire(external);
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass = begin(&f, color, &encoder);
    assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_UNAVAILABLE);
    doe_resource_render_pass_release(pass);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_external_texture_refresh(external) == DOE_RESOURCE_OK);
    pass = begin(&f, color, &encoder);
    assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_OK);
    doe_resource_render_bundle_release(bundle);
    doe_resource_commands *commands = finish(encoder, pass);
    doe_resource_external_texture_expire(external);
    doe_resource_external_texture_release(external);
    doe_resource_view_release(plane);
    doe_resource_view_release(color);
    assert(!view_tokens[1].releases);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_UNAVAILABLE);
    doe_resource_commands_release(commands);
    assert(view_tokens[1].releases == 1 && image_tokens[1].releases == 1);
    empty(&f);
}

static void test_scope_allocation_failure(void)
{
    fixture f = fresh();
    native_token tokens[2] = {{0}};
    doe_resource_buffer *vertices = buffer(&f, &tokens[0], 64, WGPUBufferUsage_Vertex);
    doe_resource_buffer *storage = buffer(&f, &tokens[1], 64, WGPUBufferUsage_Storage);
    doe_resource_layout *layout;
    doe_resource_bind_group *group = buffer_group(&f, storage, &layout);
    for (unsigned scenario = 0; scenario < 2; scenario++) {
        doe_resource_bundle_encoder *encoder = bundle_encoder(&f);
        f.fail_at = f.allocations;
        size_t live = f.live;
        doe_resource_dispatch_group input = {group, NULL, 0};
        doe_resource_status status = scenario ? doe_resource_bundle_encoder_set_group(encoder, 0, &input) :
            doe_resource_bundle_encoder_set_vertex(encoder, 0, vertices, 0, 64);
        assert(status == DOE_RESOURCE_OUT_OF_MEMORY && f.live == live);
        doe_resource_render_bundle *sentinel = (void *)(uintptr_t)1;
        assert(doe_resource_bundle_encoder_finish(encoder, &sentinel) == DOE_RESOURCE_OUT_OF_MEMORY);
        assert(sentinel == (void *)(uintptr_t)1);
        doe_resource_bundle_encoder_release(encoder);
        f.fail_at = SIZE_MAX;
    }
    doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    doe_resource_buffer_release(storage);
    doe_resource_buffer_release(vertices);
    assert(tokens[0].releases == 1 && tokens[1].releases == 1);
    empty(&f);
}

static void test_indirect_history(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token token = {0}, pipeline_token = {0};
    doe_resource_buffer *arguments = buffer(&f, &token, 256, WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect);
    doe_resource_layout *layout;
    doe_resource_bind_group *group = buffer_group(&f, arguments, &layout);
    doe_resource_pipeline_layout *pipeline_layout;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &pipeline_layout) == DOE_RESOURCE_OK);
    doe_resource_render_pipeline_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 1};
    doe_resource_render_pipeline *pipeline;
    assert(doe_resource_render_pipeline_create(&c, pipeline_layout, &desc,
        native(&f, &pipeline_token), &pipeline) == DOE_RESOURCE_OK);
    doe_resource_bundle_encoder *builder = bundle_encoder(&f);
    doe_resource_dispatch_group input = {group, NULL, 0};
    assert(doe_resource_bundle_encoder_set_pipeline(builder, pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_group(builder, 3, &input) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_group(builder, 3, NULL) == DOE_RESOURCE_OK);
    size_t allocations = f.allocations;
    assert(doe_resource_bundle_encoder_draw_indirect(builder, arguments, 128, false) == DOE_RESOURCE_USAGE);
    assert(f.allocations == allocations);
    doe_resource_bundle_encoder_release(builder);
    doe_resource_render_pipeline_release(pipeline);
    doe_resource_pipeline_layout_release(pipeline_layout);
    doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    doe_resource_buffer_release(arguments);
    assert(token.releases == 1 && pipeline_token.releases == 1);
    empty(&f);
}

int main(void)
{
    test_overwritten_setters();
    test_history_conflicts();
    test_attachment_scopes();
    test_draw_aliases();
    test_unused_external_expiry();
    test_scope_allocation_failure();
    test_indirect_history();
    puts("render usage owner: 7 host suites passed; no GPU execution claimed");
    return 0;
}
