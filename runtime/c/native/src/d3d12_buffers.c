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
    native_failure failure = native_command_create(&device->backend, &command);
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

static native_failure upload(native_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, bool initialize)
{
    native_device *device = buffer->device;
    doe_dx_buffer *staging = NULL;
    doe_dx_error error = {0};
    /* Acquire a real caller lease before copy_now may unlock while waiting.
     * Shared initialization sources stay immutable; user writes get fresh staging. */
    doe_dx_status status = initialize ?
        doe_dx_zero_upload_acquire(device->backend.value.d3d12.context, size, &staging, &error) :
        doe_dx_buffer_create(device->backend.value.d3d12.context, size, DOE_DX_UPLOAD, &staging, &error);
    native_failure failure = native_failure_d3d12(status, &error);
    if (status == DOE_DX_OK && !initialize) {
        status = doe_dx_buffer_write(staging, 0, bytes, size, &error);
        failure = native_failure_d3d12(status, &error);
    }
    if (failure.status == NATIVE_OK)
        failure = copy_now(device, staging, 0, buffer->handle.d3d12->allocation, offset, size, initialize);
    doe_dx_buffer_release(staging);
    return failure;
}
#endif

native_failure native_d3d12_buffer_create(native_buffer *buffer, uint64_t size, uint32_t usage,
    bool direct_readback)
{
#if defined(_WIN32)
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
    doe_dx_status status = doe_dx_buffer_create(buffer->device->backend.value.d3d12.context,
        physical, direct_readback ? DOE_DX_READBACK : DOE_DX_DEFAULT, &payload->allocation, &error);
    if (status != DOE_DX_OK) {
        free(payload);
        return native_failure_d3d12(status, &error);
    }
    payload->allocation_size = physical;
    payload->memory = direct_readback ? DOE_DX_READBACK : DOE_DX_DEFAULT;
    buffer->handle.d3d12 = payload;
    /* Initialized upload heaps supply genuine zeros for the entire native
     * allocation, including private CBV padding. Public length stays unchanged. */
    native_failure failure = upload(buffer, 0, NULL, (size_t)physical, true);
    if (failure.status != NATIVE_OK) native_d3d12_buffer_close(buffer);
    return failure;
#else
    (void)buffer;
    (void)size;
    (void)usage;
    (void)direct_readback;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}

void native_d3d12_buffer_close(native_buffer *buffer)
{
    native_d3d12_buffer *payload = buffer->handle.d3d12;
    if (!payload) return;
#if defined(_WIN32)
    doe_dx_buffer_release(payload->allocation);
#endif
    free(payload);
    buffer->handle.d3d12 = NULL;
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
    return upload(buffer, offset, bytes, size, false);
#else
    (void)buffer;
    (void)offset;
    (void)bytes;
    (void)size;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
