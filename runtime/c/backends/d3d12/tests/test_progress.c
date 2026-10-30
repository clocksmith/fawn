/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

static void lease_survives_shell(bool lose)
{
    doe_dx_context *context = NULL;
    assert(doe_dx_context_create(NULL, &context, NULL) == DOE_DX_OK);
    doe_dx_buffer *upload = NULL, *destination = NULL;
    assert(doe_dx_buffer_create(context, 64, DOE_DX_UPLOAD, &upload, NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context, 64, DOE_DX_DEFAULT, &destination, NULL) == DOE_DX_OK);
    doe_dx_commands *commands = NULL, *other = NULL;
    assert(doe_dx_commands_create(context, &commands, NULL) == DOE_DX_OK);
    doe_dx_wait_lease lease = {0};
    assert(doe_dx_commands_hold(commands, &lease, NULL) == DOE_DX_INVALID_STATE);
    assert(!lease.context && !lease.record);
    assert(doe_dx_commands_copy(commands, upload, 0, destination, 0, 64, NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_owner(destination) == commands);
    assert(doe_dx_commands_create(context, &other, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(other, upload, 0, destination, 0, 64, NULL) == DOE_DX_BUSY);
    assert(doe_dx_commands_conflict(other) == commands);
    doe_dx_commands_release(other);
    assert(doe_dx_commands_submit(commands, NULL) == DOE_DX_OK);
    size_t objects = context->objects;
    assert(doe_dx_commands_hold(commands, &lease, NULL) == DOE_DX_OK);
    assert(context->objects == objects + 1);
    assert(doe_dx_commands_block(&lease, 0, NULL) == DOE_DX_PENDING);
    assert(doe_dx_commands_block(&lease, 1, NULL) == DOE_DX_PENDING);
    assert(!mock.recycled_commands);
    doe_dx_commands_release(commands);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(destination);
    assert(doe_dx_context_destroy(context) == DOE_DX_BUSY);
    if (lose) {
        mock.lost = true;
        assert(doe_dx_commands_block(&lease, 1, NULL) == DOE_DX_DEVICE_LOST);
        assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_DEVICE_LOST);
    } else {
        mock.completed = 1;
        assert(doe_dx_context_poll(context, 0, NULL) == DOE_DX_OK);
        assert(doe_dx_commands_block(&lease, 1, NULL) == DOE_DX_OK);
    }
    /* The original command and every private buffer have retired, while the
     * event observer still owns one obligation on the existing context. */
    assert(!mock.commands && !mock.buffers && context->objects == 1);
    assert(mock.recycled_commands == (lose ? 0u : 1u));
    assert(doe_dx_context_destroy(context) == DOE_DX_BUSY);
    assert(doe_dx_commands_unhold(&lease, NULL) == DOE_DX_OK);
    assert(!lease.context && !lease.record);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

/* Accepted commands already retain both buffers and share one native queue.
 * A later GPU reader must record without requiring CPU completion first. */
static void accepted_buffer_ordering(bool lose)
{
    doe_dx_context *context = NULL;
    doe_dx_buffer *upload = NULL, *gpu = NULL, *readback = NULL;
    doe_dx_commands *first = NULL, *next = NULL;
    assert(doe_dx_context_create(NULL,&context,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_UPLOAD,&upload,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_DEFAULT,&gpu,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_READBACK,&readback,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&first,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(first,upload,0,gpu,0,64,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(first,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_result(first,NULL) == DOE_DX_PENDING);
    assert(doe_dx_buffer_owner(gpu) == first);
    assert(doe_dx_commands_create(context,&next,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(next,gpu,0,readback,0,64,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(next,NULL) == DOE_DX_OK);
    unsigned char bytes[64] = {0};
    assert(doe_dx_buffer_write(upload,0,bytes,sizeof(bytes),NULL) == DOE_DX_BUSY);
    assert(doe_dx_buffer_read(readback,0,bytes,sizeof(bytes),NULL) == DOE_DX_BUSY);
    doe_dx_commands_release(first);
    doe_dx_commands_release(next);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(gpu);
    doe_dx_buffer_release(readback);
    assert(doe_dx_context_destroy(context) == DOE_DX_BUSY);
    if (lose) {
        mock.lost = true;
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_DEVICE_LOST);
    } else {
        mock.completed = 2;
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_OK);
    }
    assert(!mock.commands && !mock.buffers);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void retirement_preserves_later_identities(void)
{
    doe_dx_context *context = NULL;
    doe_dx_buffer *upload = NULL, *gpu = NULL, *readback = NULL;
    doe_dx_commands *first = NULL, *second = NULL, *reserved = NULL, *third = NULL;
    assert(doe_dx_context_create(NULL,&context,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_UPLOAD,&upload,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_DEFAULT,&gpu,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_READBACK,&readback,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&first,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(first,upload,0,gpu,0,64,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(first,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&second,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(second,gpu,0,readback,0,64,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(second,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&reserved,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(reserved,upload,0,gpu,0,64,NULL) == DOE_DX_OK);
    doe_dx_commands_release(first);
    doe_dx_commands_release(second);
    mock.completed = 1;
    assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_PENDING);
    assert(gpu->owner == reserved && gpu->pending == second);
    doe_dx_commands_release(reserved);
    assert(!gpu->owner && doe_dx_buffer_owner(gpu) == second);
    assert(doe_dx_commands_create(context,&third,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(third,gpu,0,readback,0,64,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(third,NULL) == DOE_DX_OK);
    doe_dx_commands_release(third);
    mock.completed = 2;
    assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_PENDING);
    assert(gpu->pending == third && readback->pending == third);
    unsigned char bytes[64] = {0};
    assert(doe_dx_buffer_read(readback,0,bytes,sizeof(bytes),NULL) == DOE_DX_BUSY);
    assert(doe_dx_commands_create(context,&reserved,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(reserved,upload,0,gpu,0,64,NULL) == DOE_DX_OK);
    mock.completed = 3;
    assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_OK);
    assert(gpu->owner == reserved && !gpu->pending);
    assert(doe_dx_buffer_owner(gpu) == reserved);
    assert(doe_dx_buffer_read(readback,0,bytes,sizeof(bytes),NULL) == DOE_DX_OK);
    doe_dx_commands_release(reserved);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(gpu);
    doe_dx_buffer_release(readback);
    assert(!mock.commands && !mock.buffers);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void unknown_signal_keeps_reservation(bool lose)
{
    doe_dx_context *context = NULL;
    doe_dx_buffer *upload = NULL, *gpu = NULL, *readback = NULL;
    doe_dx_commands *first = NULL, *second = NULL;
    assert(doe_dx_context_create(NULL,&context,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_UPLOAD,&upload,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_DEFAULT,&gpu,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_READBACK,&readback,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&first,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(first,upload,0,gpu,0,64,NULL) == DOE_DX_OK);
    mock.failure = MOCK_SIGNAL;
    assert(doe_dx_commands_submit(first,NULL) == DOE_DX_COMPLETION_UNKNOWN);
    assert(gpu->owner == first && !gpu->pending && !gpu->initialized);
    assert(doe_dx_commands_create(context,&second,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(second,gpu,0,readback,0,64,NULL) == DOE_DX_BUSY);
    mock.failure = MOCK_NONE;
    assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_PENDING);
    assert(!gpu->owner && gpu->pending == first && !gpu->initialized && gpu->accepted_initialized);
    assert(doe_dx_commands_copy(second,gpu,0,readback,0,64,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(second,NULL) == DOE_DX_OK);
    unsigned char bytes[64] = {0};
    assert(doe_dx_buffer_read(readback,0,bytes,sizeof(bytes),NULL) == DOE_DX_BUSY);
    doe_dx_commands_release(first);
    doe_dx_commands_release(second);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(gpu);
    doe_dx_buffer_release(readback);
    assert(!mock.recycled_commands);
    if (lose) {
        mock.lost = true;
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_DEVICE_LOST);
    } else {
        mock.completed = 2;
        assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_OK);
    }
    assert(mock.recycled_commands == (lose ? 0u : 2u));
    assert(!mock.commands && !mock.buffers);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

static void partial_then_full_queued_initialization(void)
{
    doe_dx_context *context = NULL;
    doe_dx_buffer *upload = NULL, *gpu = NULL, *readback = NULL;
    doe_dx_commands *partial = NULL, *full = NULL, *read = NULL;
    assert(doe_dx_context_create(NULL,&context,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_UPLOAD,&upload,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_DEFAULT,&gpu,NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_READBACK,&readback,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_create(context,&partial,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(partial,upload,0,gpu,0,32,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(partial,NULL) == DOE_DX_OK);
    assert(!gpu->initialized && !gpu->accepted_initialized);
    assert(doe_dx_commands_create(context,&read,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(read,gpu,0,readback,0,64,NULL) == DOE_DX_UNINITIALIZED);
    assert(doe_dx_commands_create(context,&full,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(full,upload,0,gpu,0,64,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(full,NULL) == DOE_DX_OK);
    assert(!gpu->initialized && gpu->accepted_initialized);
    assert(doe_dx_commands_copy(read,gpu,0,readback,0,64,NULL) == DOE_DX_OK);
    assert(doe_dx_commands_submit(read,NULL) == DOE_DX_OK);
    doe_dx_commands_release(partial);
    doe_dx_commands_release(full);
    doe_dx_commands_release(read);
    mock.completed = 3;
    assert(doe_dx_context_poll(context,0,NULL) == DOE_DX_OK);
    /* The older partial use retires last; it cannot revoke later full proof. */
    assert(gpu->initialized && readback->initialized);
    assert(!gpu->owner && !gpu->pending && !gpu->accepted_initialized);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(gpu);
    doe_dx_buffer_release(readback);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

int main(void)
{
    mock_reset();
    accepted_buffer_ordering(false);
    accepted_buffer_ordering(true);
    retirement_preserves_later_identities();
    unknown_signal_keeps_reservation(false);
    unknown_signal_keeps_reservation(true);
    partial_then_full_queued_initialization();
    lease_survives_shell(false);
    lease_survives_shell(true);
    puts("PASS D3D12 host progress ownership: ordered accepted buffers, reserved/unknown conflicts, partial retirement, CPU exclusion, timeout/cancellation, early release, loss and leased teardown; no SDK/GPU");
    return 0;
}
