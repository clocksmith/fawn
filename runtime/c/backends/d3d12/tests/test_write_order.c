/* SPDX-License-Identifier: MIT
 * Buffer-write ordering inside one recording: actual command/resource owner at
 * the typed native-call seam. Proves call composition only, not GPU ordering. */
#include "mock_native.h"
#include <stdio.h>

static doe_dx_buffer *buffer(doe_dx_context *owner, doe_dx_memory memory)
{
    doe_dx_buffer *result = NULL;
    assert(doe_dx_buffer_create(owner, 64, memory, &result, NULL) == DOE_DX_OK);
    return result;
}

static void default_writes_are_ordered_in_chunks(void)
{
    doe_dx_context *owner = NULL;
    assert(doe_dx_context_create(NULL, &owner, NULL) == DOE_DX_OK);
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD), *targets[17];
    doe_dx_commands *work = NULL;
    assert(doe_dx_commands_create(owner, &work, NULL) == DOE_DX_OK);
    /* Nothing used yet: no native call. */
    assert(doe_dx_commands_order_buffer_writes(work, NULL) == DOE_DX_OK && !mock.buffer_orders);
    for (size_t index = 0; index < 17; ++index) {
        targets[index] = buffer(owner, DOE_DX_DEFAULT);
        assert(doe_dx_commands_copy(work, upload, 0, targets[index], 0, 64, NULL) == DOE_DX_OK);
    }
    /* The UPLOAD source is fixed-state and skipped; 17 DEFAULT uses take two calls. */
    assert(doe_dx_commands_order_buffer_writes(work, NULL) == DOE_DX_OK);
    assert(mock.buffer_orders == 2 && mock.ordered_buffers == 17 && mock.copies_before_order == 17);
    /* A later copy into an ordered buffer is recorded after the barriers. */
    assert(doe_dx_commands_copy(work, upload, 0, targets[0], 0, 64, NULL) == DOE_DX_OK);
    assert(mock.copies == 18 && mock.copies_before_order == 17);
    assert(doe_dx_commands_submit(work, NULL) == DOE_DX_OK);
    /* Submitted commands are no longer recording. */
    assert(doe_dx_commands_order_buffer_writes(work, NULL) == DOE_DX_INVALID_STATE);
    assert(mock.buffer_orders == 2);
    doe_dx_commands_release(work);
    mock.completed = 1;
    assert(doe_dx_context_poll(owner, 0, NULL) == DOE_DX_OK && !mock.commands);
    for (size_t index = 0; index < 17; ++index) doe_dx_buffer_release(targets[index]);
    doe_dx_buffer_release(upload);
    assert(doe_dx_context_destroy(owner) == DOE_DX_OK);
    mock_reset();
}

static void fixed_state_destination_records_nothing(void)
{
    doe_dx_context *owner = NULL;
    assert(doe_dx_context_create(NULL, &owner, NULL) == DOE_DX_OK);
    doe_dx_buffer *upload = buffer(owner, DOE_DX_UPLOAD);
    doe_dx_buffer *storage = buffer(owner, DOE_DX_DEFAULT), *readback = buffer(owner, DOE_DX_READBACK);
    doe_dx_commands *work = NULL;
    assert(doe_dx_commands_create(owner, &work, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(work, upload, 0, storage, 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(work, upload, 0, readback, 0, 64, NULL) == DOE_DX_OK);
    doe_dx_error error = {0};
    assert(doe_dx_commands_order_buffer_writes(work, &error) == DOE_DX_UNSUPPORTED);
    assert(error.status == DOE_DX_UNSUPPORTED && !mock.buffer_orders && !mock.ordered_buffers);
    /* The rejected call left the recording intact and usable. */
    assert(doe_dx_commands_copy(work, upload, 0, storage, 0, 64, NULL) == DOE_DX_OK);
    doe_dx_commands_release(work);
    assert(!mock.commands);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(storage);
    doe_dx_buffer_release(readback);
    assert(doe_dx_context_destroy(owner) == DOE_DX_OK);
    mock_reset();
}

int main(void)
{
    mock_reset();
    default_writes_are_ordered_in_chunks();
    fixed_state_destination_records_nothing();
    puts("PASS D3D12 recorded buffer-write ordering: DEFAULT uses transition in 16-barrier chunks, fixed-state READBACK rejects without recording, submitted commands reject; native-call seam only");
    return 0;
}
