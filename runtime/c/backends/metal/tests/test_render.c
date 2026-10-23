/* SPDX-License-Identifier: MIT
 * Graphics command ownership/call controls, never rasterized output. */
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
static doe_mtl_buffer *buffer_new(doe_mtl_context *context) {
    doe_mtl_buffer *buffer = NULL;
    assert(doe_mtl_buffer_create(context, 1024, DOE_MTL_SHARED, &buffer, NULL) == DOE_MTL_OK);
    return buffer;
}
static doe_mtl_texture_view *view_new(doe_mtl_context *context, doe_mtl_format format) {
    doe_mtl_texture_desc desc = {DOE_MTL_TEXTURE_2D, format, 16, 8, 1, 1,
        DOE_MTL_TEXTURE_RENDER_TARGET | DOE_MTL_TEXTURE_SAMPLED, false, 1};
    doe_mtl_texture_view_desc vd = {DOE_MTL_TEXTURE_2D, format, 0, 1, 0, 1,
        DOE_MTL_SWIZZLE_RED, DOE_MTL_SWIZZLE_GREEN, DOE_MTL_SWIZZLE_BLUE, DOE_MTL_SWIZZLE_ALPHA};
    doe_mtl_texture *texture = NULL;
    doe_mtl_texture_view *view = NULL;
    assert(doe_mtl_texture_create(context, &desc, &texture, NULL) == DOE_MTL_OK);
    assert(doe_mtl_texture_view_create(texture, &vd, &view, NULL) == DOE_MTL_OK);
    doe_mtl_texture_release(texture);
    return view;
}
static doe_mtl_render_pipeline_desc pipeline_desc(void) {
    doe_mtl_render_pipeline_desc desc = {0};
    desc.vertex = (doe_mtl_shader_stage){"vertex", 6, "v_main", UINT32_MAX};
    desc.fragment = (doe_mtl_shader_stage){"fragment", 8, "f_main", UINT32_MAX};
    desc.color_count = 1;
    desc.colors[0].format = DOE_MTL_RGBA8_UNORM;
    desc.colors[0].write_mask = 15;
    desc.sample_count = 1;
    return desc;
}
static doe_mtl_render_pipeline *pipeline_new(doe_mtl_context *context, doe_mtl_render_pipeline_desc desc) {
    doe_mtl_render_pipeline *pipeline = NULL;
    assert(doe_mtl_render_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_OK);
    return pipeline;
}
static doe_mtl_render_pass_desc pass_new(doe_mtl_texture_view *view) {
    doe_mtl_render_pass_desc pass = {0};
    pass.color_count = 1;
    pass.colors[0].view = view;
    pass.colors[0].load = DOE_MTL_CLEAR;
    pass.colors[0].store = DOE_MTL_STORE;
    pass.colors[0].clear_color = (doe_mtl_clear_color){0.25, 0.5, 0.75, 1};
    return pass;
}
static doe_mtl_render_draw draw_new(doe_mtl_render_pipeline *pipeline) {
    doe_mtl_render_draw draw = {0};
    draw.pipeline = pipeline;
    draw.topology = 3;
    draw.count = 3;
    draw.instance_count = 1;
    return draw;
}
static void complete(mock_native *mock, doe_mtl_commands *commands) {
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
}
static void constructors(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_render_pipeline_desc desc = pipeline_desc();
    doe_mtl_render_pipeline *pipeline = (void *)&mock;
    doe_mtl_vertex_layout layout = {5, 16, 1};
    doe_mtl_vertex_attribute attribute = {2, 5, 0, 31};
    unsigned live = mock.live;
    mock.fail = MOCK_RENDER_PIPELINE;
    assert(doe_mtl_render_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(pipeline == (void *)&mock && mock.live == live && context->references == 1);
    desc.depth_stencil.depth_format = DOE_MTL_DEPTH32_FLOAT;
    mock.fail = MOCK_DEPTH_STATE;
    assert(doe_mtl_render_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(mock.live == live && context->references == 1);
    mock.fail = MOCK_NONE; desc.layouts = &layout; desc.layout_count = 1;
    desc.attributes = &attribute; desc.attribute_count = 1;
    pipeline = pipeline_new(context, desc);
    layout.stride = 512; attribute.offset = 128;
    assert(pipeline->layouts[0].stride == 16 && pipeline->attributes[0].offset == 0);
    assert(pipeline->desc.vertex.msl == NULL && pipeline->desc.fragment.entrypoint == NULL);
    doe_mtl_render_pipeline_release(pipeline);
    desc = pipeline_desc(); desc.sample_count = 3;
    assert(doe_mtl_render_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_UNSUPPORTED);
    desc = pipeline_desc(); desc.fragment.msl = NULL;
    assert(doe_mtl_render_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_INVALID_ARGUMENT);
    desc = pipeline_desc(); desc.colors[0].color.source = 18;
    assert(doe_mtl_render_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_INVALID_ARGUMENT);
    doe_mtl_context_release(context); mock_assert_closed(&mock);
}
static void clear_load_store(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_RGBA8_UNORM);
    doe_mtl_render_pass_desc pass = pass_new(view);
    doe_mtl_commands *commands = commands_new(context);
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    assert(mock.render_draw_count == 0 && mock.render_pass.width == 16 && !view->texture->initialized[0]);
    assert(mock.render_pass.colors[0].texture == view->native && mock.render_pass.colors[0].color.blue == 0.75);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK && !view->texture->initialized[0]);
    commands = commands_new(context); pass.colors[0].load = DOE_MTL_LOAD;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_UNINITIALIZED);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context); pass.colors[0].load = DOE_MTL_CLEAR;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    pass.colors[0].load = DOE_MTL_LOAD;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    complete(&mock, commands); assert(view->texture->initialized[0]);
    commands = commands_new(context); pass.colors[0].store = DOE_MTL_STORE_DISCARD;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    complete(&mock, commands); assert(!view->texture->initialized[0]);
    commands = commands_new(context); pass.colors[0].load = DOE_MTL_LOAD_DISCARD; pass.colors[0].store = DOE_MTL_STORE;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    complete(&mock, commands); assert(!view->texture->initialized[0]);
    doe_mtl_texture_view_release(view); doe_mtl_context_release(context); mock_assert_closed(&mock);
}
static void mixed_draw_lifetime(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_RGBA8_UNORM);
    doe_mtl_render_pass_desc pass = pass_new(view);
    doe_mtl_render_pipeline *pipeline = pipeline_new(context, pipeline_desc());
    doe_mtl_buffer *buffer = buffer_new(context);
    doe_mtl_buffer_binding binding = {4, buffer, 16, 256};
    doe_mtl_render_draw draws[2] = {draw_new(pipeline), draw_new(pipeline)};
    doe_mtl_commands *commands = commands_new(context);
    draws[0].vertex = (doe_mtl_render_bindings){.buffers = &binding, .buffer_count = 1};
    draws[0].count = 7; draws[0].first_vertex = 2; draws[0].first_instance = 3;
    draws[1].index_buffer = buffer; draws[1].index_offset = 8; draws[1].index_type = 1; draws[1].base_vertex = -2;
    assert(doe_mtl_commands_render(commands, &pass, draws, 2, NULL) == DOE_MTL_OK);
    assert(mock.render_draws[0].vertex.buffers[4].buffer == buffer->native);
    assert(mock.render_draws[0].vertex.buffers[4].offset == 16 && mock.render_draws[0].values.first_vertex == 2);
    assert(mock.render_draws[1].index_buffer == buffer->native && mock.render_draws[1].values.base_vertex == -2);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    doe_mtl_render_pipeline_release(pipeline); doe_mtl_texture_view_release(view);
    doe_mtl_buffer_release(buffer); doe_mtl_context_release(context);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY && mock.closes == 0);
    mock_result(&mock, 1, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    mock_result(&mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK); mock_assert_closed(&mock);
}
static void depth_stencil(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *depth = view_new(context, DOE_MTL_DEPTH32_FLOAT);
    doe_mtl_texture_view *stencil = view_new(context, DOE_MTL_STENCIL8);
    doe_mtl_render_pipeline_desc desc = pipeline_desc();
    doe_mtl_render_pipeline *pipeline;
    doe_mtl_render_pass_desc pass = {0};
    doe_mtl_render_draw draw;
    doe_mtl_commands *commands = commands_new(context);
    desc.color_count = 0; desc.fragment = (doe_mtl_shader_stage){0};
    desc.depth_stencil.depth_format = DOE_MTL_DEPTH32_FLOAT;
    desc.depth_stencil.stencil_format = DOE_MTL_STENCIL8;
    desc.depth_stencil.depth_compare = 3; desc.depth_stencil.depth_write = true;
    pipeline = pipeline_new(context, desc); draw = draw_new(pipeline);
    pass.depth.view = depth; pass.depth.load = DOE_MTL_CLEAR; pass.depth.store = DOE_MTL_STORE; pass.depth.clear_depth = 1;
    pass.stencil.view = stencil; pass.stencil.load = DOE_MTL_CLEAR; pass.stencil.store = DOE_MTL_STORE; pass.stencil.clear_stencil = 0xa5;
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_OK);
    assert(mock.render_draws[0].depth_stencil != NULL && mock.render_draws[0].fragment_sizes_slot == UINT32_MAX);
    assert(mock.render_pass.depth.depth == 1 && mock.render_pass.stencil.stencil == 0xa5);
    complete(&mock, commands); assert(depth->texture->initialized[0] && stencil->texture->initialized[0]);
    commands = commands_new(context);
    pass.stencil.view = depth;
    pass.depth.store = DOE_MTL_STORE_DISCARD;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_UNSUPPORTED);
    assert(mock.renders == 1 && depth->texture->owner == NULL);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_render_pipeline_release(pipeline); doe_mtl_texture_view_release(depth);
    doe_mtl_texture_view_release(stencil); doe_mtl_context_release(context); mock_assert_closed(&mock);
}
static void preflight_atomicity(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_RGBA8_UNORM);
    doe_mtl_render_pass_desc pass = pass_new(view);
    doe_mtl_render_pipeline *pipeline = pipeline_new(context, pipeline_desc());
    doe_mtl_render_draw draws[2] = {draw_new(pipeline), draw_new(pipeline)};
    doe_mtl_commands *commands = commands_new(context);
    draws[1].scissor_enabled = true; draws[1].scissor = (doe_mtl_scissor){15, 0, 2, 1};
    assert(doe_mtl_commands_render(commands, &pass, draws, 2, NULL) == DOE_MTL_RANGE);
    assert(mock.renders == 0 && view->texture->owner == NULL && pipeline->references == 1);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context); pass.colors[1] = pass.colors[0]; pass.color_count = 2;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_INVALID_ARGUMENT && mock.renders == 0);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_render_pipeline_release(pipeline); doe_mtl_texture_view_release(view);
    doe_mtl_context_release(context); mock_assert_closed(&mock);
}
static void fetch_bounds_and_faults(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_RGBA8_UNORM);
    doe_mtl_render_pass_desc pass = pass_new(view);
    doe_mtl_render_pipeline_desc desc = pipeline_desc();
    doe_mtl_vertex_layout layout = {3, 16, 1};
    doe_mtl_vertex_attribute attribute = {0, 3, 0, 31};
    doe_mtl_render_pipeline *pipeline;
    doe_mtl_buffer *buffer = buffer_new(context);
    doe_mtl_buffer_binding binding = {3, buffer, 0, 32};
    doe_mtl_render_draw draw;
    doe_mtl_commands *commands = commands_new(context);
    desc.layouts = &layout; desc.layout_count = 1; desc.attributes = &attribute; desc.attribute_count = 1;
    pipeline = pipeline_new(context, desc); draw = draw_new(pipeline);
    draw.vertex = (doe_mtl_render_bindings){.buffers = &binding, .buffer_count = 1};
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_RANGE);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK && mock.renders == 0);
    binding.size = 64; commands = commands_new(context); mock.fail = MOCK_RENDER;
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_NATIVE_FAILURE && mock.commits == 0);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK && !view->texture->poisoned);
    mock.fail = MOCK_NONE; commands = commands_new(context);
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK); mock_result(&mock, 5, -9);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_EXECUTION_FAILED);
    assert(view->texture->poisoned && buffer->poisoned);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_render_pipeline_release(pipeline); doe_mtl_buffer_release(buffer);
    doe_mtl_texture_view_release(view); doe_mtl_context_release(context); mock_assert_closed(&mock);
}
static void aliases_devices_and_leases(void) {
    mock_native mock, foreign;
    doe_mtl_context *context = context_new(&mock), *other_context = context_new(&foreign);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_RGBA8_UNORM);
    doe_mtl_render_pass_desc pass = pass_new(view);
    doe_mtl_render_pipeline *pipeline = pipeline_new(context, pipeline_desc());
    doe_mtl_render_pipeline *other_pipeline = pipeline_new(other_context, pipeline_desc());
    doe_mtl_render_draw draw = draw_new(pipeline);
    doe_mtl_texture_binding texture = {7, view, false};
    doe_mtl_commands *commands = commands_new(context), *other;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    other = commands_new(context);
    assert(doe_mtl_commands_render(other, &pass, NULL, 0, NULL) == DOE_MTL_BUSY);
    assert(doe_mtl_commands_release(other) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context);
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_OK);
    complete(&mock, commands);
    commands = commands_new(context); draw.fragment.textures = &texture; draw.fragment.texture_count = 1;
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK && view->texture->owner == NULL);
    draw = draw_new(other_pipeline); commands = commands_new(context);
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_DEVICE_MISMATCH);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    draw = draw_new(pipeline); pipeline->references = UINT32_MAX; commands = commands_new(context);
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_RANGE);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK && view->texture->owner == NULL);
    pipeline->references = 1;
    doe_mtl_render_pipeline_release(pipeline); doe_mtl_render_pipeline_release(other_pipeline);
    doe_mtl_texture_view_release(view); doe_mtl_context_release(context); doe_mtl_context_release(other_context);
    mock_assert_closed(&mock); mock_assert_closed(&foreign);
}
static void index_and_attachment_ranges(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_RGBA8_UNORM);
    doe_mtl_render_pass_desc pass = pass_new(view);
    doe_mtl_render_pipeline *pipeline = pipeline_new(context, pipeline_desc());
    doe_mtl_render_draw draw = draw_new(pipeline);
    doe_mtl_buffer *buffer = buffer_new(context);
    doe_mtl_commands *commands = commands_new(context);
    draw.index_buffer = buffer; draw.index_type = 1; draw.index_offset = 1020;
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_RANGE);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context); draw.index_offset = 2;
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_ALIGNMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = commands_new(context); draw.index_offset = 0; draw.first_vertex = 3;
    assert(doe_mtl_commands_render(commands, &pass, &draw, 1, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK && mock.renders == 0);
    commands = commands_new(context); pass.colors[0].view = NULL;
    assert(doe_mtl_commands_render(commands, &pass, NULL, 0, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_render_pipeline_release(pipeline); doe_mtl_buffer_release(buffer);
    doe_mtl_texture_view_release(view); doe_mtl_context_release(context); mock_assert_closed(&mock);
}
static void coverage_and_empty_raster(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_render_pipeline_desc desc = pipeline_desc();
    doe_mtl_render_pipeline *pipeline = (void *)&mock;
    desc.sample_count = 4;
    desc.alpha_to_coverage = true;
    desc.override_sample_mask = true;
    desc.sample_mask = 15;
    pipeline = pipeline_new(context, desc);
    assert(mock.render_pipeline_desc.alpha_to_coverage && mock.render_pipeline_desc.sample_mask == 15);
    doe_mtl_render_pipeline_release(pipeline);
    unsigned created = mock.created;
    pipeline = (void *)&mock;
    desc.sample_mask = 5;
    assert(doe_mtl_render_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_UNSUPPORTED);
    assert(pipeline == (void *)&mock && mock.created == created);
    desc.sample_mask = UINT32_MAX;
    desc.sample_count = 1;
    assert(doe_mtl_render_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(pipeline == (void *)&mock && mock.created == created);
    desc.alpha_to_coverage = false;
    pipeline = pipeline_new(context, desc);
    doe_mtl_texture_view *view = view_new(context, DOE_MTL_RGBA8_UNORM);
    doe_mtl_render_pass_desc pass = pass_new(view);
    doe_mtl_render_draw draws[2] = {draw_new(pipeline), draw_new(pipeline)};
    draws[0].viewport_enabled = true;
    draws[0].viewport = (doe_mtl_viewport){16, 0, 0, 8, 0, 1};
    draws[1].scissor_enabled = true;
    draws[1].scissor = (doe_mtl_scissor){0, 8, 16, 0};
    doe_mtl_commands *commands = commands_new(context);
    assert(doe_mtl_commands_render(commands, &pass, draws, 2, NULL) == DOE_MTL_OK);
    assert(mock.render_draw_count == 2 && mock.render_draws[0].values.viewport.width == 0);
    assert(mock.render_draws[1].values.scissor.height == 0);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_texture_view_release(view);
    doe_mtl_render_pipeline_release(pipeline);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
int main(void) {
    coverage_and_empty_raster();
    constructors(); clear_load_store(); mixed_draw_lifetime(); depth_stencil();
    preflight_atomicity(); fetch_bounds_and_faults();
    aliases_devices_and_leases(); index_and_attachment_ranges();
    puts("Metal render ownership controls: 9 groups passed; no shader/raster/GPU execution");
    return 0;
}
