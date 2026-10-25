/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include "../src/query_internal.h"
#include <assert.h>
#include <stdio.h>

static doe_mtl_commands *command(doe_mtl_context *context) {
    doe_mtl_commands *result = NULL;
    assert(doe_mtl_commands_create(context, &result, NULL) == DOE_MTL_OK);
    return result;
}
static void finish(mock_native *mock, doe_mtl_commands *commands, size_t result) {
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(mock, result, -971);
    assert(doe_mtl_commands_wait(commands, NULL) == (result == 4 ? DOE_MTL_OK : DOE_MTL_EXECUTION_FAILED));
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
}
static void create_controls(void) {
    mock_native mock;
    doe_mtl_context *context = NULL;
    doe_mtl_timestamps *timestamps = NULL;
    doe_mtl_error error;
    assert(doe_mtl_context_open(mock_driver(&mock), &context, NULL) == DOE_MTL_OK);
    assert(doe_mtl_timestamps_create(context, 0, &timestamps, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_timestamps_create(context, 4097, &timestamps, NULL) == DOE_MTL_RANGE);
    assert(timestamps == NULL);
    context->caps.timestamp_stage = context->caps.timestamp_blit = false;
    assert(doe_mtl_timestamps_create(context, 4, &timestamps, NULL) == DOE_MTL_UNSUPPORTED);
    context->caps.timestamp_stage = true;
    mock.fail = MOCK_TIMESTAMPS;
    assert(doe_mtl_timestamps_create(context, 4, &timestamps, &error) == DOE_MTL_NATIVE_FAILURE);
    assert(error.native_code == -71 && timestamps == NULL && context->references == 1);
    mock.fail = MOCK_NONE;
    assert(doe_mtl_timestamps_create(context, 4, &timestamps, NULL) == DOE_MTL_OK);
    timestamps->references = UINT32_MAX;
    assert(doe_mtl_timestamps_retain(timestamps) == DOE_MTL_RANGE);
    timestamps->references = 1;
    doe_mtl_timestamps_release(timestamps);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void sample_resolve_lifetime(void) {
    mock_native mock;
    doe_mtl_context *context = NULL;
    doe_mtl_timestamps *timestamps = NULL;
    doe_mtl_buffer *buffer = NULL;
    doe_mtl_commands *commands, *competing;
    uint64_t output = 0xdeadbeef;
    assert(doe_mtl_context_open(mock_driver(&mock), &context, NULL) == DOE_MTL_OK);
    assert(doe_mtl_timestamps_create(context, 4, &timestamps, NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_create(context, 32, DOE_MTL_SHARED, &buffer, NULL) == DOE_MTL_OK);
    commands = command(context);
    assert(doe_mtl_commands_timestamp(commands, timestamps, 2, NULL) == DOE_MTL_OK);
    assert(mock.timestamp_pair.buffer == timestamps->native && mock.timestamp_pair.begin == 2);
    assert(!timestamps->available[2] && timestamps->owner == commands && timestamps->references == 2);
    assert(doe_mtl_commands_resolve_timestamps(commands, timestamps, 2, 1, buffer, 16, NULL) == DOE_MTL_OK);
    assert(commands->timestamps->resolve_event != NULL && commands->timestamps->resolve_value == 1);
    assert(mock.last_source == timestamps->native && mock.last_destination == buffer->native);
    assert(mock.last_source_offset == 2 && mock.last_length == 1 && mock.last_destination_offset == 16);
    assert(doe_mtl_buffer_read(buffer, 16, &output, 8) == DOE_MTL_BUSY && output == 0xdeadbeef);
    competing = command(context);
    assert(doe_mtl_commands_timestamp(competing, timestamps, 1, NULL) == DOE_MTL_BUSY);
    assert(doe_mtl_commands_release(competing) == DOE_MTL_OK);
    /* mock.last_command changed; observations below refer to the first command. */
    mock.last_command = commands->native;
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_PENDING);
    assert(doe_mtl_commands_wait(commands, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY);
    doe_mtl_timestamps_release(timestamps);
    doe_mtl_buffer_release(buffer);
    doe_mtl_context_release(context);
    mock_result(&mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    mock_assert_closed(&mock);
}
static void speculative_discard_and_rewrite(void) {
    mock_native mock;
    doe_mtl_context *context = NULL;
    doe_mtl_timestamps *timestamps = NULL;
    doe_mtl_buffer *buffer = NULL;
    doe_mtl_commands *commands;
    assert(doe_mtl_context_open(mock_driver(&mock), &context, NULL) == DOE_MTL_OK);
    assert(doe_mtl_timestamps_create(context, 2, &timestamps, NULL) == DOE_MTL_OK);
    assert(doe_mtl_buffer_create(context, 8, DOE_MTL_PRIVATE, &buffer, NULL) == DOE_MTL_OK);
    commands = command(context);
    assert(doe_mtl_commands_timestamp(commands, timestamps, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    assert(!timestamps->available[0] && timestamps->owner == NULL);
    commands = command(context);
    assert(doe_mtl_commands_resolve_timestamps(commands, timestamps, 0, 1, buffer, 0, NULL) == DOE_MTL_UNINITIALIZED);
    assert(mock.resolves == 0);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = command(context);
    assert(doe_mtl_commands_timestamp(commands, timestamps, 0, NULL) == DOE_MTL_OK);
    finish(&mock, commands, 4);
    assert(timestamps->available[0]);
    commands = command(context);
    assert(doe_mtl_commands_timestamp(commands, timestamps, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_resolve_timestamps(commands, timestamps, 0, 1, buffer, 0, NULL) == DOE_MTL_OK);
    assert(!buffer->initialized && doe_mtl_buffer_initialized(commands, buffer));
    finish(&mock, commands, 4);
    assert(buffer->initialized);
    doe_mtl_timestamps_release(timestamps);
    doe_mtl_buffer_release(buffer);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}
static void validation_controls(void) {
    unsigned scenario;
    for (scenario = 0; scenario < 7; ++scenario) {
        mock_native mock;
        doe_mtl_context *context = NULL;
        doe_mtl_timestamps *timestamps = NULL;
        doe_mtl_buffer *buffer = NULL;
        doe_mtl_commands *commands;
        doe_mtl_status expected;
        assert(doe_mtl_context_open(mock_driver(&mock), &context, NULL) == DOE_MTL_OK);
        assert(doe_mtl_timestamps_create(context, 4, &timestamps, NULL) == DOE_MTL_OK);
        assert(doe_mtl_buffer_create(context, 32, DOE_MTL_SHARED, &buffer, NULL) == DOE_MTL_OK);
        commands = command(context);
        if (scenario == 0) {
            context->caps.timestamp_blit = false;
            assert(doe_mtl_commands_timestamp(commands, timestamps, 0, NULL) == DOE_MTL_UNSUPPORTED);
            assert(mock.timestamps == 0);
        } else if (scenario == 1) {
            assert(doe_mtl_commands_timestamp(commands, timestamps, 4, NULL) == DOE_MTL_RANGE);
        } else {
            assert(doe_mtl_commands_timestamp(commands, timestamps, 1, NULL) == DOE_MTL_OK);
            if (scenario == 2) assert(doe_mtl_commands_timestamp(commands, timestamps, 1, NULL) == DOE_MTL_INVALID_STATE);
            else {
                expected = scenario == 3 ? DOE_MTL_ALIGNMENT : scenario == 4 ? DOE_MTL_RANGE :
                    scenario == 5 ? DOE_MTL_UNINITIALIZED : DOE_MTL_NATIVE_FAILURE;
                if (scenario == 6) mock.fail = MOCK_RESOLVE_TIMESTAMPS;
                assert(doe_mtl_commands_resolve_timestamps(commands, timestamps, 1,
                    scenario == 5 ? 2 : 1, buffer, scenario == 3 ? 3 : scenario == 4 ? 32 : 0, NULL) == expected);
            }
            assert(mock.resolves == 0);
        }
        assert(doe_mtl_commands_submit(commands, NULL) != DOE_MTL_OK);
        assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
        assert(!timestamps->available[1]);
        doe_mtl_timestamps_release(timestamps);
        doe_mtl_buffer_release(buffer);
        doe_mtl_context_release(context);
        mock_assert_closed(&mock);
    }
}
static void timed_compute_and_errors(void) {
    unsigned scenario;
    for (scenario = 0; scenario < 5; ++scenario) {
        mock_native mock;
        doe_mtl_context *context = NULL;
        doe_mtl_timestamps *timestamps = NULL;
        doe_mtl_pipeline *pipeline = NULL;
        doe_mtl_commands *commands;
        doe_mtl_pipeline_desc pipeline_desc = {"kernel void k(){}", 17, "k", {2, 4, 1}, UINT32_MAX};
        doe_mtl_compute_dispatch dispatch = {0};
        assert(doe_mtl_context_open(mock_driver(&mock), &context, NULL) == DOE_MTL_OK);
        assert(doe_mtl_timestamps_create(context, 4, &timestamps, NULL) == DOE_MTL_OK);
        assert(doe_mtl_pipeline_create(context, &pipeline_desc, &pipeline, NULL) == DOE_MTL_OK);
        commands = command(context);
        dispatch.pipeline = pipeline;
        dispatch.groups = (doe_mtl_dimensions){3, 2, 1};
        dispatch.repeat = scenario == 1 ? 0 : 2;
        dispatch.timestamps = timestamps;
        dispatch.timestamp_begin = 1;
        dispatch.timestamp_end = scenario == 2 ? 1 : 3;
        if (scenario == 3) context->caps.timestamp_stage = false;
        if (scenario == 2 || scenario == 3) {
            assert(doe_mtl_commands_compute(commands, &dispatch, NULL) ==
                (scenario == 2 ? DOE_MTL_INVALID_ARGUMENT : DOE_MTL_UNSUPPORTED));
            assert(mock.dispatches == 0 && timestamps->owner == NULL);
            assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
        } else {
            assert(doe_mtl_commands_compute(commands, &dispatch, NULL) == DOE_MTL_OK);
            assert(mock.dispatches == 1 && mock.repeat == dispatch.repeat);
            assert(mock.timestamp_pair.buffer == timestamps->native && mock.timestamp_pair.begin == 1 && mock.timestamp_pair.end == 3);
            finish(&mock, commands, scenario == 4 ? 5 : 4);
            if (scenario == 4) {
                assert(timestamps->poisoned && !timestamps->available[1]);
                commands = command(context);
                assert(doe_mtl_commands_timestamp(commands, timestamps, 0, NULL) == DOE_MTL_EXECUTION_FAILED);
                assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
            } else assert(timestamps->available[1] && timestamps->available[3]);
        }
        doe_mtl_pipeline_release(pipeline);
        doe_mtl_timestamps_release(timestamps);
        doe_mtl_context_release(context);
        mock_assert_closed(&mock);
    }
}
int main(void) {
    create_controls();
    sample_resolve_lifetime();
    speculative_discard_and_rewrite();
    validation_controls();
    timed_compute_and_errors();
    puts("Metal timestamp controls: 5 availability/lifetime/fault groups passed; no timestamp values simulated");
    return 0;
}
