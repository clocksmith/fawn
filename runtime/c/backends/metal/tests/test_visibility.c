/* SPDX-License-Identifier: MIT
 * Visibility buffer lifetime/call admission only, never synthesized query results. */
#include "mock_native.h"
#include "../src/render_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct fixture {
    mock_native mock;
    doe_mtl_context *context;
    doe_mtl_texture_view *view;
    doe_mtl_buffer *results;
    doe_mtl_render_pipeline *pipeline;
    doe_mtl_render_pass_desc pass;
    doe_mtl_render_draw draws[4];
} fixture;
static void initialize(fixture *f) {
    doe_mtl_texture_desc td = {DOE_MTL_TEXTURE_2D, DOE_MTL_RGBA8_UNORM, 8, 8, 1, 1,
        DOE_MTL_TEXTURE_RENDER_TARGET, false, 1};
    doe_mtl_texture_view_desc vd = {DOE_MTL_TEXTURE_2D, DOE_MTL_RGBA8_UNORM, 0, 1, 0, 1,
        DOE_MTL_SWIZZLE_RED, DOE_MTL_SWIZZLE_GREEN, DOE_MTL_SWIZZLE_BLUE, DOE_MTL_SWIZZLE_ALPHA};
    doe_mtl_render_pipeline_desc pd = {0};
    doe_mtl_texture *texture = NULL;
    unsigned i;
    memset(f, 0, sizeof(*f));
    assert(doe_mtl_context_open(mock_driver(&f->mock), &f->context, NULL) == DOE_MTL_OK);
    assert(doe_mtl_texture_create(f->context, &td, &texture, NULL) == DOE_MTL_OK);
    assert(doe_mtl_texture_view_create(texture, &vd, &f->view, NULL) == DOE_MTL_OK);
    doe_mtl_texture_release(texture);
    assert(doe_mtl_buffer_create(f->context, 64, DOE_MTL_SHARED, &f->results, NULL) == DOE_MTL_OK);
    pd.vertex = (doe_mtl_shader_stage){"vertex source", 13, "v", UINT32_MAX};
    pd.fragment = (doe_mtl_shader_stage){"fragment source", 15, "f", UINT32_MAX};
    pd.sample_count = 1;
    pd.color_count = 1;
    pd.colors[0].format = DOE_MTL_RGBA8_UNORM;
    pd.colors[0].write_mask = 15;
    assert(doe_mtl_render_pipeline_create(f->context, &pd, &f->pipeline, NULL) == DOE_MTL_OK);
    f->pass.color_count = 1;
    f->pass.colors[0].view = f->view;
    f->pass.colors[0].load = DOE_MTL_CLEAR;
    f->pass.colors[0].store = DOE_MTL_STORE;
    f->pass.visibility_buffer = f->results;
    for (i = 0; i < 4; ++i) {
        f->draws[i].pipeline = f->pipeline;
        f->draws[i].count = 3;
        f->draws[i].instance_count = 1;
        f->draws[i].topology = 3;
    }
}
static doe_mtl_commands *commands_new(fixture *f) {
    doe_mtl_commands *commands = NULL;
    assert(doe_mtl_commands_create(f->context, &commands, NULL) == DOE_MTL_OK);
    return commands;
}
static void release_inputs(fixture *f) {
    doe_mtl_render_pipeline_release(f->pipeline);
    doe_mtl_texture_view_release(f->view);
    doe_mtl_buffer_release(f->results);
    doe_mtl_context_release(f->context);
}
static void lifetime(void) {
    fixture f;
    doe_mtl_commands *commands;
    uint64_t untouched = 123;
    initialize(&f);
    commands = commands_new(&f);
    f.draws[0].visibility_mode = 2;
    f.draws[0].visibility_offset = 8;
    f.draws[1] = f.draws[0];
    f.draws[3].visibility_mode = 1;
    f.draws[3].visibility_offset = 16;
    assert(doe_mtl_commands_render(commands, &f.pass, f.draws, 4, NULL) == DOE_MTL_OK);
    assert(f.mock.render_pass.visibility_buffer == f.results->native);
    assert(f.mock.render_draws[0].values.visibility_offset == 8 && f.mock.render_draw_count == 4);
    assert(doe_mtl_buffer_read(f.results, 8, &untouched, sizeof(untouched)) == DOE_MTL_BUSY && untouched == 123);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    release_inputs(&f);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY && f.mock.closes == 0);
    mock_result(&f.mock, 1, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    mock_result(&f.mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    mock_assert_closed(&f.mock);
}
static void invalid_runs(void) {
    fixture f;
    doe_mtl_commands *commands;
    doe_mtl_buffer_binding alias;
    initialize(&f);
    f.draws[0].visibility_mode = 1;
    f.draws[2].visibility_mode = 1;
    commands = commands_new(&f);
    assert(doe_mtl_commands_render(commands, &f.pass, f.draws, 3, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    f.draws[1].visibility_mode = 2;
    commands = commands_new(&f);
    assert(doe_mtl_commands_render(commands, &f.pass, f.draws, 2, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    f.draws[0].visibility_offset = 2;
    commands = commands_new(&f);
    assert(doe_mtl_commands_render(commands, &f.pass, f.draws, 1, NULL) == DOE_MTL_ALIGNMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    f.draws[0].visibility_offset = 64;
    commands = commands_new(&f);
    assert(doe_mtl_commands_render(commands, &f.pass, f.draws, 1, NULL) == DOE_MTL_RANGE);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    f.draws[0].visibility_offset = 0;
    f.draws[0].visibility_mode = 2;
    f.context->caps.counting_visibility = false;
    commands = commands_new(&f);
    assert(doe_mtl_commands_render(commands, &f.pass, f.draws, 1, NULL) == DOE_MTL_UNSUPPORTED);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    f.draws[0].visibility_mode = 1;
    alias = (doe_mtl_buffer_binding){0, f.results, 0, 64};
    f.draws[0].vertex.buffers = &alias;
    f.draws[0].vertex.buffer_count = 1;
    commands = commands_new(&f);
    assert(doe_mtl_commands_render(commands, &f.pass, f.draws, 1, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    assert(f.mock.renders == 0 && f.results->owner == NULL && f.view->texture->owner == NULL);
    release_inputs(&f);
    mock_assert_closed(&f.mock);
}
static void failed_query(void) {
    fixture f;
    doe_mtl_commands *commands;
    uint64_t untouched = 123;
    initialize(&f);
    commands = commands_new(&f);
    f.draws[0].visibility_mode = 1;
    assert(doe_mtl_commands_render(commands, &f.pass, f.draws, 1, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(&f.mock, 5, -339);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_EXECUTION_FAILED);
    assert(doe_mtl_buffer_read(f.results, 0, &untouched, sizeof(untouched)) == DOE_MTL_EXECUTION_FAILED);
    assert(untouched == 123);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    release_inputs(&f);
    mock_assert_closed(&f.mock);
}
int main(void) {
    lifetime();
    invalid_runs();
    failed_query();
    puts("Metal visibility controls: 3 admission/lifetime/error groups passed; no GPU query results generated");
    return 0;
}
