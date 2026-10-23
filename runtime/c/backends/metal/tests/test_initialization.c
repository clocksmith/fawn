/* SPDX-License-Identifier: MIT
 * Attachment aspect/recording controls at real native-call boundaries, no GPU. */
#include "mock_native.h"
#include "../src/render_internal.h"
#include <assert.h>
#include <stdio.h>

static doe_mtl_context *context_new(mock_native *mock) {
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(mock), &context, NULL) == DOE_MTL_OK);
    return context;
}
static doe_mtl_commands *commands_new(doe_mtl_context *context) {
    doe_mtl_commands *commands = NULL;
    assert(doe_mtl_commands_create(context, &commands, NULL) == DOE_MTL_OK);
    return commands;
}
static doe_mtl_texture_view *view_new(doe_mtl_context *context, doe_mtl_format format,
    uint32_t mips, uint32_t layers) {
    doe_mtl_texture *texture = NULL;
    doe_mtl_texture_view *view = NULL;
    doe_mtl_texture_desc desc = {DOE_MTL_TEXTURE_2D_ARRAY, format, 16, 8, layers, mips,
        DOE_MTL_TEXTURE_RENDER_TARGET, false, 1};
    doe_mtl_texture_view_desc span = {DOE_MTL_TEXTURE_2D_ARRAY, format, 0, mips, 0, layers,
        DOE_MTL_SWIZZLE_RED, DOE_MTL_SWIZZLE_GREEN, DOE_MTL_SWIZZLE_BLUE, DOE_MTL_SWIZZLE_ALPHA};
    assert(doe_mtl_texture_create(context, &desc, &texture, NULL) == DOE_MTL_OK);
    assert(doe_mtl_texture_view_create(texture, &span, &view, NULL) == DOE_MTL_OK);
    doe_mtl_texture_release(texture);
    return view;
}
static void complete(mock_native *mock, doe_mtl_commands *commands) {
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
}

static void speculative_and_pending(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_RGBA8_UNORM, 1, 1);
    doe_mtl_commands *commands = commands_new(context);
    assert(doe_mtl_commands_initialize_attachment(commands, view, NULL) == DOE_MTL_OK);
    assert(mock.renders == 1 && mock.render_pass.colors[0].load == DOE_MTL_CLEAR);
    assert(mock.render_pass.colors[0].color.alpha == 0);
    assert(doe_mtl_texture_aspect_initialized(commands, view, DOE_MTL_ASPECT_COLOR));
    assert(view->texture->initialized[0] == 0);
    assert(doe_mtl_commands_initialize_attachment(commands, view, NULL) == DOE_MTL_OK && mock.renders == 1);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    assert(view->texture->initialized[0] == 0); /* Abandon never publishes. */
    commands = commands_new(context);
    assert(doe_mtl_commands_initialize_attachment(commands, view, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(&mock, 0, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(view->texture->initialized[0] == 0 && doe_mtl_commands_release(commands) == DOE_MTL_BUSY);
    doe_mtl_commands *other = commands_new(context);
    assert(doe_mtl_commands_initialize_attachment(other, view, NULL) == DOE_MTL_BUSY);
    assert(doe_mtl_commands_release(other) == DOE_MTL_OK);
    /* The second native command was never submitted; select the original one. */
    mock.last_command = commands->native;
    mock_result(&mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(view->texture->initialized[0] == DOE_MTL_ASPECT_COLOR);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_texture_view_release(view);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}

static void combined_discard_preserves_other_aspect(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_DEPTH32_STENCIL8, 1, 1);
    doe_mtl_commands *commands = commands_new(context);
    assert(doe_mtl_commands_initialize_attachment(commands, view, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    commands = commands_new(context);
    doe_mtl_render_pass_desc pass = {0};
    pass.depth = (doe_mtl_render_attachment){.view = view, .load = DOE_MTL_LOAD, .store = DOE_MTL_STORE_DISCARD};
    pass.stencil = (doe_mtl_render_attachment){.view = view, .load = DOE_MTL_CLEAR,
        .store = DOE_MTL_STORE, .clear_stencil = 157};
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    assert(view->texture->initialized[0] == DOE_MTL_ASPECT_STENCIL);
    commands = commands_new(context);
    assert(doe_mtl_commands_initialize_attachment(commands, view, NULL) == DOE_MTL_OK);
    assert(mock.render_pass.depth.load == DOE_MTL_CLEAR && mock.render_pass.depth.depth == 0);
    assert(mock.render_pass.stencil.load == DOE_MTL_LOAD && mock.render_pass.stencil.store == DOE_MTL_STORE);
    assert(view->texture->initialized[0] == DOE_MTL_ASPECT_STENCIL);
    complete(&mock, commands);
    assert(view->texture->initialized[0] == (DOE_MTL_ASPECT_DEPTH | DOE_MTL_ASPECT_STENCIL));
    doe_mtl_texture_view_release(view);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}

static void ranges_and_preparation_failure(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *all = view_new(context, DOE_MTL_RGBA8_UNORM, 2, 2), *one = NULL;
    doe_mtl_texture_view_desc selected = all->desc;
    selected.base_layer = 1;
    selected.mip_count = selected.layer_count = 1;
    assert(doe_mtl_texture_view_create(all->texture, &selected, &one, NULL) == DOE_MTL_OK);
    doe_mtl_commands *commands = commands_new(context);
    assert(doe_mtl_commands_initialize_attachment(commands, one, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    unsigned before = mock.renders;
    commands = commands_new(context);
    mock.fail_view_at = mock.views + 2;
    doe_mtl_error error = {0};
    assert(doe_mtl_commands_initialize_attachment(commands, all, &error) == DOE_MTL_NATIVE_FAILURE);
    assert(error.native_code == -71 && mock.renders == before);
    assert(!all->texture->owner && all->texture->initialized[1] == DOE_MTL_ASPECT_COLOR);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    mock.fail_view_at = 0;
    commands = commands_new(context);
    assert(doe_mtl_commands_initialize_attachment(commands, all, NULL) == DOE_MTL_OK);
    assert(mock.renders == before + 3); /* The initialized selected layer was loaded unchanged. */
    assert(all->texture->initialized[0] == 0 && all->texture->initialized[1] == DOE_MTL_ASPECT_COLOR);
    complete(&mock, commands);
    for (size_t i = 0; i < 4; ++i) assert(all->texture->initialized[i] == DOE_MTL_ASPECT_COLOR);
    doe_mtl_texture_view_release(one);
    doe_mtl_texture_view_release(all);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}

static void terminal_failure_and_early_release(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_RGBA8_UNORM, 1, 1);
    doe_mtl_commands *commands = commands_new(context);
    assert(doe_mtl_commands_initialize_attachment(commands, view, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(&mock, 5, -91);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_EXECUTION_FAILED);
    assert(view->texture->poisoned && view->texture->initialized[0] == 0);
    doe_mtl_texture_view_release(view);
    doe_mtl_context_release(context);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    mock_assert_closed(&mock);
}

int main(void) {
    speculative_and_pending();
    combined_discard_preserves_other_aspect();
    ranges_and_preparation_failure();
    terminal_failure_and_early_release();
    puts("PASS Metal attachment initialization: selected ranges, missing aspects, discard preservation, preflight, speculative/unknown/failure ownership");
}
