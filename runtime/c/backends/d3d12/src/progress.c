/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_dx_commands *doe_dx_buffer_owner(const doe_dx_buffer *buffer)
{
    return buffer ? buffer->owner : NULL;
}

doe_dx_commands *doe_dx_commands_conflict(const doe_dx_commands *commands)
{
    return commands ? commands->conflict : NULL;
}

doe_dx_status doe_dx_commands_hold(doe_dx_commands *commands,
    doe_dx_wait_lease *out, doe_dx_error *error)
{
    if (!commands || !out || out->context || out->record)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.hold", error);
    doe_dx_context *context = commands->context;
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "commands.hold", error);
    if (commands->state != DOE_DX_SUBMITTED || !commands->fence)
        return doe_dx_fail(DOE_DX_INVALID_STATE, 0, "commands.hold", error);
    if (context->objects == SIZE_MAX)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.hold", error);
    doe_dx_wait_record *record = NULL;
    doe_dx_status status = doe_dx_native_progress_hold(context->native,
        commands->fence, &record, error);
    if (status != DOE_DX_OK) return doe_dx_context_result(context, status, error);
    context->objects++;
    *out = (doe_dx_wait_lease){context, record};
    return doe_dx_fail(DOE_DX_OK, 0, "commands.hold", error);
}

doe_dx_status doe_dx_commands_block(const doe_dx_wait_lease *lease,
    uint32_t wait_ms, doe_dx_error *error)
{
    if (!lease || !lease->context || !lease->record || wait_ms == UINT32_MAX)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.block", error);
    /* No command shell or mutable context state is touched by this read-only
     * native fence observer. Only ordinary serialized polling settles work. */
    return doe_dx_native_progress_block(lease->record, wait_ms, error);
}

doe_dx_status doe_dx_commands_unhold(doe_dx_wait_lease *lease, doe_dx_error *error)
{
    if (!lease || !lease->context || !lease->record)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.unhold", error);
    doe_dx_context *context = lease->context;
    doe_dx_status status = doe_dx_native_progress_unhold(context->native, lease->record, error);
    if (status == DOE_DX_INVALID_ARGUMENT) return status;
    context->objects--;
    *lease = (doe_dx_wait_lease){0};
    return status;
}
