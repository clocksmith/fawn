/* SPDX-License-Identifier: MIT
 * Official SDK event/fence call composition with injected outcomes; no GPU. */
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct event_control { bool signaled, pending, closed; } event_control;
typedef struct control {
    event_control events[8];
    unsigned creates, registrations, closes, wait_calls;
    bool create_failure, close_failure, wait_failure;
    HRESULT registration_result, removed;
    uint64_t completed;
} control;
static control seen;

static HANDLE test_create(LPSECURITY_ATTRIBUTES attributes, BOOL manual,
    BOOL initial, LPCWSTR name)
{
    assert(!attributes && manual && !initial && !name);
    if (seen.create_failure) return NULL;
    assert(seen.creates < 8);
    return (HANDLE)&seen.events[seen.creates++];
}
static DWORD test_wait(HANDLE handle, DWORD timeout)
{
    event_control *event = handle;
    assert(!event->closed);
    seen.wait_calls++;
    if (seen.wait_failure) return WAIT_FAILED;
    if (event->signaled) return WAIT_OBJECT_0;
    return timeout == INFINITE ? WAIT_FAILED : WAIT_TIMEOUT;
}
static BOOL test_close(HANDLE handle)
{
    event_control *event = handle;
    assert(!event->pending && !event->closed);
    if (seen.close_failure) return FALSE;
    event->closed = true;
    seen.closes++;
    return TRUE;
}
#define CreateEventW test_create
#define WaitForSingleObject test_wait
#define CloseHandle test_close
#include "../src/windows_context.c"
#include "../src/windows_progress.c"

static HRESULT STDMETHODCALLTYPE removed(ID3D12Device *device)
{
    (void)device;
    return seen.removed;
}
static UINT64 STDMETHODCALLTYPE completed(ID3D12Fence *fence)
{
    (void)fence;
    return seen.completed;
}
static HRESULT STDMETHODCALLTYPE registration(ID3D12Fence *fence, UINT64 value, HANDLE handle)
{
    (void)fence;
    assert(value == 7);
    event_control *event = handle;
    assert(!event->pending && !event->closed);
    seen.registrations++;
    if (SUCCEEDED(seen.registration_result)) event->pending = true;
    return seen.registration_result;
}
int main(void)
{
    ID3D12DeviceVtbl device_methods = {0};
    device_methods.GetDeviceRemovedReason = removed;
    ID3D12Device device = {&device_methods};
    ID3D12FenceVtbl fence_methods = {0};
    fence_methods.GetCompletedValue = completed;
    fence_methods.SetEventOnCompletion = registration;
    ID3D12Fence fence = {&fence_methods};
    doe_dx_native_context context = {.device=&device,.fence=&fence};
    doe_dx_native_context foreign = {0};
    doe_dx_wait_record *first = NULL, *second = NULL;
    assert(doe_dx_native_progress_hold(&context, 7, &first, NULL) == DOE_DX_OK);
    assert(doe_dx_native_progress_block(first, 1, NULL) == DOE_DX_PENDING);
    assert(doe_dx_native_progress_unhold(&foreign, first, NULL) == DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_native_progress_close(&context) == DOE_DX_BUSY && !seen.closes);
    assert(doe_dx_native_progress_unhold(&context, first, NULL) == DOE_DX_OK);
    assert(context.waits && seen.creates == 1 && seen.registrations == 1 && !seen.closes);
    doe_dx_wait_record *reused = NULL;
    assert(doe_dx_native_progress_hold(&context, 7, &reused, NULL) == DOE_DX_OK);
    assert(reused == first && seen.creates == 1 && seen.registrations == 1);
    assert(doe_dx_native_progress_hold(&context, 7, &second, NULL) == DOE_DX_OK);
    assert(second != first && seen.creates == 2 && seen.registrations == 2);
    seen.wait_failure = true;
    assert(doe_dx_native_progress_block(first, 1, NULL) == DOE_DX_NATIVE_FAILURE);
    assert(doe_dx_native_progress_unhold(&context, first, NULL) == DOE_DX_COMPLETION_UNKNOWN);
    assert(!seen.closes && doe_dx_native_progress_close(&context) == DOE_DX_BUSY);
    seen.wait_failure = false;
    seen.removed = DXGI_ERROR_DEVICE_REMOVED;
    assert(doe_dx_native_progress_block(second, 1, NULL) == DOE_DX_DEVICE_LOST);
    for (unsigned i = 0; i < seen.creates; ++i) {
        seen.events[i].pending = false;
        seen.events[i].signaled = true;
    }
    seen.close_failure = true;
    assert(doe_dx_native_progress_unhold(&context, second, NULL) == DOE_DX_NATIVE_FAILURE);
    assert(!seen.closes && context.waits);
    seen.close_failure = false;
    assert(doe_dx_native_progress_close(&context) == DOE_DX_OK);
    assert(!context.waits && seen.closes == 2);
    seen.removed = S_OK;
    seen.registration_result = E_OUTOFMEMORY;
    seen.close_failure = true;
    doe_dx_wait_record *unchanged = (doe_dx_wait_record *)(uintptr_t)1;
    assert(doe_dx_native_progress_hold(&context, 7, &unchanged, NULL) == DOE_DX_OUT_OF_MEMORY);
    assert(unchanged == (doe_dx_wait_record *)(uintptr_t)1 && context.waits);
    unsigned waits = seen.wait_calls;
    seen.close_failure = false;
    assert(doe_dx_native_progress_close(&context) == DOE_DX_OK);
    assert(!context.waits && seen.wait_calls == waits);
    seen.create_failure = true;
    assert(doe_dx_native_progress_hold(&context, 7, &unchanged, NULL) == DOE_DX_NATIVE_FAILURE);
    assert(!context.waits && unchanged == (doe_dx_wait_record *)(uintptr_t)1);
    puts("PASS actual SDK progress composition: concurrent leases, timeout/cancel reuse, active-handle protection, loss and registration/close failure cleanup; no GPU");
    return 0;
}
