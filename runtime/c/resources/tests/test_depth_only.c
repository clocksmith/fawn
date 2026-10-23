/* SPDX-License-Identifier: MIT */
#include "fixture.h"
#include <math.h>

static doe_resource_view *attachment(fixture *f, native_token tokens[2],
    WGPUTextureFormat format, uint32_t samples)
{
    doe_resource_config c = config(f);
    doe_resource_texture_desc desc = texture_desc();
    desc.format = format;
    desc.samples = samples;
    desc.width = 16;
    desc.height = 8;
    desc.mip_levels = samples == 1 ? 2 : 1;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    doe_resource_view *view;
    doe_resource_view_desc view_desc = {.base_mip = samples == 1 ? 1 : 0, .mip_count = 1};
    assert(doe_resource_texture_create(&c, &desc, &limits, native(f, &tokens[0]), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &view_desc, native(f, &tokens[1]), &view) == DOE_RESOURCE_OK);
    doe_resource_texture_release(texture);
    return view;
}

static void test_depth_stencil_targets(void)
{
    const struct { WGPUTextureFormat format; bool depth, stencil; } cases[] = {
        {WGPUTextureFormat_Depth16Unorm, true, false},
        {WGPUTextureFormat_Depth24Plus, true, false},
        {WGPUTextureFormat_Depth32Float, true, false},
        {WGPUTextureFormat_Stencil8, false, true},
        {WGPUTextureFormat_Depth24PlusStencil8, true, true},
        {WGPUTextureFormat_Depth32FloatStencil8, true, true}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) for (unsigned mode = 0; mode < 3; mode++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token tokens[2] = {{0}};
        uint32_t samples = mode == 1 ? 4 : 1;
        doe_resource_view *view = attachment(&f, tokens, cases[i].format, samples);
        doe_resource_render_targets targets = {.depth_stencil = view, .clear_depth = 1,
            .clear_color = {NAN, INFINITY, -INFINITY, NAN}};
        if (mode == 2) targets.depth_read_only = targets.stencil_read_only = true;
        else {
            if (cases[i].depth) { targets.depth_load = WGPULoadOp_Clear; targets.depth_store = WGPUStoreOp_Store; }
            if (cases[i].stencil) { targets.stencil_load = WGPULoadOp_Clear; targets.stencil_store = WGPUStoreOp_Store; }
        }
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
        const doe_resource_render_state *state = doe_resource_render_pass_state(pass);
        assert(state->viewport[2] == (samples == 1 ? 8 : 16));
        assert(state->viewport[3] == (samples == 1 ? 4 : 8));
        assert(state->scissor[2] == (samples == 1 ? 8 : 16));
        assert(doe_resource_render_pass_execute_bundles(pass, NULL, 0) == DOE_RESOURCE_OK);
        assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
        doe_resource_commands *commands;
        assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
        doe_resource_render_pass_release(pass);
        doe_resource_encoder_release(encoder);
        doe_resource_view_release(view);
        size_t count;
        const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
        assert(count == 2);
        const doe_resource_render_packet *first = doe_resource_render_record_borrow(records[0].value.render);
        const doe_resource_render_packet *last = doe_resource_render_record_borrow(records[1].value.render);
        assert(!first->targets.color && first->targets.depth_stencil == view && first->first_in_pass);
        assert(last->last_in_pass && !last->targets.color);
        assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
        doe_resource_commands_release(commands);
        assert(!tokens[0].releases && !tokens[1].releases);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
        assert(tokens[0].releases == 1 && tokens[1].releases == 1);
        empty(&f);
    }
}

static void test_depth_only_draw_bundles(void)
{
    for (unsigned scenario = 0; scenario < 4; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token tokens[3] = {{0}};
        doe_resource_view *view = attachment(&f, tokens, WGPUTextureFormat_Depth32Float, 1);
        doe_resource_pipeline_layout *layout;
        assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
        doe_resource_render_pipeline_desc pipeline_desc = {.samples = 1,
            .depth_stencil_format = WGPUTextureFormat_Depth32Float, .depth_write = true};
        doe_resource_render_pipeline *pipeline;
        assert(doe_resource_render_pipeline_create(&c, layout, &pipeline_desc,
            native(&f, &tokens[2]), &pipeline) == DOE_RESOURCE_OK);
        doe_resource_pipeline_layout_release(layout);
        doe_resource_render_bundle_desc desc = {.samples = 1, .depth_stencil_format = WGPUTextureFormat_Depth32Float};
        if (scenario == 1) desc.color_format = WGPUTextureFormat_RGBA8Unorm;
        if (scenario == 3) desc.samples = 4;
        doe_resource_bundle_encoder *builder;
        doe_resource_render_bundle *bundle;
        assert(doe_resource_bundle_encoder_create(&c, &desc, &builder) == DOE_RESOURCE_OK);
        if (scenario == 0 || scenario == 2) {
            assert(doe_resource_bundle_encoder_set_pipeline(builder, pipeline) == DOE_RESOURCE_OK);
            assert(doe_resource_bundle_encoder_draw(builder, false, 3, 1, 0, 0, 0) == DOE_RESOURCE_OK);
        }
        assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OK);
        doe_resource_bundle_encoder_release(builder);
        doe_resource_render_pipeline_release(pipeline);
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass;
        doe_resource_render_targets targets = {.depth_stencil = view, .clear_depth = 1,
            .depth_load = WGPULoadOp_Clear, .depth_store = WGPUStoreOp_Store};
        if (scenario == 2) {
            targets.depth_load = WGPULoadOp_Undefined;
            targets.depth_store = WGPUStoreOp_Undefined;
            targets.depth_read_only = true;
        }
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
        doe_resource_status expected = scenario ? DOE_RESOURCE_LAYOUT : DOE_RESOURCE_OK;
        assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == expected);
        doe_resource_render_bundle_release(bundle);
        assert(doe_resource_render_pass_end(pass) == expected);
        doe_resource_commands *commands = NULL;
        assert(doe_resource_encoder_finish(encoder, &commands) == expected);
        doe_resource_render_pass_release(pass);
        doe_resource_encoder_release(encoder);
        doe_resource_view_release(view);
        if (!scenario) {
            size_t count;
            const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
            assert(count == 3);
            const doe_resource_render_packet *draw = doe_resource_render_record_borrow(records[1].value.render);
            assert(!draw->targets.color && draw->pipeline == pipeline && draw->count == 3);
            assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
            doe_resource_commands_release(commands);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_EXECUTION_FAILED) == DOE_RESOURCE_EXECUTION_FAILED);
        } else assert(!commands);
        for (size_t i = 0; i < 3; i++) assert(tokens[i].releases == 1);
        empty(&f);
    }
}

static void test_depth_only_failures(void)
{
    for (unsigned scenario = 0; scenario < 9; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token tokens[2] = {{0}};
        doe_resource_view *view = attachment(&f, tokens, WGPUTextureFormat_Depth32Float, 1);
        doe_resource_render_targets targets = {.depth_stencil = view,
            .depth_load = WGPULoadOp_Clear, .depth_store = WGPUStoreOp_Store, .clear_depth = 1};
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass = NULL;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        size_t allocations = f.allocations;
        doe_resource_status expected = DOE_RESOURCE_LAYOUT;
        if (scenario == 0) targets.depth_stencil = NULL;
        else if (scenario == 1) targets.resolve_target = view;
        else if (scenario < 6) { f.fail_at = f.allocations + scenario - 2; expected = DOE_RESOURCE_OUT_OF_MEMORY; }
        else if (scenario == 6) { targets.clear_depth = NAN; expected = DOE_RESOURCE_RANGE; }
        else if (scenario == 7) targets.stencil_load = WGPULoadOp_Clear;
        else expected = DOE_RESOURCE_OK;
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == expected);
        if (expected == DOE_RESOURCE_OK) {
            assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
            doe_resource_commands *commands;
            assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
            doe_resource_texture_destroy(doe_resource_view_texture(view));
            assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_UNAVAILABLE);
            doe_resource_commands_release(commands);
            doe_resource_render_pass_release(pass);
        } else {
            assert(!pass);
            if (expected != DOE_RESOURCE_OUT_OF_MEMORY) assert(f.allocations == allocations);
        }
        doe_resource_encoder_release(encoder);
        doe_resource_view_release(view);
        assert(tokens[0].releases == 1 && tokens[1].releases == 1);
        empty(&f);
    }
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_bundle_encoder *builder = NULL;
    assert(doe_resource_bundle_encoder_create(&c, &(doe_resource_render_bundle_desc){.samples = 1},
        &builder) == DOE_RESOURCE_LAYOUT && !builder && !f.allocations);
    doe_resource_pipeline_layout *layout;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
    native_token token = {0};
    doe_resource_render_pipeline *pipeline = NULL;
    size_t allocations = f.allocations;
    assert(doe_resource_render_pipeline_create(&c, layout,
        &(doe_resource_render_pipeline_desc){.samples = 1}, native(&f, &token), &pipeline) == DOE_RESOURCE_LAYOUT);
    assert(!pipeline && !token.releases && f.allocations == allocations);
    doe_resource_pipeline_layout_release(layout);
    empty(&f);
}

int main(void)
{
    test_depth_stencil_targets();
    test_depth_only_draw_bundles();
    test_depth_only_failures();
    puts("depth-only owner: 3 host suites passed; native drawing support remains separate");
    return 0;
}
