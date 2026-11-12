/* SPDX-License-Identifier: MIT */
#include "d3d12_frontend.h"
#include <stdlib.h>

#if defined(_WIN32)
/* Every temporary native buffer is retained by the existing private command
 * before submission. The same pending owner keeps the device alive if a wait
 * cannot prove completion; releasing the call's staging lease never frees it. */
static native_failure copy_now(native_device *device, doe_dx_buffer *source, uint64_t from,
    doe_dx_buffer *destination, uint64_t to, uint64_t size, bool wait)
{
    native_command_handle command = {0};
    native_failure failure = native_d3d12_prepare_recording(device);
    if (failure.status != NATIVE_OK) return failure;
    failure = native_command_create(&device->backend, &command);
    if (failure.status != NATIVE_OK) return failure;
    doe_dx_error detail = {0};
    doe_dx_status status = doe_dx_commands_copy(command.value.d3d12.commands,
        source, from, destination, to, size, &detail);
    if (status != DOE_DX_OK) {
        failure = native_failure_d3d12(status, &detail);
        if (native_command_release(&command) != NATIVE_OK) abort();
        return failure;
    }
    return wait ? native_submit_backend_commands(device, command) :
        native_submit_backend_commands_async(device, command);
}

static void unlink_deferred(native_device *device, native_d3d12_buffer *payload)
{
    native_d3d12_buffer **cursor = &device->backend.value.d3d12.deferred_zero;
    while (*cursor != payload) cursor = &(*cursor)->next_deferred;
    *cursor = payload->next_deferred;
    payload->next_deferred = NULL;
    payload->zero_deferred = false;
}

static native_failure upload(native_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, bool initialize)
{
    native_device *device = buffer->device;
    native_d3d12_buffer *payload = buffer->handle.d3d12;
    /* A write covering the whole allocation, padding included, is itself the
     * complete initializing copy; the deferred zero fill would be overwritten.
     * Any partial write first materializes every deferred zero fill. */
    if (!initialize && payload->zero_deferred && !offset && size == payload->allocation_size)
        unlink_deferred(device, payload);
    doe_dx_buffer *staging = NULL;
    uint64_t source_offset=0;
    doe_dx_error error = {0};
    /* Both paths snapshot through retained staging. Initialization uses the
     * immutable source; accepted GPU ordering carries the full-zero proof. */
    doe_dx_upload_slice slice={0};
    doe_dx_status status;
    if (initialize) status=doe_dx_zero_upload_acquire(device->backend.value.d3d12.context,size,&staging,&error);
    else {
        status=doe_dx_upload_slice_write(device->backend.value.d3d12.context,bytes,size,4,&slice,&error);
        staging=slice.buffer; source_offset=slice.offset;
    }
    native_failure failure=native_failure_d3d12(status,&error);
    if (failure.status == NATIVE_OK && initialize)
        failure = copy_now(device, staging, 0, buffer->handle.d3d12->allocation, offset, size, false);
    else if (failure.status == NATIVE_OK) {
        /* A partial write's deferred zero fill executes before the open writes. */
        failure = native_d3d12_materialize_zero(device);
        if (failure.status == NATIVE_OK)
            failure = native_d3d12_write_record(device, staging, source_offset, payload->allocation, offset, size);
    }
    doe_dx_buffer_release(staging);
    return failure;
}
#endif

native_failure native_d3d12_materialize_zero(native_device *device)
{
#if defined(_WIN32)
    if (device->backend.kind != WGPUBackendType_D3D12 || !device->backend.value.d3d12.deferred_zero)
        return native_failure_status(NATIVE_OK);
    native_command_handle command = {0};
    native_failure failure = native_command_create(&device->backend, &command);
    if (failure.status != NATIVE_OK) return failure;
    /* Record every fill before unlinking any: a failure discards the whole
     * command and leaves every buffer deferred, never half-published. */
    for (native_d3d12_buffer *payload = device->backend.value.d3d12.deferred_zero;
         payload && failure.status == NATIVE_OK; payload = payload->next_deferred) {
        doe_dx_buffer *zero = NULL;
        doe_dx_error error = {0};
        doe_dx_status status = doe_dx_zero_upload_acquire(device->backend.value.d3d12.context,
            (size_t)payload->allocation_size, &zero, &error);
        if (status == DOE_DX_OK)
            status = doe_dx_commands_copy(command.value.d3d12.commands, zero, 0,
                payload->allocation, 0, payload->allocation_size, &error);
        failure = native_failure_d3d12(status, &error);
        doe_dx_buffer_release(zero);
    }
    if (failure.status != NATIVE_OK) {
        if (native_command_release(&command) != NATIVE_OK) abort();
        return failure;
    }
    while (device->backend.value.d3d12.deferred_zero)
        unlink_deferred(device, device->backend.value.d3d12.deferred_zero);
    /* The pending owner adopts it; accepted queue order carries the zero proof. */
    return native_submit_backend_commands_async(device, command);
#else
    (void)device;
    return native_failure_status(NATIVE_OK);
#endif
}

native_failure native_d3d12_buffer_create(native_buffer *buffer, uint64_t size, uint32_t usage,
    native_buffer_memory_intent intent)
{
#if defined(_WIN32)
    bool direct_readback=intent==NATIVE_BUFFER_MAP_READ;
    bool direct_upload=intent==NATIVE_BUFFER_MAP_WRITE;
    if (intent<NATIVE_BUFFER_GENERAL || intent>NATIVE_BUFFER_MAP_WRITE ||
        (direct_upload && usage!=DOE_VK_BUFFER_COPY_SRC)) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    if (direct_readback && usage != DOE_VK_BUFFER_COPY_DST)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    uint64_t alignment = usage & DOE_VK_BUFFER_UNIFORM ?
        buffer->device->backend.value.d3d12.caps.limits.uniform_offset_alignment : 4;
    uint64_t bound = native_allocation_bound(buffer->device);
    if (!alignment || (alignment & (alignment - 1)) || size > UINT64_MAX - (alignment - 1))
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    uint64_t physical = (size + alignment - 1) & ~(alignment - 1);
    if (!physical || physical > bound || physical > SIZE_MAX)
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    native_d3d12_buffer *payload = calloc(1, sizeof(*payload));
    if (!payload) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    doe_dx_error error = {0};
    bool known_zero = false;
    doe_dx_context *context = buffer->device->backend.value.d3d12.context;
    doe_dx_status status = direct_readback ?
        doe_dx_buffer_create_readback_zeroed(context,physical,&payload->allocation,&error) :
        direct_upload ? doe_dx_buffer_create(context,physical,DOE_DX_UPLOAD,&payload->allocation,&error) :
        doe_dx_buffer_create_default_known_zero(context,physical,&payload->allocation,&known_zero,&error);
    if (status != DOE_DX_OK) {
        free(payload);
        return native_failure_d3d12(status, &error);
    }
    payload->allocation_size = physical;
    payload->memory = direct_readback ? DOE_DX_READBACK : direct_upload ? DOE_DX_UPLOAD : DOE_DX_DEFAULT;
    buffer->handle.d3d12 = payload;
    /* READBACK construction completed its full zero (CPU, or D3D12 at creation).
     * DEFAULT memory above 1 MiB that D3D12 zeroed at creation (a new
     * committed resource) is already initialized: a fill would copy zeros
     * over zeros (646 MB for Gemma 1B weights, before their own whole writes).
     * Other DEFAULT defers its complete zero copy (native width, CBV padding
     * included) until the device's next recording, transfer or CPU read, unless
     * a whole-allocation write initializes it first. Known-fenced acceptance of
     * either copy protects later GPU uses, never CPU access. */
    if (!direct_readback && !direct_upload && !known_zero) {
        payload->zero_deferred = true;
        payload->next_deferred = buffer->device->backend.value.d3d12.deferred_zero;
        buffer->device->backend.value.d3d12.deferred_zero = payload;
    }
    return native_failure_status(NATIVE_OK);
#else
    (void)buffer;
    (void)size;
    (void)usage;
    (void)intent;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

void native_d3d12_buffer_close(native_buffer *buffer)
{
    native_d3d12_buffer *payload = buffer->handle.d3d12;
    if (!payload) return;
#if defined(_WIN32)
    /* Never observed: nothing can read the bytes it no longer needs zeroed. */
    if (payload->zero_deferred) unlink_deferred(buffer->device, payload);
#endif
#if defined(_WIN32)
    doe_dx_buffer_release(payload->allocation);
#endif
    free(payload);
    buffer->handle.d3d12 = NULL;
}

native_failure native_d3d12_buffer_evict(native_buffer *buffer)
{
#if defined(_WIN32)
    native_d3d12_buffer *payload = buffer->handle.d3d12;
    if (!payload) return native_failure_status(NATIVE_OK);
    if (payload->zero_deferred) unlink_deferred(buffer->device, payload);
    /* Publish open copies to their existing pending owner before rejecting new
     * recordings. Even a failed signal keeps the command's allocation leases. */
    native_failure failure = native_d3d12_flush_writes(buffer->device);
    native_progress_pending(buffer->device);
    doe_dx_status evicted = doe_dx_buffer_evict(payload->allocation);
    return failure.status != NATIVE_OK ? failure : native_failure_d3d12(evicted, NULL);
#else
    (void)buffer;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

native_failure native_d3d12_buffer_read(native_buffer *buffer, uint64_t offset, void *bytes, size_t size)
{
#if defined(_WIN32)
    if (!size) return native_failure_status(NATIVE_OK);
    if (!bytes || !buffer->handle.d3d12) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    native_d3d12_buffer *payload = buffer->handle.d3d12;
    if (payload->memory == DOE_DX_READBACK) {
        /* The resource/map owner has already established completion. The
         * backend still rejects unknown ownership or uninitialized bytes; a
         * READBACK allocation needs no second GPU copy before its CPU map. */
        doe_dx_error error = {0};
        doe_dx_status status = doe_dx_buffer_read(payload->allocation, offset, bytes, size, &error);
        return native_failure_d3d12(status, &error);
    }
    doe_dx_buffer *staging = NULL;
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_buffer_create(buffer->device->backend.value.d3d12.context,
        size, DOE_DX_READBACK, &staging, &error);
    native_failure failure = native_failure_d3d12(status, &error);
    if (failure.status == NATIVE_OK)
        failure = copy_now(buffer->device, buffer->handle.d3d12->allocation, offset, staging, 0, size, true);
    if (failure.status == NATIVE_OK) {
        status = doe_dx_buffer_read(staging, 0, bytes, size, &error);
        failure = native_failure_d3d12(status, &error);
    }
    doe_dx_buffer_release(staging);
    return failure;
#else
    (void)buffer;
    (void)offset;
    (void)bytes;
    (void)size;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

native_failure native_d3d12_buffer_write(native_buffer *buffer, uint64_t offset, const void *bytes, size_t size)
{
#if defined(_WIN32)
    if (!size) return native_failure_status(NATIVE_OK);
    if (!bytes || !buffer->handle.d3d12) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    if (buffer->handle.d3d12->memory==DOE_DX_UPLOAD) {
        doe_dx_error error={0};
        doe_dx_status status=doe_dx_buffer_write(buffer->handle.d3d12->allocation,offset,bytes,size,&error);
        return native_failure_d3d12(status,&error);
    }
    return upload(buffer, offset, bytes, size, false);
#else
    (void)buffer;
    (void)offset;
    (void)bytes;
    (void)size;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

native_failure native_d3d12_buffer_lend(native_buffer *buffer, uint64_t offset, size_t size,
    const unsigned char **out)
{
#if defined(_WIN32)
    native_d3d12_buffer *payload = buffer->handle.d3d12;
    if (!payload || payload->memory != DOE_DX_READBACK || !out) return native_failure_status(NATIVE_UNSUPPORTED);
    const void *bytes = NULL;
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_buffer_lend_read(payload->allocation, offset, size, &bytes, &error);
    if (status == DOE_DX_OK) *out = bytes;
    return native_failure_d3d12(status, &error);
#else
    (void)buffer; (void)offset; (void)size; (void)out;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

void native_d3d12_buffer_return(native_buffer *buffer)
{
#if defined(_WIN32)
    if (buffer->handle.d3d12) doe_dx_buffer_return_read(buffer->handle.d3d12->allocation);
#else
    (void)buffer;
#endif
}

bool native_d3d12_buffer_write_lendable(const native_buffer *buffer)
{
    return buffer->handle.d3d12 && buffer->handle.d3d12->memory==DOE_DX_UPLOAD;
}
native_failure native_d3d12_buffer_lend_write(native_buffer *buffer,uint64_t offset,size_t size,unsigned char **out)
{
#if defined(_WIN32)
    if (!native_d3d12_buffer_write_lendable(buffer) || !out) return native_failure_status(NATIVE_UNSUPPORTED);
    void *bytes=NULL; doe_dx_error error={0};
    doe_dx_status status=doe_dx_buffer_lend_write(buffer->handle.d3d12->allocation,offset,size,&bytes,&error);
    if (status==DOE_DX_OK) *out=bytes;
    return native_failure_d3d12(status,&error);
#else
    (void)buffer; (void)offset; (void)size; (void)out;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
