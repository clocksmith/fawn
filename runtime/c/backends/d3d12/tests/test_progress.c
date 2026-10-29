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
    assert(doe_dx_context_destroy(context) == DOE_DX_BUSY);
    assert(doe_dx_commands_unhold(&lease, NULL) == DOE_DX_OK);
    assert(!lease.context && !lease.record);
    assert(doe_dx_context_destroy(context) == DOE_DX_OK);
    mock_reset();
}

int main(void)
{
    mock_reset();
    lease_survives_shell(false);
    lease_survives_shell(true);
    puts("PASS D3D12 host progress ownership: accepted conflicts, timeout/cancellation, early shell release, terminal loss and leased-context teardown; no SDK/GPU");
    return 0;
}
