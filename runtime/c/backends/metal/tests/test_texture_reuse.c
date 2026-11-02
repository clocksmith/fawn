/* SPDX-License-Identifier: MIT
 * Command/hazard/aspect call ledger, not a GPU or pixel execution model. */
#include "mock_native.h"
#include "../src/texture_internal.h"
#include "../src/progress.h"
#include "../include/doe_metal_shared.h"
#include <assert.h>
#include <stdio.h>

static doe_mtl_commands *commands_new(doe_mtl_context *context) {
    doe_mtl_commands *commands = NULL;
    assert(doe_mtl_commands_create(context, &commands, NULL) == DOE_MTL_OK);
    return commands;
}

static doe_mtl_texture *texture_new(doe_mtl_context *context, bool array) {
    doe_mtl_texture *texture = NULL;
    doe_mtl_texture_desc desc = {array ? DOE_MTL_TEXTURE_2D_ARRAY : DOE_MTL_TEXTURE_2D,
        DOE_MTL_BGRA8_UNORM, array ? 4u : 1u, array ? 4u : 1u, array ? 2u : 1u,
        array ? 2u : 1u, DOE_MTL_TEXTURE_COPY_SRC | DOE_MTL_TEXTURE_COPY_DST |
        DOE_MTL_TEXTURE_RENDER_TARGET | DOE_MTL_TEXTURE_SAMPLED, false, 1};
    assert(doe_mtl_texture_create(context, &desc, &texture, NULL) == DOE_MTL_OK);
    return texture;
}

static doe_mtl_texture_view *view_new(doe_mtl_texture *texture, uint32_t mip, uint32_t layer) {
    doe_mtl_texture_view *view = NULL;
    doe_mtl_texture_view_desc desc = {DOE_MTL_TEXTURE_2D, texture->desc.format, mip, 1, layer, 1,
        DOE_MTL_SWIZZLE_RED, DOE_MTL_SWIZZLE_GREEN, DOE_MTL_SWIZZLE_BLUE, DOE_MTL_SWIZZLE_ALPHA};
    assert(doe_mtl_texture_view_create(texture, &desc, &view, NULL) == DOE_MTL_OK);
    return view;
}

static void render(doe_mtl_commands *commands, doe_mtl_texture_view *view, doe_mtl_load_action load) {
    doe_mtl_render_pass_desc pass = {.color_count = 1};
    pass.colors[0] = (doe_mtl_render_attachment){.view = view, .load = load, .store = DOE_MTL_STORE};
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
}

static doe_mtl_status timeline_signal(void *userdata, void *command, void *event,
    uint64_t value, doe_mtl_error *error) {
    (void)userdata;
    assert(command && event && value);
    return doe_mtl_fail(DOE_MTL_OK, "mock timeline signal", error);
}

static uint64_t timeline_value(void *userdata, void *event) {
    (void)userdata; assert(event);
    return UINT64_MAX; /* Controlled terminal observations always have event proof. */
}

static void producer_then_alpha_pass(void) {
    mock_native mock;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(&mock), &context, NULL) == DOE_MTL_OK);
    /* An owned ledger object stands for the event; no GPU timeline is simulated. */
    assert(context->native.buffer(context->native.userdata, context->device, 1,
        DOE_MTL_SHARED, &context->timeline, NULL) == DOE_MTL_OK);
    context->native.timeline_signal = timeline_signal;
    context->native.timeline_value = timeline_value;
    doe_mtl_texture *texture = texture_new(context, false);
    bool initialized = true;
    assert(doe_mtl_texture_release_initialized(texture, &initialized) == DOE_MTL_OK && !initialized);
    doe_mtl_texture_view *view = view_new(texture, 0, 0);
    doe_mtl_commands *producer = commands_new(context), *alpha = commands_new(context);
    render(producer, view, DOE_MTL_CLEAR);
    assert(doe_mtl_commands_submit(producer, NULL) == DOE_MTL_OK);
    assert(!texture->initialized[0]);
    unsigned renders = mock.renders;
    assert(doe_mtl_commands_initialize_attachment(alpha, view, NULL) == DOE_MTL_OK);
    assert(mock.renders == renders); /* Committed aspect snapshot avoids a redundant clear. */
    render(alpha, view, DOE_MTL_LOAD);
    assert(doe_mtl_commands_submit(alpha, NULL) == DOE_MTL_OK && !mock.waits);
    assert(doe_mtl_texture_release_initialized(texture, &initialized) == DOE_MTL_OK && initialized);
    assert(!texture->initialized[0]); /* Metadata uses the accepted snapshot, not invented completion. */
    void *event = NULL;
    uint64_t value = 0;
    assert(doe_mtl_texture_release_point(texture, &event, &value) == DOE_MTL_OK);
    assert(event == context->timeline && value == alpha->timeline_value && value > producer->timeline_value);
    void *native = texture->native, *native_view = view->native;
    assert(doe_mtl_texture_evict(texture) == DOE_MTL_OK);
    mock_command_result(producer->native, 4, 0);
    assert(doe_mtl_commands_poll(producer, NULL) == DOE_MTL_OK);
    assert(texture->owner == alpha && texture->native == native && view->native == native_view);
    mock_command_result(alpha->native, 4, 0);
    assert(doe_mtl_commands_poll(alpha, NULL) == DOE_MTL_OK);
    assert(!texture->owner && !texture->native && !view->native);
    assert(doe_mtl_texture_release_point(texture, &event, &value) == DOE_MTL_OK);
    assert(value == alpha->timeline_value); /* Retirement cannot erase the exported queue point. */
    assert(doe_mtl_commands_release(alpha) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(producer) == DOE_MTL_OK);
    doe_mtl_texture_view_release(view); doe_mtl_texture_release(texture);
    doe_mtl_context_release(context); mock_assert_closed(&mock);
}

static void mixed_failure_chain(bool fail) {
    mock_native mock;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(&mock), &context, NULL) == DOE_MTL_OK);
    doe_mtl_texture *texture = texture_new(context, false);
    doe_mtl_buffer *source = NULL, *output = NULL;
    assert(doe_mtl_buffer_create(context, 64, DOE_MTL_SHARED, &source, NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_create(context, 64, DOE_MTL_SHARED, &output, NULL) == DOE_MTL_OK);
    doe_mtl_commands *a = commands_new(context), *b = commands_new(context), *c = commands_new(context);
    assert(doe_mtl_commands_fill(a, source, 0, 64, 1, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
    doe_mtl_texture_buffer_layout layout = {0, 256, 1};
    doe_mtl_texture_region region = {0, 0, 0, 0, 1, 1, 1};
    assert(doe_mtl_commands_upload_texture(b, source, layout, texture, region, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(b, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_read_texture(c, texture, region, output, layout, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(c, NULL) == DOE_MTL_OK);
    mock_command_result(b->native, 4, 0); mock_command_result(c->native, 4, 0);
    assert(doe_mtl_commands_poll(c, NULL) == DOE_MTL_PENDING);
    mock_command_result(a->native, 0, 0);
    assert(doe_mtl_commands_poll(c, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_commands_release(c) == DOE_MTL_BUSY && output->owner == c);
    mock_command_result(a->native, fail ? 5u : 4u, fail ? -431 : 0);
    mock_command_result(c->native, 0, 0);
    assert(doe_mtl_commands_poll(c, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(b->state == (fail ? DOE_MTL_FAILED : DOE_MTL_COMPLETE) && output->owner == c);
    unsigned char bytes[4] = {0xa5, 0xa5, 0xa5, 0xa5};
    assert(doe_mtl_buffer_read(output, 0, bytes, sizeof(bytes)) == DOE_MTL_BUSY);
    mock_command_result(c->native, fail ? 5u : 4u, fail ? -99 : 0);
    doe_mtl_error error = {0};
    assert(doe_mtl_commands_poll(c, &error) == (fail ? DOE_MTL_EXECUTION_FAILED : DOE_MTL_OK));
    if (fail) {
        assert(error.native_code == -431 && texture->poisoned && output->poisoned);
        assert(doe_mtl_texture_external_contents(texture, true) == DOE_MTL_EXECUTION_FAILED);
        assert(doe_mtl_buffer_read(output, 0, bytes, sizeof(bytes)) == DOE_MTL_EXECUTION_FAILED);
        assert(bytes[0] == 0xa5);
    } else assert(texture->initialized[0] == DOE_MTL_ASPECT_COLOR);
    assert(doe_mtl_commands_release(c) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK);
    doe_mtl_buffer_release(source); doe_mtl_buffer_release(output); doe_mtl_texture_release(texture);
    doe_mtl_context_release(context); mock_assert_closed(&mock);
}

static void discard_generation(void) {
    mock_native mock;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(&mock), &context, NULL) == DOE_MTL_OK);
    doe_mtl_texture *texture = texture_new(context, true);
    doe_mtl_texture_view *last = view_new(texture, 1, 1), *first = view_new(texture, 0, 0);
    doe_mtl_commands *a = commands_new(context), *b = commands_new(context), *c = commands_new(context);
    assert(doe_mtl_commands_initialize_attachment(a, last, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
    render(b, last, DOE_MTL_LOAD);
    assert(doe_mtl_commands_initialize_attachment(b, first, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(b, NULL) == DOE_MTL_OK);
    assert(doe_mtl_texture_discard_queued(texture) == DOE_MTL_OK);
    bool initialized = true;
    assert(doe_mtl_texture_release_initialized(texture, &initialized) == DOE_MTL_OK && !initialized);
    unsigned renders = mock.renders;
    assert(doe_mtl_commands_initialize_attachment(c, last, NULL) == DOE_MTL_OK);
    assert(mock.renders == renders + 1); /* Old generation cannot lend initialized bits. */
    assert(doe_mtl_commands_submit(c, NULL) == DOE_MTL_OK);
    assert(doe_mtl_texture_release_initialized(texture, &initialized) == DOE_MTL_OK && !initialized);
    mock_command_result(a->native, 4, 0); mock_command_result(b->native, 4, 0);
    mock_command_result(c->native, 4, 0);
    assert(doe_mtl_commands_poll(c, NULL) == DOE_MTL_OK);
    assert(!texture->initialized[0] && !texture->initialized[1] && !texture->initialized[2]);
    assert(texture->initialized[3] == DOE_MTL_ASPECT_COLOR);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(c) == DOE_MTL_OK);
    doe_mtl_texture_view_release(first); doe_mtl_texture_view_release(last); doe_mtl_texture_release(texture);
    doe_mtl_context_release(context); mock_assert_closed(&mock);
}

static void external_hazard_modes(void) {
    for (unsigned mode = 0; mode < 3; ++mode) {
        mock_native mock;
        doe_mtl_native native = mock_driver(&mock);
        mock.untracked_textures = mode == 1;
        if (mode == 2) native.texture_hazards_tracked = NULL;
        doe_mtl_context *context = NULL;
        assert(doe_mtl_context_open(native, &context, NULL) == DOE_MTL_OK);
        doe_mtl_texture_desc desc = {DOE_MTL_TEXTURE_2D, DOE_MTL_BGRA8_UNORM, 1, 1, 1, 1,
            DOE_MTL_TEXTURE_RENDER_TARGET, false, 1};
        void *handle = NULL;
        assert(native.texture(native.userdata, context->device, &desc, &handle, NULL) == DOE_MTL_OK);
        doe_mtl_texture *texture = NULL;
        assert(doe_mtl_texture_adopt_external(context, &desc, handle, &texture, NULL) == DOE_MTL_OK);
        bool initialized = false;
        assert(doe_mtl_texture_release_initialized(texture, &initialized) == DOE_MTL_OK && initialized);
        doe_mtl_texture_view *view = view_new(texture, 0, 0);
        doe_mtl_commands *a = commands_new(context), *b = commands_new(context);
        render(a, view, DOE_MTL_LOAD);
        assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
        doe_mtl_render_pass_desc pass = {.color_count = 1};
        pass.colors[0] = (doe_mtl_render_attachment){.view = view, .load = DOE_MTL_LOAD, .store = DOE_MTL_STORE};
        assert(doe_mtl_commands_render(b, &pass, NULL, 0, NULL) == (mode ? DOE_MTL_BUSY : DOE_MTL_OK));
        assert(!mock.waits);
        assert(doe_mtl_commands_release(b) == DOE_MTL_OK && texture->owner == a);
        mock_command_result(a->native, 0, 0);
        assert(doe_mtl_commands_poll(a, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
        initialized = false;
        assert(doe_mtl_texture_release_initialized(texture, &initialized) == DOE_MTL_BUSY && !initialized);
        b = commands_new(context);
        assert(doe_mtl_commands_render(b, &pass, NULL, 0, NULL) == DOE_MTL_BUSY);
        assert(doe_mtl_commands_release(b) == DOE_MTL_OK);
        mock_command_result(a->native, 4, 0);
        assert(doe_mtl_commands_poll(a, NULL) == DOE_MTL_OK);
        assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
        doe_mtl_texture_view_release(view); doe_mtl_texture_release(texture);
        doe_mtl_context_release(context); mock_assert_closed(&mock);
    }
}

static void pending_depth_stencil_aspects(void) {
    mock_native mock;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(&mock), &context, NULL) == DOE_MTL_OK);
    doe_mtl_texture_desc desc = {DOE_MTL_TEXTURE_2D, DOE_MTL_DEPTH32_STENCIL8,
        4, 4, 1, 1, DOE_MTL_TEXTURE_RENDER_TARGET, false, 1};
    doe_mtl_texture *texture = NULL;
    assert(doe_mtl_texture_create(context, &desc, &texture, NULL) == DOE_MTL_OK);
    doe_mtl_texture_view *view = view_new(texture, 0, 0);
    doe_mtl_commands *a = commands_new(context), *b = commands_new(context);
    doe_mtl_render_pass_desc pass = {0};
    pass.depth = (doe_mtl_render_attachment){.view = view, .load = DOE_MTL_CLEAR, .store = DOE_MTL_STORE};
    pass.stencil = (doe_mtl_render_attachment){.view = view, .load = DOE_MTL_CLEAR, .store = DOE_MTL_STORE_DISCARD};
    assert(doe_mtl_commands_render(a, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_initialize_attachment(b, view, NULL) == DOE_MTL_OK);
    assert(mock.render_pass.depth.load == DOE_MTL_LOAD && mock.render_pass.stencil.load == DOE_MTL_CLEAR);
    assert(!texture->initialized[0]);
    assert(doe_mtl_commands_submit(b, NULL) == DOE_MTL_OK);
    mock_command_result(a->native, 4, 0); mock_command_result(b->native, 4, 0);
    assert(doe_mtl_commands_poll(b, NULL) == DOE_MTL_OK);
    assert(texture->initialized[0] == (DOE_MTL_ASPECT_DEPTH | DOE_MTL_ASPECT_STENCIL));
    assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK);
    doe_mtl_texture_view_release(view); doe_mtl_texture_release(texture);
    doe_mtl_context_release(context); mock_assert_closed(&mock);
}

static void external_epoch_updates(void) {
    mock_native mock;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(&mock), &context, NULL) == DOE_MTL_OK);
    doe_mtl_texture *texture = texture_new(context, true);
    doe_mtl_texture_view *view = view_new(texture, 1, 1);
    assert(doe_mtl_texture_external_contents(texture, false) == DOE_MTL_OK);
    bool initialized = true;
    assert(doe_mtl_texture_release_initialized(texture, &initialized) == DOE_MTL_OK && !initialized);
    assert(doe_mtl_texture_external_contents(texture, true) == DOE_MTL_OK);
    assert(doe_mtl_texture_release_initialized(texture, &initialized) == DOE_MTL_OK && initialized);
    doe_mtl_commands *a = commands_new(context), *b = commands_new(context);
    assert(doe_mtl_commands_initialize_attachment(a, view, NULL) == DOE_MTL_OK && !mock.renders);
    doe_mtl_render_pass_desc pass = {.color_count=1};
    pass.colors[0] = (doe_mtl_render_attachment){.view=view,.load=DOE_MTL_LOAD,.store=DOE_MTL_STORE_DISCARD};
    assert(doe_mtl_commands_render(a, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
    assert(doe_mtl_texture_external_contents(texture, false) == DOE_MTL_OK);
    assert(doe_mtl_texture_external_contents(texture, true) == DOE_MTL_OK);
    uint64_t generation = texture->initialization_generation;
    unsigned renders = mock.renders;
    assert(doe_mtl_commands_initialize_attachment(b, view, NULL) == DOE_MTL_OK);
    assert(mock.renders == renders); /* A valid external producer must not be zeroed. */
    render(b, view, DOE_MTL_LOAD);
    assert(doe_mtl_commands_submit(b, NULL) == DOE_MTL_OK);
    mock_command_result(a->native, 4, 0); mock_command_result(b->native, 4, 0);
    assert(doe_mtl_commands_poll(b, NULL) == DOE_MTL_OK);
    assert(texture->initialization_generation == generation);
    for (size_t i=0;i<texture->subresource_count;++i)
        assert(texture->initialized[i] == DOE_MTL_ASPECT_COLOR);
    assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(b) == DOE_MTL_OK);
    texture->initialization_generation = UINT64_MAX;
    a = commands_new(context);
    render(a, view, DOE_MTL_LOAD);
    assert(doe_mtl_texture_external_contents(texture, false) == DOE_MTL_BUSY);
    assert(doe_mtl_texture_external_contents(texture, true) == DOE_MTL_BUSY);
    assert(doe_mtl_commands_submit(a, NULL) == DOE_MTL_OK);
    assert(doe_mtl_texture_external_contents(texture, false) == DOE_MTL_RANGE);
    assert(doe_mtl_texture_external_contents(texture, true) == DOE_MTL_RANGE);
    mock_command_result(a->native, 0, 0);
    assert(doe_mtl_commands_poll(a, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_texture_external_contents(texture, false) == DOE_MTL_BUSY);
    assert(doe_mtl_texture_external_contents(texture, true) == DOE_MTL_BUSY);
    assert(texture->initialization_generation == UINT64_MAX && texture->owner == a);
    for (size_t i=0;i<texture->subresource_count;++i)
        assert(texture->initialized[i] == DOE_MTL_ASPECT_COLOR);
    mock_command_result(a->native, 4, 0);
    assert(doe_mtl_commands_poll(a, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(a) == DOE_MTL_OK);
    doe_mtl_texture_view_release(view); doe_mtl_texture_release(texture);
    doe_mtl_context_release(context); mock_assert_closed(&mock);
}

int main(void) {
    producer_then_alpha_pass(); mixed_failure_chain(false); mixed_failure_chain(true);
    discard_generation(); external_hazard_modes(); pending_depth_stencil_aspects(); external_epoch_updates();
    puts("PASS Metal ordered texture uses: producer/Load ordering, latest fence, mixed failure/unknown leases, aspect generations, final-use eviction and actual tracked-hazard admission; host ledger only");
    return 0;
}
