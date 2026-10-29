/* SPDX-License-Identifier: MIT
 * Official SDK call composition with injected OS outcomes, never a GPU test. */
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct wait_control {
    uint64_t completed, registered_value;
    ULONGLONG clock;
    HRESULT removed, registration_result;
    DWORD wait_result;
    uint64_t complete_on_wait;
    unsigned creates, registrations, waits, closes, frees;
    bool pending, create_failure, close_failure, timeout_after_wake;
} wait_control;
static wait_control seen;

static HANDLE test_create_event(LPSECURITY_ATTRIBUTES attributes, BOOL manual,
    BOOL initial, LPCWSTR name) {
    assert(!attributes && !manual && !initial && !name);
    seen.creates++;
    return seen.create_failure ? NULL : (HANDLE)&seen;
}
static DWORD test_wait(HANDLE event, DWORD timeout) {
    assert(event == (HANDLE)&seen && seen.pending);
    seen.waits++;
    DWORD result = seen.wait_result;
    if (seen.wait_result == WAIT_OBJECT_0) {
        seen.pending = false;
        seen.completed = seen.complete_on_wait;
        if (seen.timeout_after_wake) {
            seen.wait_result = WAIT_TIMEOUT;
            seen.timeout_after_wake = false;
        }
    } else if (seen.wait_result == WAIT_TIMEOUT && timeout != INFINITE) {
        seen.clock += timeout;
    }
    return result;
}
static BOOL test_close(HANDLE event) {
    assert(event == (HANDLE)&seen && !seen.pending);
    seen.closes++;
    return !seen.close_failure;
}
static ULONGLONG test_clock(void) { return seen.clock; }
static void test_free(void *pointer) { seen.frees++; free(pointer); }

#define CreateEventW test_create_event
#define WaitForSingleObject test_wait
#define CloseHandle test_close
#define GetTickCount64 test_clock
#define free test_free
#include "../src/windows_context.c"
#undef free

static HRESULT STDMETHODCALLTYPE removed(ID3D12Device *device) {
    (void)device;
    return seen.removed;
}
static UINT64 STDMETHODCALLTYPE completed(ID3D12Fence *fence) {
    (void)fence;
    return seen.completed;
}
static HRESULT STDMETHODCALLTYPE register_event(ID3D12Fence *fence, UINT64 value,
    HANDLE event) {
    (void)fence;
    assert(event == (HANDLE)&seen && !seen.pending);
    seen.registrations++;
    seen.registered_value = value;
    if (SUCCEEDED(seen.registration_result)) seen.pending = true;
    return seen.registration_result;
}
static ULONG STDMETHODCALLTYPE release_device(ID3D12Device *device) {
    (void)device;
    assert(!seen.pending);
    return 0;
}
static ULONG STDMETHODCALLTYPE release_fence(ID3D12Fence *fence) {
    (void)fence;
    assert(!seen.pending);
    return 0;
}

int main(void) {
    ID3D12DeviceVtbl device_methods = {0};
    device_methods.GetDeviceRemovedReason = removed;
    device_methods.Release = release_device;
    ID3D12Device device = {&device_methods};
    ID3D12FenceVtbl fence_methods = {0};
    fence_methods.GetCompletedValue = completed;
    fence_methods.SetEventOnCompletion = register_event;
    fence_methods.Release = release_fence;
    ID3D12Fence fence = {&fence_methods};
    doe_dx_native_context *context = calloc(1, sizeof(*context));
    assert(context);
    context->device = &device;
    context->fence = &fence;
    doe_dx_error error = {0};
    assert(doe_dx_native_wait(context, 1, 0, &error) == DOE_DX_PENDING);
    assert(!seen.creates && !seen.registrations);
    seen.completed = 1;
    assert(doe_dx_native_wait(context, 1, 20, &error) == DOE_DX_OK);
    assert(!seen.creates);
    seen.create_failure = true;
    assert(doe_dx_native_wait(context, 2, 20, &error) == DOE_DX_NATIVE_FAILURE);
    assert(!context->completion_event && !context->completion_registered);
    seen.create_failure = false;
    seen.registration_result = E_OUTOFMEMORY;
    assert(doe_dx_native_wait(context, 2, 20, &error) == DOE_DX_OUT_OF_MEMORY);
    assert(!context->completion_registered && !seen.pending);
    seen.registration_result = S_OK;
    seen.wait_result = WAIT_TIMEOUT;
    assert(doe_dx_native_wait(context, 2, 20, &error) == DOE_DX_PENDING);
    unsigned registrations = seen.registrations;
    assert(context->completion_registered && seen.pending && !seen.closes);
    /* Abandoning a wait is cancellation of observation, never native work. */
    assert(doe_dx_native_wait(context, 2, 20, &error) == DOE_DX_PENDING);
    assert(seen.registrations == registrations && !seen.closes && !seen.frees);
    seen.wait_result = WAIT_FAILED;
    assert(doe_dx_native_wait(context, 2, 20, &error) == DOE_DX_NATIVE_FAILURE);
    assert(context->completion_registered && seen.pending && !seen.closes);
    seen.completed = 2;
    assert(doe_dx_native_close(context) == DOE_DX_COMPLETION_UNKNOWN);
    assert(context->completion_registered && seen.pending && !seen.closes && !seen.frees);
    /* An older retained event wakes first; it cannot complete newer work. */
    seen.wait_result = WAIT_OBJECT_0;
    seen.complete_on_wait = 2;
    seen.timeout_after_wake = true;
    assert(doe_dx_native_wait(context, 3, 20, &error) == DOE_DX_PENDING);
    assert(context->completion_registered && seen.pending);
    assert(seen.registered_value == 3 && seen.registrations == registrations + 1);
    seen.wait_result = WAIT_OBJECT_0;
    seen.complete_on_wait = 3;
    assert(doe_dx_native_wait(context, 3, 20, &error) == DOE_DX_OK);
    assert(!context->completion_registered && !seen.pending);
    seen.complete_on_wait = 4;
    assert(doe_dx_native_wait(context, 4, 20, &error) == DOE_DX_OK);
    assert(seen.registered_value == 4 && seen.registrations == registrations + 2);
    /* A fence sentinel and a removed device never become successful output. */
    seen.completed = UINT64_MAX;
    assert(doe_dx_native_wait(context, 5, 20, &error) == DOE_DX_DEVICE_LOST);
    seen.completed = 4;
    seen.complete_on_wait = UINT64_MAX;
    assert(doe_dx_native_wait(context, 5, 20, &error) == DOE_DX_DEVICE_LOST);
    assert(!context->completion_registered && !seen.pending);
    seen.completed = 4;
    seen.wait_result = WAIT_TIMEOUT;
    assert(doe_dx_native_wait(context, 5, 20, &error) == DOE_DX_PENDING);
    seen.removed = DXGI_ERROR_DEVICE_REMOVED;
    assert(doe_dx_native_wait(context, 5, 20, &error) == DOE_DX_DEVICE_LOST);
    assert(context->completion_registered && seen.pending && !seen.closes);
    /* Terminal loss signals monitored fences; teardown must consume it. */
    seen.wait_result = WAIT_OBJECT_0;
    seen.complete_on_wait = UINT64_MAX;
    seen.close_failure = true;
    assert(doe_dx_native_close(context) == DOE_DX_NATIVE_FAILURE);
    assert(!context->completion_registered && !seen.pending && !seen.frees);
    seen.close_failure = false;
    assert(doe_dx_native_close(context) == DOE_DX_OK);
    assert(seen.closes == 2 && seen.frees == 1);
    puts("PASS actual SDK wait composition: completion, repeated timeout/cancellation, failures, loss and retained cleanup; no GPU");
    return 0;
}
