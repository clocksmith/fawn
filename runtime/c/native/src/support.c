/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void *native_allocate(void *state, const void *vtable, size_t size, size_t alignment)
{
    (void)state;
    (void)vtable;
    if (alignment > _Alignof(max_align_t)) return NULL;
    return malloc(size ? size : 1);
}

void native_deallocate(void *state, const void *vtable, void *memory,
    size_t size, size_t alignment)
{
    (void)state;
    (void)vtable;
    (void)size;
    (void)alignment;
    free(memory);
}

void native_retain_count(atomic_uint *count)
{
    unsigned previous = atomic_load_explicit(count, memory_order_relaxed);
    for (;;) {
        if (!previous || previous == UINT_MAX) abort();
        if (atomic_compare_exchange_weak_explicit(count, &previous, previous + 1,
                memory_order_relaxed, memory_order_relaxed)) return;
    }
}

bool native_release_count(atomic_uint *count)
{
    unsigned previous = atomic_fetch_sub_explicit(count, 1, memory_order_acq_rel);
    if (!previous) abort();
    return previous == 1;
}

void native_lock(native_device *device)
{
    if (doe_mutex_lock(&device->mutex).status != DOE_HOST_OK) abort();
}

void native_unlock(native_device *device)
{
    if (doe_mutex_unlock(&device->mutex).status != DOE_HOST_OK) abort();
}

WGPUStringView native_string(const char *text)
{
    return (WGPUStringView){text, text ? strlen(text) : 0};
}

bool native_string_size(WGPUStringView view, size_t *size)
{
    if (!view.data) {
        if (view.length && view.length != WGPU_STRLEN) return false;
        *size = 0;
        return true;
    }
    *size = view.length == WGPU_STRLEN ? strlen(view.data) : view.length;
    return true;
}

WGPUFuture native_callback(doe_instance *instance, WGPUCallbackMode mode,
    void *context, size_t size, size_t alignment,
    doe_callback_copy_context copy, doe_callback_deliver_context deliver)
{
    doe_instance_queue_lease lease = doe_instance_borrow_queue(instance);
    doe_callback_owner owner = {lease.queue, lease.userdata, lease.retain, lease.release};
    doe_callback_request request = {
        .abi_version = DOE_CALLBACKS_ABI_VERSION,
        .mode = (uint32_t)mode,
        .owner = &owner,
        .allocator = {NULL, NULL, native_allocate, native_deallocate},
        .source_context = context,
        .context_size = size,
        .context_alignment = alignment,
        .copy = copy,
        .deliver = deliver
    };
    WGPUFuture future = {0};
    doe_callback_status status = doe_callback_ready(&request, &future.id);
    if (status != DOE_CALLBACK_OK) {
        /* Admission failed before transfer. The API-specific caller still owns
         * context cleanup; zero is an invalid future, never synthetic success. */
        return (WGPUFuture){0};
    }
    return future;
}

void native_error(native_device *device, WGPUErrorType type, const char *message)
{
    if (!device) return;
    native_device_retain(device);
    native_lock(device);
    bool lost = native_backend_status(&device->backend) == NATIVE_DEVICE_LOST;
    native_unlock(device);
    if (lost) native_loss_complete(device->loss, WGPUDeviceLostReason_Unknown, "native device lost");
    doe_error_scope_result result = doe_error_scope_deliver(device->errors,
        (uint32_t)type, (const unsigned char *)message, strlen(message));
    if (result.status != DOE_ERROR_SCOPE_OK) abort();
    native_device_release(device);
}

void wgpuAdapterInfoFreeMembers(WGPUAdapterInfo info)
{
    /* native_backend_info owns every string in one block starting at vendor. */
    free((void *)info.vendor.data);
}

WGPUStatus native_limits(const native_backend *backend, WGPULimits *out)
{
    if (!out || out->nextInChain) return WGPUStatus_Error;
    *out = backend->limits;
    return WGPUStatus_Success;
}
