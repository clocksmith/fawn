/* SPDX-License-Identifier: MIT
 * Requested-device viewport admission, independent of native physical limits. */
#include "fixture.h"
#include <float.h>
#include <math.h>

typedef struct viewport_case {
    float values[6];
    uint32_t limit;
    doe_resource_status expected;
} viewport_case;

static void run_case(const viewport_case *test, bool invalidate_scissor)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    c.max_texture_dimension_2d = test->limit;
    native_token texture_token = {0}, view_token = {0};
    doe_resource_texture_desc desc = texture_desc();
    desc.usage = WGPUTextureUsage_RenderAttachment;
    if (test->limit && test->limit < 8) desc.width = desc.height = test->limit;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    doe_resource_view *view;
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &texture_token), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &(doe_resource_view_desc){0}, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    c.max_texture_dimension_2d = 1; /* The encoder owns a copied limit, not this caller storage. */
    const doe_resource_render_targets targets = {.color = view,
        .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store};
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    const doe_resource_render_state before = *doe_resource_render_pass_state(pass);
    doe_resource_render_state state = before;
    memcpy(state.viewport, test->values, sizeof(state.viewport));
    if (invalidate_scissor) state.scissor[2] = 9;
    size_t allocations = f.allocations;
    doe_resource_status status = doe_resource_render_pass_set_state(pass, &state);
    assert(status == test->expected && f.allocations == allocations);
    doe_resource_commands *commands = NULL;
    if (status == DOE_RESOURCE_OK) {
        assert(!memcmp(doe_resource_render_pass_state(pass), &state, sizeof(state)));
        assert(doe_resource_render_pass_execute_bundles(pass, NULL, 0) == DOE_RESOURCE_OK);
        assert(!memcmp(doe_resource_render_pass_state(pass), &state, sizeof(state)));
        assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
        size_t count;
        const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
        assert(count == 2 && records[1].kind == DOE_COMMAND_RENDER);
        const doe_resource_render_packet *end = doe_resource_render_record_borrow(records[1].value.render);
        assert(end->last_in_pass && end->boundary_only && !memcmp(&end->state, &state, sizeof(state)));
    } else {
        assert(!memcmp(doe_resource_render_pass_state(pass), &before, sizeof(before)));
        assert(doe_resource_render_pass_set_state(pass, &before) == status);
        assert(doe_resource_render_pass_end(pass) == status);
        assert(doe_resource_encoder_finish(encoder, &commands) == status && !commands);
    }
    doe_resource_render_pass_release(pass);
    doe_resource_encoder_release(encoder);
    doe_resource_view_release(view);
    doe_resource_texture_release(texture);
    if (commands) {
        assert(!texture_token.releases && !view_token.releases);
        assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
        doe_resource_commands_release(commands);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
        assert(!texture_token.releases && !view_token.releases);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    }
    assert(texture_token.releases == 1 && view_token.releases == 1);
    empty(&f);
}

static void test_requested_bounds(void)
{
    const viewport_case cases[] = {
        {{-1, -1, 8, 8, 0, 1}, 32, DOE_RESOURCE_OK},
        {{-64, -64, 0, 0, 0, 1}, 32, DOE_RESOURCE_OK},
        {{63, 63, 0, 0, 0, 1}, 32, DOE_RESOURCE_OK},
        {{31, 31, 32, 32, 0, 1}, 32, DOE_RESOURCE_OK},
        {{-65, 0, 0, 8, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{0, -65, 8, 0, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{64, 0, 0, 8, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{0, 64, 8, 0, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{32, 0, 32, 8, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{0, 32, 8, 32, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{0, 0, 33, 8, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{0, 0, 8, 33, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{0, 0, -1, 8, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{0, 0, 8, -1, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{0, 0, -0.0f, -0.0f, -0.0f, 1}, 32, DOE_RESOURCE_OK},
        {{0, 0, 0, 8, 0, 1}, 32, DOE_RESOURCE_OK},
        {{0, 0, 8, 0, 0, 1}, 32, DOE_RESOURCE_OK},
        {{32, 32, 33, 33, 0, 1}, 33, DOE_RESOURCE_OK},
        {{33, 32, 33, 33, 0, 1}, 33, DOE_RESOURCE_RANGE},
        {{0, 0, 8, 8, -0.125f, 1}, 32, DOE_RESOURCE_RANGE},
        {{0, 0, 8, 8, 0, 1.125f}, 32, DOE_RESOURCE_RANGE},
        {{0, 0, 8, 8, 0.75f, 0.25f}, 32, DOE_RESOURCE_RANGE},
        {{0, 0, 8, 8, 0.5f, 0.5f}, 32, DOE_RESOURCE_OK}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) run_case(&cases[i], false);
    /* Viewport may extend outside 8x8; scissor remains attachment-bounded. */
    const viewport_case scissor = {{-1, -1, 32, 32, 0, 1}, 32, DOE_RESOURCE_RANGE};
    run_case(&scissor, true);
}

static void test_finite_and_widened_edges(void)
{
    for (size_t component = 0; component < 6; component++) {
        const float bad[] = {NAN, INFINITY, -INFINITY};
        for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
            viewport_case test = {{0, 0, 8, 8, 0, 1}, 32, DOE_RESOURCE_RANGE};
            test.values[component] = bad[i];
            run_case(&test, false);
        }
    }
    const viewport_case cases[] = {
        {{0, 0, FLT_MAX, 8, 0, 1}, UINT32_MAX, DOE_RESOURCE_RANGE},
        {{FLT_MAX, 0, 0, 8, 0, 1}, UINT32_MAX, DOE_RESOURCE_RANGE},
        {{-FLT_MAX, 0, 8, 8, 0, 1}, UINT32_MAX, DOE_RESOURCE_RANGE},
        {{-0x1.fffffep32f, 0, 0, 0, 0, 1}, UINT32_MAX, DOE_RESOURCE_OK},
        {{0x1.fffffep32f, 0, 0, 0, 0, 1}, UINT32_MAX, DOE_RESOURCE_OK},
        {{-0x1p33f, 0, 0, 0, 0, 1}, UINT32_MAX, DOE_RESOURCE_RANGE},
        {{0x1p33f, 0, 0, 0, 0, 1}, UINT32_MAX, DOE_RESOURCE_RANGE},
        {{0, 0, 0x1.fffffep31f, 0, 0, 1}, UINT32_MAX, DOE_RESOURCE_OK},
        {{0, 0, 0x1p32f, 0, 0, 1}, UINT32_MAX, DOE_RESOURCE_RANGE},
        /* Float addition rounds this forbidden edge down to the legal boundary. */
        {{63, 0, 0x1p-20f, 0, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{0, 63, 0, 0x1p-20f, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{63, 0, 0x1p-149f, 0, 0, 1}, 32, DOE_RESOURCE_RANGE},
        {{0x1p-149f, 0, 1, 1, 0, 1}, 1, DOE_RESOURCE_RANGE},
        {{0, 0x1p-149f, 1, 1, 0, 1}, 1, DOE_RESOURCE_RANGE},
        {{0, 0, 1, 1, 0, 1}, 1, DOE_RESOURCE_OK}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) run_case(&cases[i], false);
}

static void test_default_limit(void)
{
    const viewport_case cases[] = {
        {{-16384, -16384, 8192, 8192, 0, 1}, 0, DOE_RESOURCE_OK},
        {{8191, 8191, 8192, 8192, 0, 1}, 0, DOE_RESOURCE_OK},
        {{8192, 8191, 8192, 8192, 0, 1}, 0, DOE_RESOURCE_RANGE},
        {{0, 0, 8193, 8, 0, 1}, 0, DOE_RESOURCE_RANGE}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) run_case(&cases[i], false);
}

int main(void)
{
    test_requested_bounds();
    test_finite_and_widened_edges();
    test_default_limit();
    puts("viewport admission owner: 3 host suites passed; native physical viewport support remains separate");
    return 0;
}
