/* SPDX-License-Identifier: MIT */
#include "fixture.h"
#include "../src/internal.h"

static void test_encoder_groups(void)
{
    for (unsigned scenario = 0; scenario < 5; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        doe_resource_encoder *encoder;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        size_t allocations = f.allocations;
        doe_resource_status expected = DOE_RESOURCE_OK;
        if (scenario == 0 || scenario == 2) {
            assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_PUSH) == DOE_RESOURCE_OK);
            assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_OK);
            if (scenario == 0) {
                assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_PUSH) == DOE_RESOURCE_OK);
                assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_OK);
                assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_OK);
            } else expected = DOE_RESOURCE_STATE;
        } else if (scenario == 1) {
            expected = DOE_RESOURCE_STATE;
            assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_POP) == expected);
        } else if (scenario == 3) {
            expected = DOE_RESOURCE_INVALID_ARGUMENT;
            assert(doe_resource_encoder_debug(encoder, (doe_resource_debug_operation)99) == expected);
        } else {
            f.alive = false;
            expected = DOE_RESOURCE_UNAVAILABLE;
            assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_MARKER) == expected);
        }
        assert(f.allocations == allocations);
        doe_resource_commands *commands = NULL;
        assert(doe_resource_encoder_finish(encoder, &commands) == expected);
        if (expected == DOE_RESOURCE_OK) {
            size_t count;
            (void)doe_resource_commands_borrow(commands, &count);
            assert(count == 0);
            assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_STATE);
            assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
            doe_resource_commands_release(commands);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
        } else {
            assert(!commands && f.allocations == allocations);
            assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_POP) == expected);
            assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_MARKER) == expected);
        }
        doe_resource_encoder_release(encoder);
        empty(&f);
    }
}

static void test_compute_groups(void)
{
    for (unsigned scenario = 0; scenario < 5; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        doe_resource_encoder *encoder;
        doe_resource_compute_pass *pass;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_PUSH) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_compute(encoder, &pass) == DOE_RESOURCE_OK);
        size_t allocations = f.allocations;
        doe_resource_status expected = DOE_RESOURCE_STATE;
        if (scenario == 0 || scenario == 1) {
            assert(doe_resource_compute_pass_debug(pass, DOE_RESOURCE_DEBUG_PUSH) == DOE_RESOURCE_OK);
            assert(doe_resource_compute_pass_debug(pass, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_OK);
            if (!scenario) {
                assert(doe_resource_compute_pass_debug(pass, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_OK);
                expected = DOE_RESOURCE_OK;
            }
        } else if (scenario == 2) {
            /* A parent group cannot satisfy a child pop. */
            assert(doe_resource_compute_pass_debug(pass, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_STATE);
        } else if (scenario == 3) {
            assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_STATE);
        } else {
            assert(doe_resource_encoder_invalidate(encoder, DOE_RESOURCE_OUT_OF_MEMORY) == DOE_RESOURCE_OUT_OF_MEMORY);
            expected = DOE_RESOURCE_OUT_OF_MEMORY;
            assert(doe_resource_compute_pass_debug(pass, DOE_RESOURCE_DEBUG_PUSH) == expected);
        }
        assert(f.allocations == allocations && encoder->count == 0);
        assert(doe_resource_compute_pass_end(pass) == expected);
        doe_resource_commands *commands = NULL;
        if (expected == DOE_RESOURCE_OK) {
            assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_OK);
            assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
            assert(doe_resource_compute_pass_debug(pass, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_STATE);
            doe_resource_commands_release(commands);
        } else {
            assert(doe_resource_encoder_finish(encoder, &commands) == expected && !commands);
            assert(doe_resource_compute_pass_debug(pass, DOE_RESOURCE_DEBUG_MARKER) == expected);
        }
        doe_resource_encoder_release(encoder);
        doe_resource_compute_pass_release(pass);
        empty(&f);
    }
}

static doe_resource_view *color_attachment(fixture *f, native_token tokens[2])
{
    doe_resource_config c = config(f);
    doe_resource_texture_desc desc = texture_desc();
    doe_resource_texture_limits limits = texture_limits();
    desc.usage = WGPUTextureUsage_RenderAttachment;
    doe_resource_texture *texture;
    doe_resource_view *view;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(f, &tokens[0]), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &(doe_resource_view_desc){0}, native(f, &tokens[1]), &view) == DOE_RESOURCE_OK);
    doe_resource_texture_release(texture);
    return view;
}

static void test_render_groups(void)
{
    for (unsigned scenario = 0; scenario < 4; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token tokens[2] = {{0}};
        doe_resource_view *view = color_attachment(&f, tokens);
        doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear,
            .color_store = WGPUStoreOp_Store};
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_PUSH) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
        size_t allocations = f.allocations;
        doe_resource_status expected = DOE_RESOURCE_STATE;
        if (scenario == 0 || scenario == 1) {
            assert(doe_resource_render_pass_debug(pass, DOE_RESOURCE_DEBUG_PUSH) == DOE_RESOURCE_OK);
            assert(doe_resource_render_pass_execute_bundles(pass, NULL, 0) == DOE_RESOURCE_OK);
            assert(doe_resource_render_pass_debug(pass, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_OK);
            if (!scenario) {
                assert(doe_resource_render_pass_debug(pass, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_OK);
                expected = DOE_RESOURCE_OK;
            }
        } else if (scenario == 2) {
            assert(doe_resource_render_pass_debug(pass, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_STATE);
        } else {
            assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_STATE);
        }
        assert(f.allocations == allocations && encoder->count == 1);
        assert(doe_resource_render_pass_end(pass) == expected);
        doe_resource_commands *commands = NULL;
        if (expected == DOE_RESOURCE_OK) {
            assert(doe_resource_encoder_debug(encoder, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_OK);
            assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
            assert(doe_resource_render_pass_debug(pass, DOE_RESOURCE_DEBUG_PUSH) == DOE_RESOURCE_STATE);
        } else {
            assert(encoder->count == 1); /* Unbalanced end did not append a boundary. */
            assert(doe_resource_encoder_finish(encoder, &commands) == expected && !commands);
        }
        doe_resource_render_pass_release(pass);
        doe_resource_encoder_release(encoder);
        doe_resource_view_release(view);
        if (commands) {
            assert(!tokens[0].releases && !tokens[1].releases);
            assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
            doe_resource_commands_release(commands);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
            assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
        }
        assert(tokens[0].releases == 1 && tokens[1].releases == 1);
        empty(&f);
    }
}

static void test_bundle_groups(void)
{
    for (unsigned scenario = 0; scenario < 4; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        doe_resource_render_bundle_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 1};
        doe_resource_bundle_encoder *builder;
        assert(doe_resource_bundle_encoder_create(&c, &desc, &builder) == DOE_RESOURCE_OK);
        size_t allocations = f.allocations;
        doe_resource_status expected = DOE_RESOURCE_STATE;
        if (scenario == 0 || scenario == 1) {
            assert(doe_resource_bundle_encoder_debug(builder, DOE_RESOURCE_DEBUG_PUSH) == DOE_RESOURCE_OK);
            for (unsigned i = 0; i < 5000; i++)
                assert(doe_resource_bundle_encoder_debug(builder, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_OK);
            if (!scenario) {
                assert(doe_resource_bundle_encoder_debug(builder, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_OK);
                expected = DOE_RESOURCE_OK;
            }
        } else if (scenario == 2) {
            assert(doe_resource_bundle_encoder_debug(builder, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_STATE);
        } else {
            expected = DOE_RESOURCE_OUT_OF_MEMORY;
            assert(doe_resource_bundle_encoder_invalidate(builder, expected) == expected);
            assert(doe_resource_bundle_encoder_debug(builder, DOE_RESOURCE_DEBUG_PUSH) == expected);
        }
        assert(f.allocations == allocations);
        doe_resource_render_bundle *bundle = NULL;
        assert(doe_resource_bundle_encoder_finish(builder, &bundle) == expected);
        if (expected == DOE_RESOURCE_OK) {
            assert(doe_resource_bundle_encoder_debug(builder, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_STATE);
            doe_resource_render_bundle_release(bundle);
        } else assert(!bundle && f.allocations == allocations);
        doe_resource_bundle_encoder_release(builder);
        empty(&f);
    }
}

static void test_debug_arithmetic(void)
{
    size_t depth = SIZE_MAX;
    assert(resource_debug_apply(&depth, DOE_RESOURCE_DEBUG_PUSH) == DOE_RESOURCE_OVERFLOW && depth == SIZE_MAX);
    assert(resource_debug_apply(&depth, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_OK && depth == SIZE_MAX);
    assert(resource_debug_apply(&depth, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_OK && depth == SIZE_MAX - 1);
    assert(resource_debug_apply(&depth, (doe_resource_debug_operation)-1) == DOE_RESOURCE_INVALID_ARGUMENT && depth == SIZE_MAX - 1);
    depth = 0;
    assert(resource_debug_apply(&depth, DOE_RESOURCE_DEBUG_POP) == DOE_RESOURCE_STATE && depth == 0);
    assert(doe_resource_encoder_debug(NULL, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_INVALID_ARGUMENT);
    assert(doe_resource_compute_pass_debug(NULL, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_INVALID_ARGUMENT);
    assert(doe_resource_render_pass_debug(NULL, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_INVALID_ARGUMENT);
    assert(doe_resource_bundle_encoder_debug(NULL, DOE_RESOURCE_DEBUG_MARKER) == DOE_RESOURCE_INVALID_ARGUMENT);
}

static void test_compute_invalidation(void)
{
    assert(doe_resource_compute_pass_invalidate(NULL, DOE_RESOURCE_LAYOUT) == DOE_RESOURCE_INVALID_ARGUMENT);
    for (unsigned scenario = 0; scenario < 3; scenario++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        doe_resource_encoder *encoder;
        doe_resource_compute_pass *pass;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_compute(encoder, &pass) == DOE_RESOURCE_OK);
        size_t allocations = f.allocations;
        const doe_resource_status ignored[] = {DOE_RESOURCE_OK, DOE_RESOURCE_PENDING,
            (doe_resource_status)-1, (doe_resource_status)999};
        for (size_t i = 0; i < sizeof(ignored) / sizeof(*ignored); i++)
            assert(doe_resource_compute_pass_invalidate(pass, ignored[i]) == DOE_RESOURCE_INVALID_ARGUMENT);
        assert(doe_resource_encoder_status(encoder) == DOE_RESOURCE_OK);
        doe_resource_status expected = DOE_RESOURCE_INVALID_ARGUMENT;
        if (scenario == 0) {
            assert(doe_resource_compute_pass_invalidate(pass, expected) == expected);
            assert(doe_resource_compute_pass_invalidate(pass, DOE_RESOURCE_OUT_OF_MEMORY) == expected);
        } else if (scenario == 1) {
            assert(doe_resource_compute_pass_end(pass) == DOE_RESOURCE_OK);
            expected = DOE_RESOURCE_STATE;
            assert(doe_resource_compute_pass_invalidate(pass, DOE_RESOURCE_LAYOUT) == expected);
        } else {
            f.alive = false;
            expected = DOE_RESOURCE_UNAVAILABLE;
            assert(doe_resource_compute_pass_invalidate(pass, DOE_RESOURCE_LAYOUT) == expected);
        }
        assert(f.allocations == allocations && encoder->count == 0);
        assert(doe_resource_compute_pass_debug(pass, DOE_RESOURCE_DEBUG_PUSH) == expected);
        doe_resource_commands *commands = NULL;
        assert(doe_resource_encoder_finish(encoder, &commands) == expected && !commands);
        doe_resource_encoder_release(encoder);
        doe_resource_compute_pass_release(pass);
        empty(&f);
    }
}

int main(void)
{
    test_encoder_groups();
    test_compute_groups();
    test_render_groups();
    test_bundle_groups();
    test_debug_arithmetic();
    test_compute_invalidation();
    puts("debug group owner: 6 host suites passed; optional native diagnostics not captured");
    return 0;
}
