/* SPDX-License-Identifier: MIT
 * Real C command/error owners over the existing Metal native-call boundary.
 * No shader interpreter, GPU execution or hardware qualification. */
#include "../src/backend.h"
#include "../../backends/metal/tests/mock_native.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    mock_native mock;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(mock_driver(&mock), &context, NULL) == DOE_MTL_OK);
    native_backend backend = {.kind = WGPUBackendType_Metal, .value.metal.context = context};
    native_command_handle commands = {0};
    assert(native_command_create(&backend, &commands).status == NATIVE_OK);
    assert(native_command_submit(&commands).status == NATIVE_OK);
    strcpy(mock.completion_message, "owned terminal detail");
    mock_result(&mock, 5, INT64_C(4294967297));
    native_failure failure = native_command_wait(&commands);
    assert(failure.status == NATIVE_FAILURE && failure.native_code == INT64_C(4294967297));
    assert(!strcmp(failure.operation, "MTLCommandBuffer.error"));
    native_failure repeated = native_command_poll(&commands);
    assert(repeated.status == failure.status && repeated.native_code == failure.native_code);
    repeated = native_command_recover(&commands);
    assert(repeated.status == failure.status && !strcmp(repeated.message, failure.message));
    assert(native_command_release(&commands) == NATIVE_OK);
    doe_mtl_context_release(context);
    mock_assert_closed(&mock);
    strcpy(mock.completion_message, "mutated after release");
    char formatted[NATIVE_FAILURE_MESSAGE_CAPACITY];
    errno = EDOM;
    native_failure_format(&failure, "fallback", formatted);
    assert(errno == EDOM);
    assert(!strcmp(formatted, "Metal: MTLCommandBuffer.error (native=4294967297): owned terminal detail"));

    char operation[] = "vkWaitForFences";
    doe_vk_error error = {DOE_VK_DEVICE_LOST, -4, operation};
    failure = native_failure_vulkan(DOE_VK_DEVICE_LOST, &error);
    memset(operation, 'x', sizeof(operation) - 1);
    native_failure_format(&failure, "fallback", formatted);
    assert(!strcmp(formatted, "Vulkan: vkWaitForFences (native=-4)"));
    failure = native_failure_vulkan(DOE_VK_OUT_OF_MEMORY, &error);
    native_failure_format(&failure, "typed failure without matching detail", formatted);
    assert(!strcmp(formatted, "typed failure without matching detail"));
    char dx_operation[] = "ID3D12CommandQueue.Signal";
    doe_dx_error dx_error = {DOE_DX_COMPLETION_UNKNOWN, INT32_MIN, dx_operation};
    failure = native_failure_d3d12(DOE_DX_COMPLETION_UNKNOWN, &dx_error);
    memset(dx_operation, 'x', sizeof(dx_operation) - 1);
    native_failure_format(&failure, "fallback", formatted);
    assert(failure.status == NATIVE_UNKNOWN);
    assert(!strcmp(formatted, "D3D12: ID3D12CommandQueue.Signal (native=-2147483648)"));
    failure = native_failure_d3d12(DOE_DX_OUT_OF_MEMORY, &dx_error);
    native_failure_format(&failure, "allocation detail does not match status", formatted);
    assert(failure.status == NATIVE_OUT_OF_MEMORY);
    assert(!strcmp(formatted, "allocation detail does not match status"));
    assert(native_from_d3d12(DOE_DX_DEVICE_LOST) == NATIVE_DEVICE_LOST);
    assert(native_from_d3d12(DOE_DX_UNAVAILABLE) == NATIVE_UNAVAILABLE);
    assert(native_from_d3d12(DOE_DX_DEVICE_MISMATCH) == NATIVE_INVALID_ARGUMENT);
    failure = native_failure_status(NATIVE_OK);
    native_failure_format(&failure, "must not appear on success", formatted);
    assert(!formatted[0]);

    doe_mtl_error longest = {.status = DOE_MTL_NATIVE_FAILURE, .native_code = INT64_MIN};
    char long_operation[128];
    memset(long_operation, 'o', sizeof(long_operation) - 1);
    long_operation[sizeof(long_operation) - 1] = '\0';
    longest.operation = long_operation;
    memset(longest.message, 'm', sizeof(longest.message) - 1);
    failure = native_failure_metal(longest.status, &longest);
    native_failure_format(&failure, "fallback", formatted);
    assert(strlen(formatted) < sizeof(formatted));
    assert(strstr(formatted, "native=-9223372036854775808"));
    assert(strstr(formatted, longest.message));
    puts("PASS native failure transport: real command wait/poll/recover first detail, copied lifetime, signed 64-bit code, exact formatting and bounded text");
}
