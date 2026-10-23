/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include "../src/internal.h"
#include <assert.h>
#include <stdio.h>

static doe_mtl_context *context_create(mock_native *mock) {
    doe_mtl_context *context = NULL;
    doe_mtl_native native = mock_driver(mock);
    assert(doe_mtl_context_open(native, &context, NULL) == DOE_MTL_OK);
    assert(context != NULL && mock->live == 2);
    doe_mtl_caps caps;
    assert(doe_mtl_context_caps(context, &caps) == DOE_MTL_OK);
    assert(caps.max_threadgroup_memory_length == 32768);
    return context;
}

static doe_mtl_buffer *buffer_create(doe_mtl_context *context, doe_mtl_memory memory) {
    doe_mtl_buffer *buffer = NULL;
    assert(doe_mtl_buffer_create(context, 64, memory, &buffer, NULL) == DOE_MTL_OK);
    return buffer;
}

static doe_mtl_commands *command_create(doe_mtl_context *context) {
    doe_mtl_commands *commands = NULL;
    assert(doe_mtl_commands_create(context, &commands, NULL) == DOE_MTL_OK);
    return commands;
}

static doe_mtl_pipeline_desc pipeline_description(void) {
    const doe_mtl_pipeline_desc description = {
        .msl = "call-ledger bytes, never interpreted as a shader", .msl_length = 45,
        .entrypoint = "chosen_entry", .workgroup = {8, 4, 2}, .sizes_slot = 30
    };
    return description;
}

static doe_mtl_pipeline *pipeline_create(doe_mtl_context *context) {
    doe_mtl_pipeline *pipeline = NULL;
    doe_mtl_pipeline_desc desc = pipeline_description();
    assert(doe_mtl_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_OK);
    return pipeline;
}

static void terminal_success(mock_native *mock, doe_mtl_commands *commands) {
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
}

static void test_acquisition_failures(void) {
    mock_operation operations[] = {MOCK_DEVICE, MOCK_QUEUE};
    size_t i;
    for (i = 0; i < sizeof(operations) / sizeof(operations[0]); ++i) {
        mock_native mock;
        doe_mtl_native native = mock_driver(&mock);
        doe_mtl_context *out = (doe_mtl_context *)(uintptr_t)0x100;
        doe_mtl_error error = {0};
        mock.fail = operations[i];
        assert(doe_mtl_context_open(native, &out, &error) == DOE_MTL_NATIVE_FAILURE);
        assert(out == (doe_mtl_context *)(uintptr_t)0x100);
        assert(error.native_code == -71);
        mock_assert_closed(&mock);
    }
    {
        mock_native mock;
        doe_mtl_native native = mock_driver(&mock);
        native.dispatch = NULL;
        assert(doe_mtl_context_open(native, NULL, NULL) == DOE_MTL_INVALID_ARGUMENT);
        mock_assert_closed(&mock);
    }
    {
        mock_native mock;
        doe_mtl_context *context = context_create(&mock);
        doe_mtl_buffer *buffer = (doe_mtl_buffer *)(uintptr_t)0x100;
        doe_mtl_commands *command = (doe_mtl_commands *)(uintptr_t)0x100;
        doe_mtl_pipeline *pipeline = (doe_mtl_pipeline *)(uintptr_t)0x100;
        doe_mtl_pipeline_desc desc = pipeline_description();
        mock.fail = MOCK_BUFFER;
        assert(doe_mtl_buffer_create(context, 64, DOE_MTL_SHARED, &buffer, NULL) == DOE_MTL_NATIVE_FAILURE);
        assert(buffer == (doe_mtl_buffer *)(uintptr_t)0x100 && mock.live == 2);
        mock.fail = MOCK_CONTENTS;
        assert(doe_mtl_buffer_create(context, 64, DOE_MTL_SHARED, &buffer, NULL) == DOE_MTL_NATIVE_FAILURE);
        assert(buffer == (doe_mtl_buffer *)(uintptr_t)0x100 && mock.live == 2);
        mock.fail = MOCK_COMMAND;
        assert(doe_mtl_commands_create(context, &command, NULL) == DOE_MTL_NATIVE_FAILURE);
        assert(command == (doe_mtl_commands *)(uintptr_t)0x100 && mock.live == 2);
        mock.fail = MOCK_PIPELINE;
        assert(doe_mtl_pipeline_create(context, &desc, &pipeline, NULL) == DOE_MTL_NATIVE_FAILURE);
        assert(pipeline == (doe_mtl_pipeline *)(uintptr_t)0x100 && mock.live == 2);
        doe_mtl_context_release(context);
        mock_assert_closed(&mock);
    }
}

static void test_host_access(void) {
    mock_native mock;
    doe_mtl_context *context = context_create(&mock);
    doe_mtl_buffer *buffer = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_buffer *private_buffer = buffer_create(context, DOE_MTL_PRIVATE);
    unsigned char output[64], input[64];
    doe_mtl_buffer *untouched = buffer;
    size_t i;
    memset(output, 0x7c, sizeof(output));
    assert(doe_mtl_buffer_read(buffer, 0, output, sizeof(output)) == DOE_MTL_OK);
    for (i = 0; i < sizeof(output); ++i) assert(output[i] == 0);
    memset(input, 0x3c, sizeof(input));
    assert(doe_mtl_buffer_write(buffer, 4, input + 4, 12) == DOE_MTL_OK);
    assert(doe_mtl_buffer_read(buffer, 0, output, sizeof(output)) == DOE_MTL_OK);
    for (i = 0; i < sizeof(output); ++i)
        assert(output[i] == (i >= 4 && i < 16 ? 0x3c : 0));
    memset(output, 0x7c, sizeof(output));
    assert(doe_mtl_buffer_read(buffer, UINT64_MAX, output, 4) == DOE_MTL_RANGE);
    assert(output[0] == 0x7c);
    assert(doe_mtl_buffer_write(buffer, 63, input, 2) == DOE_MTL_RANGE);
    assert(doe_mtl_buffer_read(private_buffer, 0, output, 4) == DOE_MTL_UNSUPPORTED);
    assert(output[0] == 0x7c);
    assert(doe_mtl_buffer_create(context, 4097, DOE_MTL_SHARED, &untouched, NULL) == DOE_MTL_RANGE);
    assert(untouched == buffer);
    assert(doe_mtl_buffer_create(context, 0, DOE_MTL_SHARED, &untouched, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_buffer_read(buffer, 64, NULL, 0) == DOE_MTL_OK);
    mock.fail = MOCK_CONTENTS;
    assert(doe_mtl_buffer_read(buffer, 0, output, 4) == DOE_MTL_NATIVE_FAILURE);
    assert(output[0] == 0x7c);
    doe_mtl_buffer_release(buffer);
    doe_mtl_buffer_release(private_buffer);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}

static void test_pipeline_limits(void) {
    mock_native mock;
    doe_mtl_context *context = context_create(&mock);
    doe_mtl_pipeline_desc desc = pipeline_description();
    doe_mtl_pipeline *out = NULL;
    desc.workgroup.x = 0;
    assert(doe_mtl_pipeline_create(context, &desc, &out, NULL) == DOE_MTL_RANGE);
    desc.workgroup = (doe_mtl_dimensions){8, 4, 65};
    assert(doe_mtl_pipeline_create(context, &desc, &out, NULL) == DOE_MTL_RANGE);
    desc.workgroup = (doe_mtl_dimensions){8, 8, 8};
    assert(doe_mtl_pipeline_create(context, &desc, &out, NULL) == DOE_MTL_RANGE);
    assert(mock.live == 2 && out == NULL);
    desc = pipeline_description();
    desc.sizes_slot = 31;
    assert(doe_mtl_pipeline_create(context, &desc, &out, NULL) == DOE_MTL_RANGE);
    desc.sizes_slot = UINT32_MAX;
    mock.pipeline_max_threads = 0;
    assert(doe_mtl_pipeline_create(context, &desc, &out, NULL) == DOE_MTL_RANGE);
    mock.pipeline_max_threads = 64;
    assert(doe_mtl_pipeline_create(context, &desc, &out, NULL) == DOE_MTL_OK);
    assert(strcmp(mock.entrypoint, "chosen_entry") == 0);
    doe_mtl_pipeline_release(out);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}

static void test_exact_dispatch_and_lifetime(void) {
    mock_native mock;
    doe_mtl_context *context = context_create(&mock);
    doe_mtl_buffer *shared = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_buffer *private_buffer = buffer_create(context, DOE_MTL_PRIVATE);
    doe_mtl_pipeline *pipeline = pipeline_create(context);
    doe_mtl_commands *commands = command_create(context);
    doe_mtl_buffer_binding bindings[] = {{7, private_buffer, 8, 20}, {2, shared, 4, 12}};
    doe_mtl_dimensions groups = {3, 5, 7};
    void *private_native = doe_mtl_buffer_native(private_buffer);
    void *shared_native = doe_mtl_buffer_native(shared);
    unsigned char byte = 0xa5;
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_INVALID_STATE);
    assert(doe_mtl_commands_fill(commands, private_buffer, 0, 64, 0x37, NULL) == DOE_MTL_OK);
    assert(mock.last_destination == private_native && mock.last_fill == 0x37);
    assert(mock.last_destination_offset == 0 && mock.last_length == 64);
    assert(doe_mtl_commands_dispatch(commands, pipeline, bindings, 2, groups, 2, NULL) == DOE_MTL_OK);
    assert(mock.bindings[7].buffer == private_native && mock.bindings[7].offset == 8);
    assert(mock.bindings[7].size == 20 && mock.bindings[2].buffer == shared_native);
    assert(mock.bindings[2].offset == 4 && mock.bindings[2].size == 12);
    assert(mock.bindings[0].buffer == NULL && mock.bindings[30].buffer == NULL);
    assert(mock.sizes_slot == 30 && mock.groups.x == 3 && mock.groups.y == 5 && mock.groups.z == 7);
    assert(mock.workgroup.x == 8 && mock.workgroup.y == 4 && mock.workgroup.z == 2 && mock.repeat == 2);
    assert(doe_mtl_commands_copy(commands, private_buffer, 12, shared, 4, 16, NULL) == DOE_MTL_OK);
    assert(mock.last_source == private_native && mock.last_destination == shared_native);
    assert(mock.last_source_offset == 12 && mock.last_destination_offset == 4 && mock.last_length == 16);
    assert(doe_mtl_buffer_read(shared, 0, &byte, 1) == DOE_MTL_BUSY && byte == 0xa5);
    assert(doe_mtl_buffer_write(shared, 0, &byte, 1) == DOE_MTL_BUSY);
    doe_mtl_buffer_release(shared);
    doe_mtl_buffer_release(private_buffer);
    doe_mtl_pipeline_release(pipeline);
    doe_mtl_context_release(context);
    assert(mock.live == 6 && mock.closes == 0);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_INVALID_STATE && mock.commits == 1);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_PENDING);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY && mock.live == 6);
    mock_result(&mock, 3, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_PENDING);
    mock_result(&mock, 1, 0);
    assert(doe_mtl_commands_wait(commands, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY && mock.live == 6);
    mock_result(&mock, 4, 0);
    assert(doe_mtl_commands_wait(commands, NULL) == DOE_MTL_OK);
    assert(mock.live == 3 && mock.closes == 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    mock_assert_closed(&mock);
}

static void test_speculative_initialization(void) {
    mock_native mock;
    doe_mtl_context *context = context_create(&mock);
    doe_mtl_buffer *buffer = buffer_create(context, DOE_MTL_PRIVATE);
    doe_mtl_buffer *shared = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_commands *commands = command_create(context);
    assert(doe_mtl_commands_fill(commands, buffer, 0, 64, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    assert(!buffer->initialized);
    commands = command_create(context);
    assert(doe_mtl_commands_fill(commands, buffer, 4, 60, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_copy(commands, buffer, 0, shared, 0, 4, NULL) == DOE_MTL_UNINITIALIZED);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_UNINITIALIZED && mock.commits == 0);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    commands = command_create(context);
    assert(doe_mtl_commands_copy(commands, shared, 0, buffer, 0, 64, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_copy(commands, buffer, 0, shared, 0, 64, NULL) == DOE_MTL_OK);
    assert(!buffer->initialized);
    terminal_success(&mock, commands);
    assert(buffer->initialized);
    doe_mtl_buffer_release(buffer);
    doe_mtl_buffer_release(shared);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}

static void test_terminal_failure(void) {
    mock_native mock;
    doe_mtl_context *context = context_create(&mock);
    doe_mtl_buffer *buffer = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_commands *commands = command_create(context);
    doe_mtl_error error = {0};
    unsigned char input[64], output[64];
    memset(input, 0xa7, sizeof(input));
    memset(output, 0x31, sizeof(output));
    assert(doe_mtl_commands_fill(commands, buffer, 0, 64, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(&mock, 5, -8765);
    strcpy(mock.completion_message, "Metal terminal detail preserved before retirement");
    assert(doe_mtl_commands_poll(commands, &error) == DOE_MTL_EXECUTION_FAILED);
    assert(error.native_code == -8765);
    assert(strcmp(error.message, mock.completion_message) == 0);
    assert(strcmp(error.operation, "MTLCommandBuffer.error") == 0);
    doe_mtl_error first = error;
    strcpy(mock.completion_message, "replacement must not overwrite the first error");
    mock_result(&mock, 5, -111);
    assert(doe_mtl_commands_wait(commands, &error) == DOE_MTL_EXECUTION_FAILED);
    assert(memcmp(&error, &first, sizeof(error)) == 0);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    assert(doe_mtl_buffer_read(buffer, 0, output, 64) == DOE_MTL_EXECUTION_FAILED);
    assert(output[0] == 0x31);
    assert(doe_mtl_buffer_write(buffer, 4, input, 60) == DOE_MTL_EXECUTION_FAILED);
    commands = command_create(context);
    assert(doe_mtl_commands_fill(commands, buffer, 0, 64, 0, NULL) == DOE_MTL_EXECUTION_FAILED);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    assert(doe_mtl_buffer_write(buffer, 0, input, 64) == DOE_MTL_OK);
    assert(doe_mtl_buffer_read(buffer, 0, output, 64) == DOE_MTL_OK);
    assert(memcmp(input, output, 64) == 0);
    doe_mtl_buffer_release(buffer);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}

static void test_competing_owner_and_devices(void) {
    mock_native first, second;
    doe_mtl_context *a = context_create(&first), *b = context_create(&second);
    doe_mtl_buffer *buffer = buffer_create(a, DOE_MTL_SHARED);
    doe_mtl_pipeline *foreign = pipeline_create(b);
    doe_mtl_commands *one = command_create(a), *two = command_create(a);
    doe_mtl_dimensions groups = {1, 1, 1};
    assert(doe_mtl_commands_fill(one, buffer, 0, 64, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(two, buffer, 0, 64, 0, NULL) == DOE_MTL_BUSY);
    assert(first.fills == 1);
    assert(doe_mtl_commands_release(two) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(one) == DOE_MTL_OK);
    two = command_create(b);
    assert(doe_mtl_commands_fill(two, buffer, 0, 64, 0, NULL) == DOE_MTL_DEVICE_MISMATCH);
    assert(doe_mtl_commands_release(two) == DOE_MTL_OK);
    one = command_create(a);
    assert(doe_mtl_commands_dispatch(one, foreign, NULL, 0, groups, 1, NULL) == DOE_MTL_DEVICE_MISMATCH);
    assert(doe_mtl_commands_release(one) == DOE_MTL_OK);
    doe_mtl_buffer_release(buffer);
    doe_mtl_pipeline_release(foreign);
    doe_mtl_context_release(a);
    doe_mtl_context_release(b);
    mock_assert_closed(&first);
    mock_assert_closed(&second);
}

static void test_recording_failures(void) {
    mock_operation operations[] = {MOCK_COPY, MOCK_FILL, MOCK_DISPATCH};
    size_t i;
    for (i = 0; i < sizeof(operations) / sizeof(operations[0]); ++i) {
        mock_native mock;
        doe_mtl_context *context = context_create(&mock);
        doe_mtl_buffer *a = buffer_create(context, DOE_MTL_SHARED);
        doe_mtl_buffer *b = buffer_create(context, DOE_MTL_SHARED);
        doe_mtl_pipeline *pipeline = pipeline_create(context);
        doe_mtl_commands *command = command_create(context);
        doe_mtl_error error = {0};
        doe_mtl_status status;
        doe_mtl_buffer_binding binding = {0, a, 0, 64};
        mock.fail = operations[i];
        if (operations[i] == MOCK_COPY)
            status = doe_mtl_commands_copy(command, a, 0, b, 0, 64, &error);
        else if (operations[i] == MOCK_FILL)
            status = doe_mtl_commands_fill(command, a, 0, 64, 0, &error);
        else
            status = doe_mtl_commands_dispatch(command, pipeline, &binding, 1,
                (doe_mtl_dimensions){1, 1, 1}, 1, &error);
        assert(status == DOE_MTL_NATIVE_FAILURE && error.native_code == -71);
        assert(strcmp(error.message, "owned fault boundary") == 0);
        assert(doe_mtl_commands_fill(command, NULL, 0, 64, 0, &error) == DOE_MTL_NATIVE_FAILURE);
        assert(error.native_code == -71);
        assert(doe_mtl_commands_submit(command, NULL) == DOE_MTL_NATIVE_FAILURE && mock.commits == 0);
        assert(doe_mtl_commands_release(command) == DOE_MTL_OK);
        assert(!a->poisoned && !b->poisoned && a->owner == NULL && b->owner == NULL);
        doe_mtl_buffer_release(a);
        doe_mtl_buffer_release(b);
        doe_mtl_pipeline_release(pipeline);
        doe_mtl_context_release(context);
        mock_assert_closed(&mock);
    }
}

static void test_validation_and_empty_work(void) {
    mock_native mock;
    doe_mtl_context *context = context_create(&mock);
    doe_mtl_buffer *buffer = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_pipeline *pipeline = pipeline_create(context);
    doe_mtl_commands *command;
    doe_mtl_buffer_binding bindings[2] = {{2, buffer, 0, 64}, {2, buffer, 0, 64}};
    doe_mtl_dimensions groups = {1, 1, 1};
    unsigned i;
    for (i = 0; i < 5; ++i) {
        command = command_create(context);
        bindings[0] = (doe_mtl_buffer_binding){2, buffer, 0, 64};
        if (i == 0) {
            assert(doe_mtl_commands_dispatch(command, pipeline, bindings, 2, groups, 1, NULL) == DOE_MTL_INVALID_ARGUMENT);
        } else if (i == 1) {
            bindings[0].slot = 30;
            assert(doe_mtl_commands_dispatch(command, pipeline, bindings, 1, groups, 1, NULL) == DOE_MTL_RANGE);
        } else if (i == 2) {
            bindings[0].offset = 60;
            bindings[0].size = 8;
            assert(doe_mtl_commands_dispatch(command, pipeline, bindings, 1, groups, 1, NULL) == DOE_MTL_RANGE);
        } else if (i == 3) {
            bindings[0].offset = 2;
            bindings[0].size = 4;
            assert(doe_mtl_commands_dispatch(command, pipeline, bindings, 1, groups, 1, NULL) == DOE_MTL_ALIGNMENT);
        } else {
            bindings[0].size = 0;
            assert(doe_mtl_commands_dispatch(command, pipeline, bindings, 1, groups, 1, NULL) == DOE_MTL_RANGE);
        }
        assert(mock.dispatches == 0 && buffer->owner == NULL);
        assert(doe_mtl_commands_release(command) == DOE_MTL_OK);
    }
    command = command_create(context);
    assert(doe_mtl_commands_copy(command, buffer, 0, buffer, 4, 16, NULL) == DOE_MTL_RANGE);
    assert(doe_mtl_commands_release(command) == DOE_MTL_OK);
    command = command_create(context);
    assert(doe_mtl_commands_fill(command, buffer, 2, 4, 0, NULL) == DOE_MTL_ALIGNMENT);
    assert(doe_mtl_commands_release(command) == DOE_MTL_OK);
    command = command_create(context);
    assert(doe_mtl_commands_copy(command, buffer, UINT64_MAX - 3, buffer, 0, 8, NULL) == DOE_MTL_RANGE);
    assert(doe_mtl_commands_release(command) == DOE_MTL_OK);
    command = command_create(context);
    assert(doe_mtl_commands_dispatch_indirect(command, NULL) == DOE_MTL_UNSUPPORTED);
    assert(doe_mtl_commands_release(command) == DOE_MTL_OK);
    command = command_create(context);
    bindings[0] = (doe_mtl_buffer_binding){2, buffer, 0, 64};
    assert(doe_mtl_commands_dispatch(command, pipeline, bindings, 1, groups, 0, NULL) == DOE_MTL_OK);
    groups.y = 0;
    assert(doe_mtl_commands_dispatch(command, pipeline, bindings, 1, groups, 1, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_copy(command, buffer, 64, buffer, 64, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_fill(command, buffer, 64, 0, 0, NULL) == DOE_MTL_OK);
    assert(buffer->owner == NULL && mock.dispatches == 0 && mock.fills == 0 && mock.copies == 0);
    assert(doe_mtl_commands_release(command) == DOE_MTL_OK);
    doe_mtl_buffer_release(buffer);
    doe_mtl_pipeline_release(pipeline);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}

static void test_unknown_commit_and_reference_overflow(void) {
    mock_native mock;
    doe_mtl_context *context = context_create(&mock);
    doe_mtl_buffer *buffer = buffer_create(context, DOE_MTL_SHARED);
    doe_mtl_pipeline *pipeline = pipeline_create(context);
    doe_mtl_commands *commands = command_create(context);
    uint32_t references = context->references;
    context->references = UINT32_MAX;
    assert(doe_mtl_context_retain(context) == DOE_MTL_RANGE);
    context->references = references;
    buffer->references = UINT32_MAX;
    assert(doe_mtl_buffer_retain(buffer) == DOE_MTL_RANGE);
    buffer->references = 1;
    pipeline->references = UINT32_MAX;
    assert(doe_mtl_pipeline_retain(pipeline) == DOE_MTL_RANGE);
    pipeline->references = 1;
    assert(doe_mtl_commands_fill(commands, buffer, 0, 64, 0, NULL) == DOE_MTL_OK);
    mock.unknown_commit = true;
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY);
    assert(buffer->owner == commands && !buffer->poisoned);
    mock_result(&mock, 2, 0);
    assert(doe_mtl_commands_wait(commands, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    mock_result(&mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_buffer_release(buffer);
    doe_mtl_pipeline_release(pipeline);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
}

int main(void) {
    test_acquisition_failures();
    test_host_access();
    test_pipeline_limits();
    test_exact_dispatch_and_lifetime();
    test_speculative_initialization();
    test_terminal_failure();
    test_competing_owner_and_devices();
    test_recording_failures();
    test_validation_and_empty_work();
    test_unknown_commit_and_reference_overflow();
    puts("Metal developer controls: 10 scenario groups passed; mocked calls, no GPU execution");
    return 0;
}
