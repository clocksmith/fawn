/* SPDX-License-Identifier: MIT */
#include "fixture.h"

static doe_resource_view *make_view(fixture *f, native_token tokens[2],
    doe_resource_texture_desc desc, doe_resource_view_desc view_desc)
{
    doe_resource_config c = config(f);
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    doe_resource_view *view;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(f, &tokens[0]), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &view_desc, native(f, &tokens[1]), &view) == DOE_RESOURCE_OK);
    doe_resource_texture_release(texture);
    return view;
}

static doe_resource_texture_desc render_texture(uint32_t samples)
{
    doe_resource_texture_desc desc = texture_desc();
    desc.samples = samples;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    return desc;
}

static void test_resolve_targets(void)
{
    const doe_resource_status expected[] = {
        DOE_RESOURCE_OK, DOE_RESOURCE_LAYOUT, DOE_RESOURCE_LAYOUT, DOE_RESOURCE_LAYOUT,
        DOE_RESOURCE_LAYOUT, DOE_RESOURCE_USAGE, DOE_RESOURCE_USAGE, DOE_RESOURCE_UNAVAILABLE,
        DOE_RESOURCE_DEVICE_MISMATCH, DOE_RESOURCE_OK, DOE_RESOURCE_LAYOUT, DOE_RESOURCE_LAYOUT,
        DOE_RESOURCE_OK, DOE_RESOURCE_OK, DOE_RESOURCE_LAYOUT, DOE_RESOURCE_OK,
        DOE_RESOURCE_OK /* a single-layer 2D array view is a renderable view */
    };
    for (size_t scenario = 0; scenario < sizeof(expected) / sizeof(*expected); scenario++) {
        fixture f = fresh(), other = fresh();
        doe_resource_config c = config(&f);
        native_token tokens[4] = {{0}};
        doe_resource_texture_desc source = render_texture(4), destination = render_texture(1);
        doe_resource_view_desc source_view = {0}, destination_view = {0};
        WGPUTextureFormat srgb = WGPUTextureFormat_RGBA8UnormSrgb;
        if (scenario == 1) source.samples = 1;
        if (scenario == 2) destination.samples = 4;
        if (scenario == 3) destination.width = 4;
        if (scenario == 4) destination.format = WGPUTextureFormat_RGBA16Float;
        if (scenario == 5) destination.usage = WGPUTextureUsage_TextureBinding;
        if (scenario == 6) destination.usage = WGPUTextureUsage_TransientAttachment | WGPUTextureUsage_RenderAttachment;
        if (scenario == 9) {
            destination.width = destination.height = 16;
            destination.mip_levels = 2;
            destination_view.base_mip = 1;
        }
        if (scenario >= 10 && scenario <= 12) {
            source.view_formats = &srgb;
            source.view_format_count = 1;
            source_view.format = srgb;
            if (scenario == 11) destination.format = srgb;
            if (scenario == 12) {
                destination.view_formats = &srgb;
                destination.view_format_count = 1;
                destination_view.format = srgb;
            }
        }
        if (scenario == 13) source.usage = WGPUTextureUsage_TransientAttachment | WGPUTextureUsage_RenderAttachment;
        if (scenario == 14) destination.mip_levels = 2;
        if (scenario == 15) {
            destination.depth_or_layers = 2;
            destination_view.dimension = WGPUTextureViewDimension_2D;
            destination_view.base_layer = 1;
            destination_view.layer_count = 1;
        }
        if (scenario == 16) destination_view.dimension = WGPUTextureViewDimension_2DArray;
        doe_resource_view *color = make_view(&f, tokens, source, source_view);
        doe_resource_view *resolve = make_view(scenario == 8 ? &other : &f, &tokens[2], destination, destination_view);
        if (scenario == 7) doe_resource_texture_destroy(doe_resource_view_texture(resolve));
        doe_resource_render_targets targets = {.color = color, .resolve_target = resolve,
            .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Discard};
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass = NULL;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        size_t allocations = f.allocations;
        unsigned references = f.device_references;
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == expected[scenario]);
        doe_resource_commands *commands = NULL;
        if (expected[scenario] == DOE_RESOURCE_OK) {
            assert(doe_resource_render_pass_execute_bundles(pass, NULL, 0) == DOE_RESOURCE_OK);
            assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
            doe_resource_render_pass_release(pass);
            assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
            size_t count;
            const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
            assert(count == 2);
            for (size_t i = 0; i < count; i++) {
                const doe_resource_render_packet *packet = doe_resource_render_record_borrow(records[i].value.render);
                assert(packet->targets.colors[0].view == color && packet->targets.colors[0].resolve_target == resolve);
            }
        } else {
            assert(!pass && f.allocations == allocations && f.device_references == references);
            assert(doe_resource_encoder_finish(encoder, &commands) == expected[scenario] && !commands);
        }
        doe_resource_encoder_release(encoder);
        doe_resource_view_release(color);
        doe_resource_view_release(resolve);
        if (commands) {
            for (size_t i = 0; i < 4; i++) assert(!tokens[i].releases);
            assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
            doe_resource_commands_release(commands);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
        }
        for (size_t i = 0; i < 4; i++) assert(tokens[i].releases == 1);
        empty(&f);
        empty(&other);
    }
}

static void test_resolve_scope_history(void)
{
    for (unsigned bundled = 0; bundled < 2; bundled++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token tokens[4] = {{0}};
        doe_resource_view *color = make_view(&f, tokens, render_texture(4), (doe_resource_view_desc){0});
        doe_resource_view *resolve = make_view(&f, &tokens[2], render_texture(1), (doe_resource_view_desc){0});
        doe_resource_layout_entry entry = {.kind = DOE_BIND_TEXTURE, .visibility = WGPUShaderStage_Fragment,
            .sample_type = WGPUTextureSampleType_Float, .view_dimension = WGPUTextureViewDimension_2D};
        doe_resource_layout *layout;
        assert(doe_resource_layout_create(&c, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
        doe_resource_binding binding = {.kind = DOE_BIND_TEXTURE, .resource.view = resolve};
        doe_resource_binding_limits limits = {16, 16, 256, 256};
        doe_resource_bind_group *group;
        assert(doe_resource_bind_group_create(layout, &binding, 1, &limits, &group) == DOE_RESOURCE_OK);
        doe_resource_dispatch_group input = {.group = group};
        doe_resource_render_targets targets = {.color = color, .resolve_target = resolve,
            .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store};
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
        if (bundled) {
            doe_resource_render_bundle_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 4};
            doe_resource_bundle_encoder *builder;
            doe_resource_render_bundle *bundle;
            assert(doe_resource_bundle_encoder_create(&c, &desc, &builder) == DOE_RESOURCE_OK);
            assert(doe_resource_bundle_encoder_set_group(builder, 0, &input) == DOE_RESOURCE_OK);
            assert(doe_resource_bundle_encoder_set_group(builder, 0, NULL) == DOE_RESOURCE_OK);
            assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OK);
            doe_resource_bundle_encoder_release(builder);
            assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_USAGE);
            doe_resource_render_bundle_release(bundle);
        } else assert(doe_resource_render_pass_set_group(pass, 0, &input) == DOE_RESOURCE_USAGE);
        assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_USAGE);
        doe_resource_bind_group_release(group);
        doe_resource_layout_release(layout);
        doe_resource_render_pass_release(pass);
        doe_resource_encoder_release(encoder);
        doe_resource_view_release(color);
        doe_resource_view_release(resolve);
        for (size_t i = 0; i < 4; i++) assert(tokens[i].releases == 1);
        empty(&f);
    }
}

static void test_resolve_failure_retirement(void)
{
    for (unsigned scenario = 0; scenario < 6; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token tokens[4] = {{0}};
        doe_resource_view *color = make_view(&f, tokens, render_texture(4), (doe_resource_view_desc){0});
        doe_resource_view *resolve = make_view(&f, &tokens[2], render_texture(1), (doe_resource_view_desc){0});
        doe_resource_render_targets targets = {.color = color, .resolve_target = resolve,
            .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Discard};
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass = NULL;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        if (scenario < 4) f.fail_at = f.allocations + scenario;
        doe_resource_status status = doe_resource_encoder_begin_render(encoder, &targets, &pass);
        doe_resource_commands *commands = NULL;
        if (scenario < 4) {
            assert(status == DOE_RESOURCE_OUT_OF_MEMORY && !pass);
            assert(doe_resource_encoder_finish(encoder, &commands) == status && !commands);
        } else {
            assert(status == DOE_RESOURCE_OK);
            assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
            doe_resource_render_pass_release(pass);
            assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
            if (scenario == 4) doe_resource_texture_destroy(doe_resource_view_texture(resolve));
        }
        doe_resource_encoder_release(encoder);
        doe_resource_view_release(color);
        doe_resource_view_release(resolve);
        if (scenario == 4) {
            assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_UNAVAILABLE);
            doe_resource_commands_release(commands);
        }
        if (scenario == 5) {
            assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
            doe_resource_commands_release(commands);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_EXECUTION_FAILED) == DOE_RESOURCE_EXECUTION_FAILED);
        }
        for (size_t i = 0; i < 4; i++) assert(tokens[i].releases == 1);
        empty(&f);
    }
}

static void test_resolve_bundle_packets(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token tokens[5] = {{0}};
    doe_resource_view *color = make_view(&f, tokens, render_texture(4), (doe_resource_view_desc){0});
    doe_resource_view *resolve = make_view(&f, &tokens[2], render_texture(1), (doe_resource_view_desc){0});
    doe_resource_pipeline_layout *layout;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
    doe_resource_render_pipeline_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 4};
    doe_resource_render_pipeline *pipeline;
    assert(doe_resource_render_pipeline_create(&c, layout, &desc, native(&f, &tokens[4]), &pipeline) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout_release(layout);
    doe_resource_render_bundle_desc signature = {.color_format = desc.color_format, .samples = desc.samples};
    doe_resource_bundle_encoder *builder;
    doe_resource_render_bundle *bundle;
    assert(doe_resource_bundle_encoder_create(&c, &signature, &builder) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_pipeline(builder, pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_draw(builder, false, 3, 1, 0, 0, 0) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OK);
    doe_resource_bundle_encoder_release(builder);
    doe_resource_render_pipeline_release(pipeline);
    doe_resource_render_targets targets = {.color = color, .resolve_target = resolve,
        .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Discard};
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_OK);
    doe_resource_render_bundle_release(bundle);
    assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_render_pass_release(pass);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_view_release(color);
    doe_resource_view_release(resolve);
    size_t count;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    assert(count == 4);
    for (size_t i = 0; i < count; i++) {
        const doe_resource_render_packet *packet = doe_resource_render_record_borrow(records[i].value.render);
        assert(packet->targets.colors[0].resolve_target == resolve && packet->targets.colors[0].view == color);
        assert(packet->first_in_pass == (i == 0) && packet->last_in_pass == (i == 3));
        assert(!tokens[i].releases);
    }
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    for (size_t i = 0; i < 5; i++) assert(tokens[i].releases == 1);
    empty(&f);
}

int main(void)
{
    test_resolve_targets();
    test_resolve_scope_history();
    test_resolve_failure_retirement();
    test_resolve_bundle_packets();
    puts("resolve admission owner: 4 host suites passed; no multisample GPU execution claimed");
    return 0;
}
