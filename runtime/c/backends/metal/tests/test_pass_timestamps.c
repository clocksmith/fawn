/* SPDX-License-Identifier: MIT
 * Pass attachment preparation, call boundaries and actual ownership only.
 * No counter values or GPU execution are simulated. */
#include "mock_native.h"
#include "../src/query_internal.h"
#include "../src/render_internal.h"
#include <assert.h>
#include <stdio.h>

#ifdef DOE_PASS_TIMESTAMP_ALLOC_WRAP
static _Thread_local size_t allocation_failure;
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t count, size_t size) {
    if (allocation_failure && !--allocation_failure) return NULL;
    return __real_calloc(count, size);
}
#endif

static doe_mtl_context *context_new(mock_native *mock) {
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(mock), &context, NULL) == DOE_MTL_OK);
    return context;
}
static doe_mtl_commands *command_new(doe_mtl_context *context) {
    doe_mtl_commands *commands = NULL;
    assert(doe_mtl_commands_create(context, &commands, NULL) == DOE_MTL_OK);
    return commands;
}
static doe_mtl_timestamps *timestamps_new(doe_mtl_context *context) {
    doe_mtl_timestamps *timestamps = NULL;
    assert(doe_mtl_timestamps_create(context, 4, &timestamps, NULL) == DOE_MTL_OK);
    return timestamps;
}
static doe_mtl_pipeline *pipeline_new(doe_mtl_context *context) {
    doe_mtl_pipeline *pipeline = NULL;
    doe_mtl_pipeline_desc descriptor = {"kernel void k(){}",17,"k",{1,1,1},UINT32_MAX};
    assert(doe_mtl_pipeline_create(context, &descriptor, &pipeline, NULL) == DOE_MTL_OK);
    return pipeline;
}
static doe_mtl_texture_view *view_new(doe_mtl_context *context) {
    const doe_mtl_texture_desc descriptor = {DOE_MTL_TEXTURE_2D,DOE_MTL_RGBA8_UNORM,
        4,4,1,1,DOE_MTL_TEXTURE_RENDER_TARGET,false,1};
    const doe_mtl_texture_view_desc view_descriptor = {DOE_MTL_TEXTURE_2D,DOE_MTL_RGBA8_UNORM,
        0,1,0,1,DOE_MTL_SWIZZLE_RED,DOE_MTL_SWIZZLE_GREEN,DOE_MTL_SWIZZLE_BLUE,DOE_MTL_SWIZZLE_ALPHA};
    doe_mtl_texture *texture = NULL;
    doe_mtl_texture_view *view = NULL;
    assert(doe_mtl_texture_create(context, &descriptor, &texture, NULL) == DOE_MTL_OK);
    assert(doe_mtl_texture_view_create(texture, &view_descriptor, &view, NULL) == DOE_MTL_OK);
    doe_mtl_texture_release(texture);
    return view;
}
static void compute(doe_mtl_commands *commands, doe_mtl_pipeline *pipeline,
    doe_mtl_pass_timestamps writes) {
    const doe_mtl_compute_dispatch dispatch = {.pipeline=pipeline,.groups={1,1,1},.repeat=1,
        .timestamps=writes.set,.timestamp_begin=writes.begin,.timestamp_end=writes.end};
    assert(doe_mtl_commands_compute(commands, &dispatch, NULL) == DOE_MTL_OK);
}
static void prepare_and_compute(void) {
    for (unsigned mode = 0; mode < 3; ++mode) {
        mock_native mock;
        doe_mtl_context *context = context_new(&mock);
        doe_mtl_timestamps *set = timestamps_new(context);
        doe_mtl_pipeline *pipeline = pipeline_new(context);
        doe_mtl_commands *commands = command_new(context);
        doe_mtl_pass_timestamps writes = {set,mode == 2 ? UINT32_MAX : 1,mode == 1 ? UINT32_MAX : 3};
        bool attach = false;
        assert(doe_mtl_commands_prepare_pass_timestamps(commands, &writes, &attach, NULL) == DOE_MTL_OK && attach);
        assert(set->owner == commands && set->references == 2);
        assert(!commands->timestamps->available[1] && !commands->timestamps->available[3]);
        compute(commands, pipeline, writes);
        assert(mock.timestamp_pair.buffer == set->native && mock.timestamp_pair.begin == writes.begin &&
            mock.timestamp_pair.end == writes.end && mock.dispatches == 1);
        assert(commands->timestamps->available[1] == (mode != 2));
        assert(commands->timestamps->available[3] == (mode != 1));
        assert(!set->available[1] && !set->available[3]);
        assert(!mock.timestamps && !mock.resolves && !mock.fills && !commands->timestamps->resolve_event);
        /* Abandonment never publishes the speculative attachment result. */
        assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
        assert(!set->owner && !set->available[1] && !set->available[3]);
        doe_mtl_pipeline_release(pipeline);
        doe_mtl_timestamps_release(set);
        doe_mtl_context_release(context);
        mock_assert_closed(&mock);
    }
}
static void preparation_controls(void) {
    for (unsigned mode = 0; mode < 5; ++mode) {
        mock_native mock;
        doe_mtl_context *context = context_new(&mock);
        doe_mtl_timestamps *set = timestamps_new(context);
        doe_mtl_commands *commands = command_new(context);
        doe_mtl_pass_timestamps writes = {set,1,3};
        bool attach = true;
        if (mode == 0) writes.begin = writes.end = UINT32_MAX;
        else if (mode == 1) writes.end = 1;
        else if (mode == 2) writes.end = 4;
        else if (mode == 3) context->caps.timestamp_stage = false;
        else set->poisoned = true;
        doe_mtl_status expected = mode < 2 ? DOE_MTL_INVALID_ARGUMENT : mode == 2 ? DOE_MTL_RANGE :
            mode == 3 ? DOE_MTL_OK : DOE_MTL_EXECUTION_FAILED;
        assert(doe_mtl_commands_prepare_pass_timestamps(commands, &writes, &attach, NULL) == expected);
        assert(attach == (mode != 3) && !set->owner && set->references == 1);
        assert(!mock.dispatches && !mock.renders && !mock.timestamps);
        assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
        doe_mtl_timestamps_release(set);
        doe_mtl_context_release(context);
        mock_assert_closed(&mock);
    }
}
static void reuse_and_resolve_fallback(void) {
    mock_native mock;
    doe_mtl_context *context = context_new(&mock);
    doe_mtl_timestamps *set = timestamps_new(context);
    doe_mtl_pipeline *pipeline = pipeline_new(context);
    doe_mtl_commands *commands = command_new(context);
    doe_mtl_pass_timestamps writes = {set,1,3};
    bool attach = false;
    assert(doe_mtl_commands_prepare_pass_timestamps(commands, &writes, &attach, NULL) == DOE_MTL_OK && attach);
    compute(commands, pipeline, writes);
    attach = true;
    assert(doe_mtl_commands_prepare_pass_timestamps(commands, &writes, &attach, NULL) == DOE_MTL_OK && !attach);
    writes = (doe_mtl_pass_timestamps){set,0,2};
    assert(doe_mtl_commands_prepare_pass_timestamps(commands, &writes, &attach, NULL) == DOE_MTL_OK && attach);
    doe_mtl_buffer *resolved = NULL;
    assert(doe_mtl_buffer_create(context, 16, DOE_MTL_SHARED, &resolved, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_resolve_timestamps(commands, set, 1, 1, resolved, 0, NULL) == DOE_MTL_OK);
    assert(commands->timestamps->resolve_value == 1);
    attach = true;
    assert(doe_mtl_commands_prepare_pass_timestamps(commands, &writes, &attach, NULL) == DOE_MTL_OK && !attach);
    /* The strict private operation still rejects an already-written endpoint. */
    const doe_mtl_compute_dispatch duplicate = {.pipeline=pipeline,.groups={1,1,1},.repeat=1,
        .timestamps=set,.timestamp_begin=1,.timestamp_end=3};
    assert(doe_mtl_commands_compute(commands, &duplicate, NULL) == DOE_MTL_INVALID_STATE);
    assert(mock.dispatches == 1);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_buffer_release(resolved);
    doe_mtl_pipeline_release(pipeline);
    doe_mtl_timestamps_release(set);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void render_and_unknown(void) {
    for (unsigned mode = 0; mode < 5; ++mode) {
        mock_native mock;
        doe_mtl_context *context = context_new(&mock);
        doe_mtl_timestamps *set = timestamps_new(context);
        doe_mtl_texture_view *view = view_new(context);
        doe_mtl_commands *commands = command_new(context);
        doe_mtl_render_pass_desc pass = {.color_count=1,
            .timestamps={set,mode == 2 ? UINT32_MAX : 1,mode == 1 ? UINT32_MAX : 3}};
        pass.colors[0] = (doe_mtl_render_attachment){.view=view,.load=DOE_MTL_CLEAR,.store=DOE_MTL_STORE};
        bool attach = false;
        assert(doe_mtl_commands_prepare_pass_timestamps(commands, &pass.timestamps, &attach, NULL) == DOE_MTL_OK && attach);
        if (mode == 3) mock.fail = MOCK_RENDER;
        doe_mtl_status status = doe_mtl_commands_render(commands, &pass, NULL, 0, NULL);
        assert(status == (mode == 3 ? DOE_MTL_NATIVE_FAILURE : DOE_MTL_OK));
        assert(mock.renders == (mode == 3 ? 0u : 1u));
        if (mode != 3) {
            assert(mock.render_pass.timestamp_buffer == set->native &&
                mock.render_pass.timestamp_begin == pass.timestamps.begin &&
                mock.render_pass.timestamp_end == pass.timestamps.end);
            assert(commands->timestamps->available[1] == (mode != 2));
            assert(commands->timestamps->available[3] == (mode != 1));
        } else assert(!commands->timestamps->available[1] && !commands->timestamps->available[3]);
        assert(!mock.timestamps && !mock.resolves && !commands->timestamps->resolve_event);
        if (mode == 4) {
            mock.unknown_commit = true;
            assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
            assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
            assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY && set->owner == commands);
            doe_mtl_commands *competing = command_new(context);
            attach = false;
            assert(doe_mtl_commands_prepare_pass_timestamps(competing, &pass.timestamps, &attach, NULL) == DOE_MTL_BUSY);
            assert(!attach && !set->available[1] && !set->available[3]);
            assert(doe_mtl_commands_release(competing) == DOE_MTL_OK);
            doe_mtl_timestamps_release(set);
            doe_mtl_texture_view_release(view);
            doe_mtl_context_release(context);
            mock_command_result(commands->native, 4, 0);
            assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
            assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
        } else {
            assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
            assert(!set->owner && !set->available[1] && !set->available[3]);
            doe_mtl_timestamps_release(set);
            doe_mtl_texture_view_release(view);
            doe_mtl_context_release(context);
        }
        mock_assert_closed(&mock);
    }
}
#ifdef DOE_PASS_TIMESTAMP_ALLOC_WRAP
static void allocation_rollback(void) {
    for (size_t failure = 1; failure <= 3; ++failure) {
        mock_native mock;
        doe_mtl_context *context = context_new(&mock);
        doe_mtl_timestamps *set = timestamps_new(context);
        doe_mtl_commands *commands = command_new(context);
        const doe_mtl_pass_timestamps writes = {set,1,3};
        bool attach = true;
        allocation_failure = failure;
        assert(doe_mtl_commands_prepare_pass_timestamps(commands, &writes, &attach, NULL) == DOE_MTL_OUT_OF_MEMORY);
        allocation_failure = 0;
        assert(attach && !set->owner && set->references == 1 && !commands->timestamps);
        assert(!mock.dispatches && !mock.renders && !mock.timestamps);
        assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
        doe_mtl_timestamps_release(set);
        doe_mtl_context_release(context);
        mock_assert_closed(&mock);
    }
}
#endif
int main(void) {
    prepare_and_compute();
    preparation_controls();
    reuse_and_resolve_fallback();
    render_and_unknown();
#ifdef DOE_PASS_TIMESTAMP_ALLOC_WRAP
    allocation_rollback();
#endif
    puts("PASS Metal pass timestamp host controls: actual attachment spans, one-sided writes, fallback, rollback and unknown leases; no GPU values simulated");
    return 0;
}
