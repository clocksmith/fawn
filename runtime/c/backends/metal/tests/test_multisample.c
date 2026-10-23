/* SPDX-License-Identifier: MIT
 * Ownership/call-ledger controls, not sample/pixel simulation. */
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
static doe_mtl_texture_desc descriptor(doe_mtl_format format, uint32_t samples) {
    doe_mtl_texture_desc desc = {samples == 1 ? DOE_MTL_TEXTURE_2D : DOE_MTL_TEXTURE_2D_MS,
        format, 16, 8, 1, 1, DOE_MTL_TEXTURE_RENDER_TARGET | DOE_MTL_TEXTURE_SAMPLED, true, samples};
    return desc;
}
static doe_mtl_texture_view *view_new(doe_mtl_context *context, doe_mtl_format format, uint32_t samples) {
    doe_mtl_texture_desc desc = descriptor(format, samples);
    doe_mtl_texture_view_desc vd = {desc.type, format, 0, 1, 0, 1,
        DOE_MTL_SWIZZLE_RED, DOE_MTL_SWIZZLE_GREEN, DOE_MTL_SWIZZLE_BLUE, DOE_MTL_SWIZZLE_ALPHA};
    doe_mtl_texture *texture = NULL;
    doe_mtl_texture_view *view = NULL;
    assert(doe_mtl_texture_create(context, &desc, &texture, NULL) == DOE_MTL_OK);
    assert(doe_mtl_texture_view_create(texture, &vd, &view, NULL) == DOE_MTL_OK);
    doe_mtl_texture_release(texture);
    return view;
}
static doe_mtl_render_pass_desc color_pass(doe_mtl_texture_view *source, doe_mtl_texture_view *resolve) {
    doe_mtl_render_pass_desc pass = {0};
    pass.color_count = 1;
    pass.colors[0].view = source;
    pass.colors[0].load = DOE_MTL_CLEAR;
    pass.colors[0].store = resolve != NULL ? DOE_MTL_RESOLVE : DOE_MTL_STORE;
    pass.colors[0].resolve_view = resolve;
    return pass;
}
static doe_mtl_render_pass_desc combined_pass(doe_mtl_texture_view *view, doe_mtl_texture_view *resolve) {
    doe_mtl_render_pass_desc pass = {0};
    pass.depth.view = view;
    pass.depth.load = DOE_MTL_CLEAR;
    pass.depth.store = resolve != NULL ? DOE_MTL_RESOLVE : DOE_MTL_STORE;
    pass.depth.clear_depth = 1;
    pass.depth.resolve_view = resolve;
    pass.stencil = pass.depth;
    return pass;
}
static void complete(mock_native *mock, doe_mtl_commands *commands) {
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
}
static void allocation_admission(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_desc desc = descriptor(DOE_MTL_RGBA8_UNORM, 4);
    doe_mtl_texture *out = (void *)&mock;
    const unsigned live = mock.live;
    context->caps.sample_count_mask = 1;
    assert(doe_mtl_texture_create(context, &desc, &out, NULL) == DOE_MTL_UNSUPPORTED);
    context->caps.sample_count_mask = 31;
    desc.sample_count = 3;
    assert(doe_mtl_texture_create(context, &desc, &out, NULL) == DOE_MTL_UNSUPPORTED);
    desc.sample_count = 1;
    assert(doe_mtl_texture_create(context, &desc, &out, NULL) == DOE_MTL_INVALID_ARGUMENT);
    desc.sample_count = 4;
    desc.mip_levels = 2;
    assert(doe_mtl_texture_create(context, &desc, &out, NULL) == DOE_MTL_UNSUPPORTED);
    desc.mip_levels = 1;
    desc.usage |= DOE_MTL_TEXTURE_COPY_SRC;
    assert(doe_mtl_texture_create(context, &desc, &out, NULL) == DOE_MTL_UNSUPPORTED);
    desc = descriptor(DOE_MTL_DEPTH24_STENCIL8, 1);
    context->caps.depth24_stencil8 = false;
    assert(doe_mtl_texture_create(context, &desc, &out, NULL) == DOE_MTL_UNSUPPORTED);
    desc = descriptor(DOE_MTL_X32_STENCIL8, 1);
    assert(doe_mtl_texture_create(context, &desc, &out, NULL) == DOE_MTL_UNSUPPORTED);
    desc = descriptor(DOE_MTL_STENCIL8, 1);
    context->caps.separate_depth_stencil = false;
    assert(doe_mtl_texture_create(context, &desc, &out, NULL) == DOE_MTL_UNSUPPORTED);
    desc = descriptor(DOE_MTL_RGBA8_UNORM, 4);
    desc.type = DOE_MTL_TEXTURE_2D_MS_ARRAY;
    desc.depth_or_layers = 2;
    context->caps.multisample_array = false;
    assert(doe_mtl_texture_create(context, &desc, &out, NULL) == DOE_MTL_UNSUPPORTED);
    assert(out == (void *)&mock && mock.live == live);
    context->caps.multisample_array = true;
    assert(doe_mtl_texture_create(context, &desc, &out, NULL) == DOE_MTL_OK);
    assert(mock.texture_desc.sample_count == 4 && mock.texture_desc.depth_or_layers == 2);
    doe_mtl_texture_release(out);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void independent_aspects(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_DEPTH32_STENCIL8, 1), *stencil = NULL;
    doe_mtl_texture_view_desc vd = view->desc;
    doe_mtl_render_pass_desc pass = combined_pass(view, NULL);
    doe_mtl_commands *commands = commands_new(context);
    doe_mtl_native_resources resources = {0};
    doe_mtl_texture_binding binding = {0, view, false};
    vd.format = DOE_MTL_X32_STENCIL8;
    assert(doe_mtl_texture_view_create(view->texture, &vd, &stencil, NULL) == DOE_MTL_OK);
    pass.depth.store = DOE_MTL_STORE_DISCARD;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    assert(!doe_mtl_texture_aspect_initialized(commands, view, DOE_MTL_ASPECT_DEPTH));
    assert(doe_mtl_texture_aspect_initialized(commands, view, DOE_MTL_ASPECT_STENCIL));
    assert(view->texture->initialized[0] == 0);
    assert(doe_mtl_image_bindings_preflight(commands, &binding, 1, NULL, 0, &resources) == DOE_MTL_UNINITIALIZED);
    binding.view = stencil;
    assert(doe_mtl_image_bindings_preflight(commands, &binding, 1, NULL, 0, &resources) == DOE_MTL_OK);
    complete(&mock, commands);
    assert(view->texture->initialized[0] == DOE_MTL_ASPECT_STENCIL);
    commands = commands_new(context);
    pass.depth.load = DOE_MTL_LOAD;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_UNINITIALIZED);
    assert(mock.renders == 1);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context);
    pass.depth.load = DOE_MTL_CLEAR;
    pass.depth.store = DOE_MTL_STORE;
    pass.stencil.load = DOE_MTL_LOAD;
    pass.stencil.store = DOE_MTL_STORE_DISCARD;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    assert(view->texture->initialized[0] == DOE_MTL_ASPECT_DEPTH);
    commands = commands_new(context);
    memset(&resources, 0, sizeof(resources));
    binding.view = view;
    assert(doe_mtl_image_bindings_preflight(commands, &binding, 1, NULL, 0, &resources) == DOE_MTL_OK);
    memset(&resources, 0, sizeof(resources));
    binding.view = stencil;
    assert(doe_mtl_image_bindings_preflight(commands, &binding, 1, NULL, 0, &resources) == DOE_MTL_UNINITIALIZED);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_texture_view_release(stencil);
    doe_mtl_texture_view_release(view);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void color_resolve(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *source = view_new(context, DOE_MTL_RGBA8_UNORM, 4);
    doe_mtl_texture_view *target = view_new(context, DOE_MTL_RGBA8_UNORM, 1);
    doe_mtl_render_pass_desc pass = color_pass(source, target);
    doe_mtl_commands *commands = commands_new(context);
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    assert(mock.render_pass.samples == 4 && mock.render_pass.colors[0].resolve == target->native);
    assert(!doe_mtl_texture_aspect_initialized(commands, source, DOE_MTL_ASPECT_COLOR));
    assert(doe_mtl_texture_aspect_initialized(commands, target, DOE_MTL_ASPECT_COLOR));
    assert(target->texture->initialized[0] == 0);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    assert(target->texture->initialized[0] == 0);
    commands = commands_new(context);
    pass.colors[0].store = DOE_MTL_STORE_RESOLVE;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    assert(source->texture->initialized[0] == 1 && target->texture->initialized[0] == 1);
    commands = commands_new(context);
    pass.colors[0].load = DOE_MTL_LOAD_DISCARD;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    assert(source->texture->initialized[0] == 0 && target->texture->initialized[0] == 0);
    doe_mtl_texture_view_release(source);
    doe_mtl_texture_view_release(target);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void combined_resolve_lifetime(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *source = view_new(context, DOE_MTL_DEPTH32_STENCIL8, 4);
    doe_mtl_texture_view *target = view_new(context, DOE_MTL_DEPTH32_STENCIL8, 1), *alias = NULL;
    doe_mtl_render_pass_desc pass = combined_pass(source, target);
    doe_mtl_commands *commands = commands_new(context);
    assert(doe_mtl_texture_view_create(source->texture, &source->desc, &alias, NULL) == DOE_MTL_OK);
    pass.stencil.view = alias;
    pass.depth.resolve_filter = 1;
    pass.stencil.resolve_filter = 1;
    pass.stencil.store = DOE_MTL_STORE_RESOLVE;
    context->caps.separate_depth_stencil = false;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    assert(mock.render_pass.depth.texture == mock.render_pass.stencil.texture);
    assert(mock.render_pass.depth.resolve == target->native && mock.render_pass.stencil.resolve == target->native);
    assert(mock.render_pass.depth.resolve_filter == 1 && mock.render_pass.stencil.resolve_filter == 1);
    assert(doe_mtl_texture_aspect_initialized(commands, source, DOE_MTL_ASPECT_STENCIL));
    assert(!doe_mtl_texture_aspect_initialized(commands, source, DOE_MTL_ASPECT_DEPTH));
    complete(&mock, commands);
    assert(source->texture->initialized[0] == DOE_MTL_ASPECT_STENCIL);
    assert(target->texture->initialized[0] == (DOE_MTL_ASPECT_DEPTH | DOE_MTL_ASPECT_STENCIL));
    commands = commands_new(context);
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    doe_mtl_texture_view_release(source);
    doe_mtl_texture_view_release(alias);
    doe_mtl_texture_view_release(target);
    doe_mtl_context_release(context);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY && mock.closes == 0);
    mock_result(&mock, 5, -991);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_EXECUTION_FAILED);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    mock_assert_closed(&mock);
}
static void rejected_resolves(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *source = view_new(context, DOE_MTL_RGBA8_UNORM, 4);
    doe_mtl_texture_view *other = view_new(context, DOE_MTL_RGBA8_UNORM, 4);
    doe_mtl_texture_view *target = view_new(context, DOE_MTL_RGBA8_UNORM, 1);
    doe_mtl_render_pass_desc pass = color_pass(source, target);
    doe_mtl_commands *commands = commands_new(context);
    pass.color_count = 2;
    pass.colors[1] = pass.colors[0];
    pass.colors[1].view = other;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context);
    pass.color_count = 1;
    pass.colors[0].store = DOE_MTL_STORE_RESOLVE;
    context->caps.store_and_resolve = false;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_UNSUPPORTED);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context);
    pass.colors[0].store = DOE_MTL_STORE;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    assert(mock.renders == 0 && source->texture->owner == NULL && target->texture->owner == NULL);
    doe_mtl_texture_view_release(source);
    doe_mtl_texture_view_release(other);
    doe_mtl_texture_view_release(target);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void sample_pipeline_match(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_RGBA8_UNORM, 4);
    doe_mtl_render_pass_desc pass = color_pass(view, NULL);
    doe_mtl_render_pipeline_desc desc = {0};
    doe_mtl_render_pipeline *pipeline = NULL;
    doe_mtl_render_draw draw = {0};
    doe_mtl_commands *commands = commands_new(context);
    desc.vertex = (doe_mtl_shader_stage){"v", 1, "v", UINT32_MAX};
    desc.fragment = (doe_mtl_shader_stage){"f", 1, "f", UINT32_MAX};
    desc.color_count = 1;
    desc.colors[0].format = DOE_MTL_RGBA8_UNORM;
    desc.colors[0].write_mask = 15;
    desc.sample_count = 1;
    assert(doe_mtl_render_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_OK);
    draw.pipeline = pipeline;
    draw.count = 3;
    draw.instance_count = 1;
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(mock.renders == 0 && view->texture->owner == NULL);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_render_pipeline_release(pipeline);
    desc.sample_count = 4;
    assert(doe_mtl_render_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_OK);
    assert(mock.render_pipeline_desc.sample_count == 4);
    draw.pipeline = pipeline;
    commands = commands_new(context);
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    doe_mtl_render_pipeline_release(pipeline);
    doe_mtl_texture_view_release(view);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
int main(void) {
    sample_pipeline_match();
    allocation_admission();
    independent_aspects();
    color_resolve();
    combined_resolve_lifetime();
    rejected_resolves();
    puts("Metal multisample controls: 6 ownership/aspect/resolve groups passed; no sample or pixel execution");
    return 0;
}
