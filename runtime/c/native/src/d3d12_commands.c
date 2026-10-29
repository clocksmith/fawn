/* SPDX-License-Identifier: MIT */
#include "d3d12_frontend.h"

native_failure native_d3d12_command_create(const native_backend *backend, native_command_handle *out)
{
#if defined(_WIN32)
    native_command_handle result = {.kind = WGPUBackendType_D3D12};
    result.value.d3d12.context = backend->value.d3d12.context;
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_commands_create(result.value.d3d12.context,
        &result.value.d3d12.commands, &error);
    if (status == DOE_DX_OK) *out = result;
    return native_failure_d3d12(status, &error);
#else
    (void)backend;
    (void)out;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

native_failure native_d3d12_command_submit(native_command_handle *commands)
{
#if defined(_WIN32)
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_commands_submit(commands->value.d3d12.commands, &error);
    return native_failure_d3d12(status, &error);
#else
    (void)commands;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

native_failure native_d3d12_command_progress(native_command_handle *commands, uint32_t wait_ms)
{
#if defined(_WIN32)
    doe_dx_error progress = {0}, terminal = {0};
    doe_dx_status polled = doe_dx_context_poll(commands->value.d3d12.context, wait_ms, &progress);
    doe_dx_status result = doe_dx_commands_result(commands->value.d3d12.commands, &terminal);
    if ((result == DOE_DX_PENDING || result == DOE_DX_COMPLETION_UNKNOWN) &&
        polled != DOE_DX_OK && polled != DOE_DX_PENDING)
        return native_failure_d3d12(polled, &progress);
    return native_failure_d3d12(result, &terminal);
#else
    (void)commands;
    (void)wait_ms;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

native_result native_d3d12_command_release(native_command_handle *commands)
{
#if defined(_WIN32)
    /* Caller release alone is not completion. Keep the frontend's pointer and
     * pending device lease until the sole backend owner proves terminal state.
     * INVALID_STATE here is the canonical never-submitted recording state. */
    doe_dx_status status = doe_dx_commands_result(commands->value.d3d12.commands, NULL);
    if (status == DOE_DX_PENDING || status == DOE_DX_COMPLETION_UNKNOWN) return NATIVE_BUSY;
    if (status == DOE_DX_INVALID_ARGUMENT) return NATIVE_INVALID_ARGUMENT;
    doe_dx_commands_release(commands->value.d3d12.commands);
    return NATIVE_OK;
#else
    (void)commands;
    return NATIVE_UNAVAILABLE;
#endif
}

bool native_d3d12_command_in_flight(const native_command_handle *commands)
{
#if defined(_WIN32)
    return doe_dx_commands_result(commands->value.d3d12.commands, NULL) == DOE_DX_PENDING;
#else
    (void)commands;
    return false;
#endif
}
