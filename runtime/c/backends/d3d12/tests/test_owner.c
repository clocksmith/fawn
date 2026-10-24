/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

static doe_dx_context *context(void) {
    doe_dx_context *result = NULL;
    assert(doe_dx_context_create(NULL, &result, NULL) == DOE_DX_OK);
    return result;
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
    const unsigned char bytecode[] = {1}; /* Never treated as real shader code. */
    doe_dx_binding_layout layout = {2, 3, 4, DOE_DX_READ_WRITE, DOE_DX_STRUCTURED};
    doe_dx_pipeline_desc description = {bytecode, sizeof(bytecode), &layout, 1};
    doe_dx_pipeline *result = NULL;
    assert(doe_dx_pipeline_create(owner, &description, &result, NULL) == DOE_DX_OK);
    return result;
}

static void finish(doe_dx_context *owner) {
    assert(doe_dx_context_destroy(owner) == DOE_DX_OK);
    mock_reset();
}

static void lifetime_and_order(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *storage = buffer(owner, DOE_DX_DEFAULT);
    doe_dx_buffer *readback = buffer(owner, DOE_DX_READBACK);
    doe_dx_pipeline *shader = pipeline(owner);
    doe_dx_commands *work = commands(owner);
    unsigned char bytes[64] = {0};
    assert(doe_dx_buffer_read(readback, 0, bytes, sizeof(bytes), NULL) == DOE_DX_UNINITIALIZED);
    assert(doe_dx_buffer_write(upload, 0, bytes, sizeof(bytes), NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(work, upload, 0, storage, 0, 64, NULL) == DOE_DX_OK);
    assert(mock.copy_source == upload->native && mock.copy_destination == storage->native);
    assert(!mock.source_offset && !mock.destination_offset && mock.copy_size == 64);
    doe_dx_buffer_binding binding = {storage, 16, 32};
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 7, 2, 3, NULL) == DOE_DX_OK);
    assert(mock.binding_count == 1 && mock.bindings[0].buffer == storage->native);
    assert(mock.bindings[0].offset == 16 && mock.bindings[0].size == 32);
    assert(mock.x == 7 && mock.y == 2 && mock.z == 3);
    assert(doe_dx_commands_copy(work, storage, 0, readback, 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_write(upload, 0, bytes, sizeof(bytes), NULL) == DOE_DX_BUSY);
    assert(doe_dx_buffer_read(readback, 0, bytes, sizeof(bytes), NULL) == DOE_DX_BUSY);
    doe_dx_pipeline_release(shader);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(storage);
    assert(mock.buffers == 3 && mock.pipelines == 1);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_INVALID_STATE);
    doe_dx_commands_release(work);
    assert(doe_dx_context_destroy(owner) == DOE_DX_BUSY);
    assert(doe_dx_context_poll(owner, 1, NULL) == DOE_DX_PENDING);
    assert(mock.commands == 1 && mock.buffers == 3 && mock.pipelines == 1);
    mock.completed = 1;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK);
    assert(!mock.commands && mock.buffers == 1 && !mock.pipelines);
    assert(doe_dx_buffer_read(readback, 0, bytes, sizeof(bytes), NULL) == DOE_DX_OK);
    assert(bytes[0] == 0x5a); /* Mapping eligibility only, not computed output. */
    doe_dx_buffer_release(readback);
    finish(owner);
}

static void discard_initialization(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *readback = buffer(owner, DOE_DX_READBACK);
    doe_dx_commands *work = commands(owner);
    assert(doe_dx_commands_copy(work, upload, 0, readback, 0, 64, NULL) == DOE_DX_OK);
    doe_dx_commands_release(work);
    unsigned char bytes[64] = {17};
    assert(doe_dx_buffer_read(readback, 0, bytes, sizeof(bytes), NULL) == DOE_DX_UNINITIALIZED);
    assert(bytes[0] == 17);
    work = commands(owner);
    assert(doe_dx_commands_copy(work, upload, 0, readback, 4, 60, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    mock.completed = 1;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_result(work, NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_read(readback, 0, bytes, sizeof(bytes), NULL) == DOE_DX_UNINITIALIZED);
    doe_dx_commands_release(work);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(readback);
    finish(owner);
}

static void unknown_completion(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *readback = buffer(owner, DOE_DX_READBACK);
    doe_dx_commands *work = commands(owner);
    doe_dx_commands *later = commands(owner);
    assert(doe_dx_commands_copy(work, upload, 0, readback, 0, 64, NULL) == DOE_DX_OK);
    mock.failure = MOCK_SIGNAL;
    doe_dx_error error = {0};
    assert(doe_dx_commands_submit(work, &error) == DOE_DX_COMPLETION_UNKNOWN);
    assert(error.native_code == -100 - MOCK_SIGNAL && mock.executes == 1);
    assert(doe_dx_commands_result(work, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    assert(doe_dx_commands_submit(later, NULL) == DOE_DX_BUSY);
    assert(mock.finishes == 1);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(readback);
    doe_dx_commands_release(work);
    assert(doe_dx_context_poll(owner, 1, &error) == DOE_DX_COMPLETION_UNKNOWN);
    assert(mock.commands == 2 && mock.buffers == 2);
    /* Even a reported old completed counter cannot retire unsignaled work. */
    mock.completed = 1;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    assert(mock.buffers == 2);
    mock.failure = MOCK_NONE;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK);
    assert(!mock.buffers && mock.commands == 1 && mock.last_signal == 1);
    doe_dx_commands_release(later);
    finish(owner);
}

static void wait_and_observation_failures(void) {
    doe_dx_context *owner = context();
    doe_dx_commands *work = commands(owner);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    doe_dx_error error = {0};
    mock.failure = MOCK_WAIT;
    assert(doe_dx_context_poll(owner, 2, &error) == DOE_DX_COMPLETION_UNKNOWN);
    assert(error.native_code == -100 - MOCK_WAIT && mock.commands == 1);
    mock.failure = MOCK_COMPLETED;
    assert(doe_dx_context_poll(owner, 0, &error) == DOE_DX_COMPLETION_UNKNOWN);
    assert(error.native_code == -100 - MOCK_COMPLETED && mock.commands == 1);
    mock.failure = MOCK_NONE;
    mock.completed = 1;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_result(work, NULL) == DOE_DX_OK);
    doe_dx_commands_release(work);
    finish(owner);
}

static void device_loss(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *readback = buffer(owner, DOE_DX_READBACK);
    doe_dx_commands *work = commands(owner);
    assert(doe_dx_commands_copy(work, upload, 0, readback, 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    doe_dx_buffer_release(upload);
    mock.lost = true;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_DEVICE_LOST);
    assert(mock.commands == 0 && mock.buffers == 1);
    assert(doe_dx_commands_result(work, NULL) == DOE_DX_DEVICE_LOST);
    unsigned char bytes[4] = {19};
    assert(doe_dx_buffer_read(readback, 0, bytes, 4, NULL) == DOE_DX_DEVICE_LOST);
    assert(bytes[0] == 19 && !readback->initialized);
    doe_dx_commands_release(work);
    doe_dx_buffer_release(readback);
    finish(owner);
}

static void preflight_and_recording_failures(void) {
    doe_dx_context *owner = context();
    doe_dx_context *other = context();
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *storage = buffer(owner, DOE_DX_DEFAULT);
    doe_dx_buffer *foreign = buffer(other, DOE_DX_DEFAULT);
    doe_dx_commands *work = commands(owner);
    doe_dx_commands *competing = commands(owner);
    doe_dx_pipeline *shader = pipeline(owner);
    doe_dx_buffer_binding binding = {storage, 0, 64};
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 1, 1, 1, NULL) == DOE_DX_UNINITIALIZED);
    assert(doe_dx_commands_copy(work, upload, 0, foreign, 0, 64, NULL) == DOE_DX_DEVICE_MISMATCH);
    assert(doe_dx_commands_copy(work, upload, UINT64_MAX, storage, 0, 4, NULL) == DOE_DX_RANGE);
    assert(doe_dx_commands_copy(work, upload, 0, storage, 1, 4, NULL) == DOE_DX_ALIGNMENT);
    assert(!mock.copies && !work->buffer_count);
    assert(doe_dx_commands_copy(work, upload, 0, storage, 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(competing, upload, 0, foreign, 0, 4, NULL) == DOE_DX_BUSY);
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 65536, 1, 1, NULL) == DOE_DX_RANGE);
    binding.offset = 3;
    binding.size = 4;
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 1, 1, 1, NULL) == DOE_DX_ALIGNMENT);
    binding.offset = 0;
    binding.size = 64;
    mock.failure = MOCK_DISPATCH;
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 1, 1, 1, NULL) == DOE_DX_NATIVE_FAILURE);
    assert(!mock.dispatches && !work->pipeline_count);
    mock.failure = MOCK_NONE;
    assert(doe_dx_commands_dispatch(work, shader, &binding, 1, 1, 1, 1, NULL) == DOE_DX_OK);
    mock.failure = MOCK_FINISH;
    doe_dx_error first, again;
    assert(doe_dx_commands_submit(work, &first) == DOE_DX_NATIVE_FAILURE);
    mock.failure = MOCK_NONE;
    assert(doe_dx_commands_submit(work, &again) == DOE_DX_NATIVE_FAILURE);
    assert(first.native_code == again.native_code && mock.finishes == 1 && !mock.executes);
    doe_dx_commands_release(work);
    doe_dx_commands_release(competing);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(storage);
    doe_dx_buffer_release(foreign);
    doe_dx_pipeline_release(shader);
    assert(doe_dx_context_destroy(other) == DOE_DX_OK);
    finish(owner);
}

static void constructor_failures(void) {
    doe_dx_context *unchanged = (doe_dx_context *)(uintptr_t)1;
    mock.failure = MOCK_OPEN;
    assert(doe_dx_context_create(NULL, &unchanged, NULL) == DOE_DX_NATIVE_FAILURE);
    assert(unchanged == (doe_dx_context *)(uintptr_t)1 && !mock.allocations);
    mock.failure = MOCK_NONE;
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_context_create(NULL, &unchanged, NULL) == DOE_DX_OUT_OF_MEMORY);
    assert(unchanged == (doe_dx_context *)(uintptr_t)1);
    mock.allocation_failure = 0;
    doe_dx_context *owner = context();
    doe_dx_buffer *b = NULL;
    mock.failure = MOCK_BUFFER;
    assert(doe_dx_buffer_create(owner, 64, DOE_DX_UPLOAD, &b, NULL) == DOE_DX_NATIVE_FAILURE && !b);
    mock.failure = MOCK_NONE;
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_buffer_create(owner, 64, DOE_DX_UPLOAD, &b, NULL) == DOE_DX_OUT_OF_MEMORY && !b);
    mock.allocation_failure = 0;
    doe_dx_pipeline *p = NULL;
    const unsigned char bytecode = 1;
    doe_dx_pipeline_desc description = {&bytecode, 1, NULL, 0};
    mock.failure = MOCK_PIPELINE;
    assert(doe_dx_pipeline_create(owner, &description, &p, NULL) == DOE_DX_NATIVE_FAILURE && !p);
    mock.failure = MOCK_NONE;
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_pipeline_create(owner, &description, &p, NULL) == DOE_DX_OUT_OF_MEMORY && !p);
    mock.allocation_failure = 0;
    doe_dx_commands *c = NULL;
    mock.failure = MOCK_COMMANDS;
    assert(doe_dx_commands_create(owner, &c, NULL) == DOE_DX_NATIVE_FAILURE && !c);
    mock.failure = MOCK_NONE;
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_commands_create(owner, &c, NULL) == DOE_DX_OUT_OF_MEMORY && !c);
    mock.allocation_failure = 0;
    assert(owner->objects == 0 && mock.allocations == 1);
    finish(owner);
}

static void partial_retirement_and_budgets(void) {
    doe_dx_context *owner = context();
    doe_dx_commands *first = commands(owner);
    doe_dx_commands *second = commands(owner);
    assert(doe_dx_commands_submit(first, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(second, NULL) == DOE_DX_OK);
    mock.completed = 1;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_PENDING);
    assert(doe_dx_commands_result(first, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_result(second, NULL) == DOE_DX_PENDING);
    assert(mock.commands == 1);
    doe_dx_commands_release(first);
    doe_dx_commands_release(second);
    mock.completed = 2;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK);
    doe_dx_commands *exhausted = commands(owner);
    owner->next_fence = UINT64_MAX - 1;
    assert(doe_dx_commands_submit(exhausted, NULL) == DOE_DX_RANGE);
    assert(mock.executes == 2);
    doe_dx_commands_release(exhausted);
    finish(owner);
}

static void resource_and_reference_limits(void) {
    doe_dx_context *owner = context();
    doe_dx_commands *work = commands(owner);
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *destinations[64];
    for (size_t index = 0; index < 64; index++) destinations[index] = buffer(owner, DOE_DX_DEFAULT);
    upload->references = UINT32_MAX;
    assert(doe_dx_buffer_retain(upload) == DOE_DX_RANGE);
    assert(doe_dx_commands_copy(work, upload, 0, destinations[0], 0, 64, NULL) == DOE_DX_RANGE);
    upload->references = 1;
    for (size_t index = 0; index < 63; index++)
        assert(doe_dx_commands_copy(work, upload, 0, destinations[index], 0, 64, NULL) == DOE_DX_OK);
    assert(work->buffer_count == 64);
    mock.allocation_failure = mock.allocation_attempts + 1;
    assert(doe_dx_commands_copy(work, upload, 0, destinations[63], 0, 64, NULL) == DOE_DX_OUT_OF_MEMORY);
    assert(destinations[63]->owner == NULL && mock.copies == 63 && work->buffer_count == 64);
    mock.allocation_failure = 0;
    assert(doe_dx_commands_copy(work, upload, 0, destinations[63], 0, 64, NULL) == DOE_DX_OK);
    assert(work->buffer_count == 65 && mock.copies == 64);
    for (size_t index = 0; index < 64; index++) doe_dx_buffer_release(destinations[index]);
    doe_dx_buffer_release(upload);
    doe_dx_commands_release(work);
    finish(owner);
}

static void descriptor_identity_and_aliasing(void) {
    doe_dx_context *owner = context();
    doe_dx_commands *work = commands(owner);
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *storage = buffer(owner, DOE_DX_DEFAULT);
    const unsigned char bytecode = 1;
    doe_dx_binding_layout layout[2] = {{0, 0, 4, DOE_DX_READ_ONLY, DOE_DX_STRUCTURED}, {0, 0, 4, DOE_DX_READ_ONLY, DOE_DX_STRUCTURED}};
    doe_dx_pipeline_desc description = {&bytecode, 1, layout, 2};
    doe_dx_pipeline *shader = NULL;
    assert(doe_dx_pipeline_create(owner, &description, &shader, NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(!shader && !mock.pipelines);
    layout[1].access = DOE_DX_READ_WRITE;
    assert(doe_dx_pipeline_create(owner, &description, &shader, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(work, upload, 0, storage, 0, 64, NULL) == DOE_DX_OK);
    doe_dx_buffer_binding aliases[2] = {{storage, 0, 32}, {storage, 32, 32}};
    assert(doe_dx_commands_dispatch(work, shader, aliases, 2, 1, 1, 1, NULL) == DOE_DX_UNSUPPORTED);
    assert(!mock.dispatches && !work->pipeline_count);
    doe_dx_pipeline_release(shader);
    doe_dx_commands_release(work);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(storage);
    finish(owner);
}

static void mapping_failure_outputs(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *readback = buffer(owner, DOE_DX_READBACK);
    doe_dx_commands *work = commands(owner);
    unsigned char bytes[64] = {29};
    assert(doe_dx_buffer_write(upload, UINT64_MAX, bytes, 1, NULL) == DOE_DX_RANGE);
    mock.failure = MOCK_WRITE;
    assert(doe_dx_buffer_write(upload, 0, bytes, 64, NULL) == DOE_DX_NATIVE_FAILURE);
    mock.failure = MOCK_NONE;
    assert(doe_dx_commands_copy(work, upload, 0, readback, 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    mock.completed = 1;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK);
    mock.failure = MOCK_READ;
    assert(doe_dx_buffer_read(readback, 0, bytes, 64, NULL) == DOE_DX_NATIVE_FAILURE);
    assert(bytes[0] == 29 && bytes[1] == 0);
    doe_dx_commands_release(work);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(readback);
    finish(owner);
}

static void loss_from_other_native_calls(void) {
    doe_dx_context *owner = context();
    doe_dx_commands *work = commands(owner);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    mock.lost = true;
    doe_dx_buffer *output = NULL;
    assert(doe_dx_buffer_create(owner, 64, DOE_DX_UPLOAD, &output, NULL) == DOE_DX_DEVICE_LOST);
    assert(!output && owner->lost && !mock.commands);
    assert(doe_dx_commands_result(work, NULL) == DOE_DX_DEVICE_LOST);
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_DEVICE_LOST);
    doe_dx_commands_release(work);
    finish(owner);
}

static void failed_signal_after_prior_batch(void) {
    doe_dx_context *owner = context();
    doe_dx_buffer *uploads[2];
    uploads[0] = buffer(owner, DOE_DX_UPLOAD);
    uploads[1] = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *readbacks[2];
    readbacks[0] = buffer(owner, DOE_DX_READBACK);
    readbacks[1] = buffer(owner, DOE_DX_READBACK);
    doe_dx_commands *first = commands(owner);
    doe_dx_commands *second = commands(owner);
    assert(doe_dx_commands_copy(first, uploads[0], 0, readbacks[0], 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(second, uploads[1], 0, readbacks[1], 0, 64, NULL) == DOE_DX_OK);
    assert(!readbacks[0]->initialized && !readbacks[1]->initialized);
    assert(doe_dx_commands_submit(first, NULL) == DOE_DX_OK);
    mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(second, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(second);
    doe_dx_buffer_release(uploads[0]);
    doe_dx_buffer_release(uploads[1]);
    mock.completed = 1;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    assert(doe_dx_commands_result(first, NULL) == DOE_DX_OK);
    assert(readbacks[0]->initialized && !readbacks[1]->initialized);
    assert(readbacks[0]->owner == NULL && readbacks[1]->owner != NULL);
    assert(mock.commands == 1 && mock.buffers == 3);
    doe_dx_commands_release(first);
    mock.failure = MOCK_NONE;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_PENDING);
    assert(!readbacks[1]->initialized && mock.last_signal == 2);
    mock.completed = 2;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK);
    assert(readbacks[1]->initialized && readbacks[1]->owner == NULL);
    doe_dx_buffer_release(readbacks[0]);
    doe_dx_buffer_release(readbacks[1]);
    finish(owner);
}

int main(void) {
    mock_reset();
    lifetime_and_order();
    discard_initialization();
    unknown_completion();
    wait_and_observation_failures();
    device_loss();
    preflight_and_recording_failures();
    constructor_failures();
    partial_retirement_and_budgets();
    resource_and_reference_limits();
    descriptor_identity_and_aliasing();
    mapping_failure_outputs();
    loss_from_other_native_calls();
    failed_signal_after_prior_batch();
    puts("D3D12 C host owner: 13 groups passed (no SDK or GPU execution)");
    return 0;
}
