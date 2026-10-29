/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "progress.h"

bool doe_mtl_commands_in_flight(const doe_mtl_commands *commands)
{
    return commands && commands->state == DOE_MTL_SUBMITTED;
}

doe_mtl_commands *doe_mtl_commands_conflict(const doe_mtl_commands *commands)
{
    return commands ? commands->conflict : NULL;
}

doe_mtl_commands *doe_mtl_buffer_owner(const doe_mtl_buffer *buffer)
{
    return buffer ? buffer->owner : NULL;
}

bool doe_mtl_commands_matches(const doe_mtl_commands *commands, const doe_mtl_wait_lease *lease)
{
    return commands && lease && commands->context == lease->context && commands->native == lease->command;
}

doe_mtl_status doe_mtl_commands_hold(doe_mtl_commands *commands, doe_mtl_wait_lease *out)
{
    if (!out || !doe_mtl_commands_in_flight(commands)) return DOE_MTL_INVALID_STATE;
    doe_mtl_context *context = commands->context;
    if (!context->native.retain || !context->native.block) return DOE_MTL_UNSUPPORTED;
    doe_mtl_status status = doe_mtl_context_retain(context);
    if (status != DOE_MTL_OK) return status;
    void *native = context->native.retain(context->native.userdata, commands->native);
    if (!native) { doe_mtl_context_release(context); return DOE_MTL_NATIVE_FAILURE; }
    *out = (doe_mtl_wait_lease){context,native,context->timeline,commands->timeline_value};
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_commands_block(const doe_mtl_wait_lease *lease, uint64_t timeout_ns)
{
    if (!lease || !lease->context || !lease->command) return DOE_MTL_INVALID_ARGUMENT;
    const doe_mtl_native *native = &lease->context->native;
    return native->block(native->userdata,lease->command,lease->event,lease->value,timeout_ns);
}

void doe_mtl_commands_unhold(doe_mtl_wait_lease *lease)
{
    if (!lease || !lease->context || !lease->command) abort();
    doe_mtl_context *context = lease->context;
    context->native.release(context->native.userdata,lease->command);
    *lease = (doe_mtl_wait_lease){0};
    doe_mtl_context_release(context);
}
