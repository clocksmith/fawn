/* SPDX-License-Identifier: MIT
 * One pass draw budget, including bundle expansion and no-op draw commands. */
#include "fixture.h"
#include "../src/internal.h"

typedef struct budget_fixture {
    fixture f;
    native_token texture_token, view_token, pipeline_token, buffer_token;
    doe_resource_texture *texture;
    doe_resource_view *view;
    doe_resource_pipeline_layout *layout;
    doe_resource_render_pipeline *pipeline;
    doe_resource_buffer *arguments;
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass;
} budget_fixture;

static void begin(budget_fixture *b, bool present, uint64_t maximum)
{
    memset(b, 0, sizeof(*b));
    b->f = fresh();
    doe_resource_config c = config(&b->f);
    doe_resource_texture_desc td = texture_desc();
    td.usage = WGPUTextureUsage_RenderAttachment;
    doe_resource_texture_limits limits = texture_limits();
    assert(doe_resource_texture_create(&c, &td, &limits, native(&b->f, &b->texture_token), &b->texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(b->texture, &(doe_resource_view_desc){0}, native(&b->f, &b->view_token), &b->view) == DOE_RESOURCE_OK);
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &b->layout) == DOE_RESOURCE_OK);
    doe_resource_render_pipeline_desc pd = {.color_format = td.format, .samples = 1};
    assert(doe_resource_render_pipeline_create(&c, b->layout, &pd, native(&b->f, &b->pipeline_token), &b->pipeline) == DOE_RESOURCE_OK);
    b->arguments = buffer(&b->f, &b->buffer_token, 20, WGPUBufferUsage_Indirect | WGPUBufferUsage_Index);
    assert(doe_resource_encoder_create(&c, &b->encoder) == DOE_RESOURCE_OK);
    doe_resource_render_targets targets = {.color = b->view, .color_load = WGPULoadOp_Clear,
        .color_store = WGPUStoreOp_Store, .max_draw_count = maximum, .has_max_draw_count = present};
    assert(doe_resource_encoder_begin_render(b->encoder, &targets, &b->pass) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_set_pipeline(b->pass, b->pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_set_index(b->pass, b->arguments, WGPUIndexFormat_Uint16, 0, 20) == DOE_RESOURCE_OK);
    assert(b->pass->state.targets.has_max_draw_count);
    assert(b->pass->state.targets.max_draw_count == (present ? maximum : UINT64_C(50000000)));
}

static void release_callers(budget_fixture *b)
{
    doe_resource_render_pass_release(b->pass);
    doe_resource_encoder_release(b->encoder);
    doe_resource_render_pipeline_release(b->pipeline);
    doe_resource_pipeline_layout_release(b->layout);
    doe_resource_buffer_release(b->arguments);
    doe_resource_view_release(b->view);
    doe_resource_texture_release(b->texture);
}

static void finish(budget_fixture *b, doe_resource_status expected)
{
    doe_resource_commands *commands = NULL;
    assert(doe_resource_render_pass_end(b->pass) == expected);
    assert(doe_resource_encoder_finish(b->encoder, &commands) == expected);
    release_callers(b);
    if (commands) {
        assert(!b->texture_token.releases && !b->view_token.releases);
        assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
        doe_resource_commands_release(commands);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
        assert(!b->texture_token.releases);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    }
    assert(b->texture_token.releases == 1 && b->view_token.releases == 1);
    assert(b->pipeline_token.releases == 1 && b->buffer_token.releases == 1);
    empty(&b->f);
}

static doe_resource_status draw(budget_fixture *b, unsigned kind)
{
    if (kind == 0) return doe_resource_render_pass_draw(b->pass, 0, 0, 0, 0);
    if (kind == 1) return doe_resource_render_pass_draw_indexed(b->pass, 0, 0, 0, 0, 0);
    return doe_resource_render_pass_draw_indirect(b->pass, b->arguments, 0, kind == 3);
}

static doe_resource_render_bundle *bundle(budget_fixture *b, unsigned count)
{
    doe_resource_config c = config(&b->f);
    doe_resource_bundle_encoder *encoder;
    doe_resource_render_bundle *result;
    doe_resource_render_bundle_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 1};
    assert(doe_resource_bundle_encoder_create(&c, &desc, &encoder) == DOE_RESOURCE_OK);
    if (count) assert(doe_resource_bundle_encoder_set_pipeline(encoder, b->pipeline) == DOE_RESOURCE_OK);
    for (unsigned i = 0; i < count; i++)
        assert(doe_resource_bundle_encoder_draw(encoder, false, 0, 0, 0, 0, 0) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_finish(encoder, &result) == DOE_RESOURCE_OK);
    doe_resource_bundle_encoder_release(encoder);
    return result;
}

static void test_presence_and_zero(void)
{
    budget_fixture b;
    begin(&b, false, UINT64_MAX); /* Ignored omitted value is not an override. */
    assert(draw(&b, 0) == DOE_RESOURCE_OK);
    finish(&b, DOE_RESOURCE_OK);
    begin(&b, true, 0);
    doe_resource_render_bundle *empty_bundle = bundle(&b, 0);
    assert(doe_resource_render_pass_execute_bundles(b.pass, &empty_bundle, 1) == DOE_RESOURCE_OK);
    doe_resource_render_bundle_release(empty_bundle);
    assert(doe_resource_render_pass_debug(b.pass, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_OK);
    assert(!b.pass->draw_count);
    finish(&b, DOE_RESOURCE_OK);
    for (unsigned kind = 0; kind < 4; kind++) {
        begin(&b, true, 0);
        size_t allocations = b.f.allocations, records = b.encoder->count;
        assert(draw(&b, kind) == DOE_RESOURCE_RANGE);
        assert(b.f.allocations == allocations && b.encoder->count == records && !b.pass->draw_count);
        assert(draw(&b, 0) == DOE_RESOURCE_RANGE);
        finish(&b, DOE_RESOURCE_RANGE);
    }
}

static void test_all_draws_share_budget(void)
{
    budget_fixture b;
    begin(&b, true, 4);
    for (unsigned kind = 0; kind < 4; kind++) assert(draw(&b, kind) == DOE_RESOURCE_OK);
    assert(b.pass->draw_count == 4);
    size_t allocations = b.f.allocations, records = b.encoder->count;
    assert(draw(&b, 0) == DOE_RESOURCE_RANGE);
    assert(b.f.allocations == allocations && b.encoder->count == records && b.pass->draw_count == 4);
    finish(&b, DOE_RESOURCE_RANGE);
    begin(&b, true, 1);
    b.f.fail_at = b.f.allocations;
    assert(draw(&b, 0) == DOE_RESOURCE_OUT_OF_MEMORY);
    assert(!b.pass->draw_count && b.encoder->count == 1);
    finish(&b, DOE_RESOURCE_OUT_OF_MEMORY);
}

static void test_bundle_preflight(void)
{
    budget_fixture b;
    begin(&b, true, 3);
    doe_resource_render_bundle *two = bundle(&b, 2);
    assert(draw(&b, 0) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_execute_bundles(b.pass, &two, 1) == DOE_RESOURCE_OK);
    assert(b.pass->draw_count == 3);
    doe_resource_render_bundle_release(two);
    finish(&b, DOE_RESOURCE_OK);
    begin(&b, true, 1);
    doe_resource_render_bundle *one = bundle(&b, 1);
    doe_resource_render_bundle *twice[] = {one, one};
    size_t allocations = b.f.allocations, records = b.encoder->count;
    assert(doe_resource_render_pass_execute_bundles(b.pass, twice, 2) == DOE_RESOURCE_RANGE);
    assert(b.f.allocations == allocations && b.encoder->count == records && !b.pass->draw_count);
    doe_resource_render_bundle_release(one);
    finish(&b, DOE_RESOURCE_RANGE);
    begin(&b, true, 0);
    one = bundle(&b, 1);
    assert(doe_resource_render_pass_execute_bundles(b.pass, &one, 1) == DOE_RESOURCE_RANGE);
    doe_resource_render_bundle_release(one);
    finish(&b, DOE_RESOURCE_RANGE);
}

static void test_final_counter_without_wrap(void)
{
    budget_fixture b;
    begin(&b, true, UINT64_MAX);
    /* Private boundary injection avoids an impractical 2^64-command allocation. */
    b.pass->draw_count = UINT64_MAX - 1;
    assert(draw(&b, 0) == DOE_RESOURCE_OK && b.pass->draw_count == UINT64_MAX);
    assert(draw(&b, 0) == DOE_RESOURCE_RANGE && b.pass->draw_count == UINT64_MAX);
    finish(&b, DOE_RESOURCE_RANGE);
}

int main(void)
{
    test_presence_and_zero();
    test_all_draws_share_budget();
    test_bundle_preflight();
    test_final_counter_without_wrap();
    puts("render draw budget: 4 host suites passed; no native draw execution claimed");
    return 0;
}
