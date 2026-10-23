/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

static doe_dx_context *context(void) {
    doe_dx_context *owner = NULL;
    assert(doe_dx_context_create(NULL, &owner, NULL) == DOE_DX_OK);
    return owner;
}

static doe_dx_buffer *buffer(doe_dx_context *owner, doe_dx_memory memory) {
    doe_dx_buffer *result = NULL;
    assert(doe_dx_buffer_create(owner, 64, memory, &result, NULL) == DOE_DX_OK);
    return result;
}

static doe_dx_commands *commands(doe_dx_context *owner) {
    doe_dx_commands *result = NULL;
    assert(doe_dx_commands_create(owner, &result, NULL) == DOE_DX_OK);
    return result;
}

static doe_dx_pipeline *pipeline(doe_dx_context *owner) {
    const unsigned char bytecode[] = {1}; /* Call control, not executable shader code. */
    doe_dx_binding_layout binding = {0, 0, 4, DOE_DX_READ_WRITE, DOE_DX_STRUCTURED};
    doe_dx_pipeline_desc description = {bytecode, sizeof(bytecode), &binding, 1};
    doe_dx_pipeline *result = NULL;
    assert(doe_dx_pipeline_create(owner, &description, &result, NULL) == DOE_DX_OK);
    return result;
}

static void finish(doe_dx_context *owner) {
    assert(doe_dx_context_destroy(owner) == DOE_DX_OK);
    mock_reset();
}

static void preflight(void) {
    doe_dx_context *owner = context(), *other = context();
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *arguments = buffer(owner, DOE_DX_DEFAULT);
    doe_dx_buffer *storage = buffer(owner, DOE_DX_DEFAULT);
    doe_dx_buffer *foreign = buffer(other, DOE_DX_DEFAULT);
    doe_dx_pipeline *shader = pipeline(owner);
    doe_dx_commands *work = commands(owner);
    doe_dx_buffer_binding binding = {storage, 0, 64};
    doe_dx_compute_resources resources = {&binding, 1, NULL, 0};
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, NULL, 0, NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, foreign, 0, NULL) == DOE_DX_DEVICE_MISMATCH);
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, upload, 0, NULL) == DOE_DX_UNSUPPORTED);
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, arguments, 0, NULL) == DOE_DX_UNINITIALIZED);
    assert(!mock.indirect_dispatches && !work->buffer_count && !work->pipeline_count);
    assert(doe_dx_commands_copy(work, upload, 0, arguments, 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(work, upload, 0, storage, 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, arguments, 1, NULL) == DOE_DX_ALIGNMENT);
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, arguments, 56, NULL) == DOE_DX_RANGE);
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, arguments, UINT64_MAX, NULL) == DOE_DX_RANGE);
    binding.buffer = arguments;
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, arguments, 0, NULL) == DOE_DX_UNSUPPORTED);
    binding.buffer = storage;
    assert(!mock.indirect_dispatches && !work->pipeline_count && work->operation_count == 2);
    doe_dx_commands_release(work);
    doe_dx_pipeline_release(shader);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(arguments);
    doe_dx_buffer_release(storage);
    doe_dx_buffer_release(foreign);
    assert(doe_dx_context_destroy(other) == DOE_DX_OK);
    finish(owner);
}

static void lifetime_and_unknown_completion(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *arguments = buffer(owner, DOE_DX_DEFAULT);
    doe_dx_buffer *storage = buffer(owner, DOE_DX_DEFAULT);
    doe_dx_pipeline *shader = pipeline(owner);
    doe_dx_commands *work = commands(owner), *competing = commands(owner);
    doe_dx_buffer_binding binding = {storage, 0, 64};
    doe_dx_compute_resources resources = {&binding, 1, NULL, 0};
    assert(doe_dx_commands_copy(work, upload, 0, arguments, 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(work, upload, 0, storage, 0, 64, NULL) == DOE_DX_OK);
    doe_dx_error error = {0};
    mock.failure = MOCK_DISPATCH_INDIRECT;
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, arguments, 16, &error) == DOE_DX_NATIVE_FAILURE);
    assert(error.native_code == -100 - MOCK_DISPATCH_INDIRECT);
    assert(!mock.indirect_dispatches && !work->pipeline_count && work->operation_count == 2);
    mock.failure = MOCK_NONE;
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, arguments, 16, NULL) == DOE_DX_OK);
    assert(mock.indirect_dispatches == 1 && mock.indirect_arguments == arguments->native);
    assert(mock.indirect_offset == 16 && mock.bindings[0].buffer == storage->native);
    assert(work->buffer_count == 3 && work->pipeline_count == 1 && work->operation_count == 3);
    assert(doe_dx_commands_dispatch_indirect(competing, shader, &resources, arguments, 16, NULL) == DOE_DX_BUSY);
    doe_dx_commands_release(competing);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(arguments);
    doe_dx_buffer_release(storage);
    doe_dx_pipeline_release(shader);
    mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(work);
    assert(mock.buffers == 3 && mock.pipelines == 1 && mock.commands == 1);
    assert(doe_dx_context_destroy(owner) == DOE_DX_BUSY);
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    assert(mock.buffers == 3 && mock.pipelines == 1 && mock.commands == 1);
    mock.failure = MOCK_NONE;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_PENDING);
    mock.completed = 1;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK);
    assert(!mock.buffers && !mock.pipelines && !mock.commands);
    finish(owner);
}

static void shared_resource_budget(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *buffers[65];
    for (size_t index = 0; index < 65; index++) buffers[index] = buffer(owner, DOE_DX_DEFAULT);
    /* Establish initialization through real owner operations, in two batches. */
    doe_dx_commands *work = commands(owner);
    for (size_t index = 0; index < 63; index++)
        assert(doe_dx_commands_copy(work, upload, 0, buffers[index], 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    doe_dx_commands_release(work);
    mock.completed = 1;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK);
    work = commands(owner);
    for (size_t index = 63; index < 65; index++)
        assert(doe_dx_commands_copy(work, upload, 0, buffers[index], 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    doe_dx_commands_release(work);
    mock.completed = 2;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK);
    work = commands(owner);
    for (size_t index = 3; index < 65; index++)
        assert(doe_dx_commands_copy(work, buffers[2], 0, buffers[index], 0, 64, NULL) == DOE_DX_OK);
    assert(work->buffer_count == 63);
    doe_dx_pipeline *shader = pipeline(owner);
    doe_dx_buffer_binding binding = {buffers[0], 0, 64};
    doe_dx_compute_resources resources = {&binding, 1, NULL, 0};
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, buffers[1], 0, NULL) == DOE_DX_RANGE);
    assert(!mock.indirect_dispatches && !work->pipeline_count && work->buffer_count == 63);
    /* One existing binding plus the last argument owner exactly fills capacity. */
    binding.buffer = buffers[2];
    assert(doe_dx_commands_dispatch_indirect(work, shader, &resources, buffers[1], 0, NULL) == DOE_DX_OK);
    assert(work->buffer_count == 64 && mock.indirect_dispatches == 1);
    doe_dx_commands_release(work);
    doe_dx_pipeline_release(shader);
    for (size_t index = 0; index < 65; index++) doe_dx_buffer_release(buffers[index]);
    doe_dx_buffer_release(upload);
    finish(owner);
}

int main(void) {
    mock_reset();
    preflight();
    lifetime_and_unknown_completion();
    shared_resource_budget();
    puts("D3D12 C indirect dispatch: 3 groups passed (no SDK or GPU execution)");
    return 0;
}
