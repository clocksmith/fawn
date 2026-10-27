/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* This poisons recording, not GPU completion. The unchanged native failure and
 * pending owner retain uncertainty and the actual diagnostic independently. */
static doe_resource_status recording_failure(native_result result)
{
    switch (result) {
    case NATIVE_OK: return DOE_RESOURCE_OK;
    case NATIVE_INVALID_ARGUMENT: return DOE_RESOURCE_INVALID_ARGUMENT;
    case NATIVE_OUT_OF_MEMORY: return DOE_RESOURCE_OUT_OF_MEMORY;
    case NATIVE_UNSUPPORTED: return DOE_RESOURCE_UNSUPPORTED;
    case NATIVE_UNAVAILABLE: case NATIVE_DEVICE_LOST:
    case NATIVE_SURFACE_OUTDATED: case NATIVE_SURFACE_LOST: return DOE_RESOURCE_UNAVAILABLE;
    case NATIVE_BUSY: case NATIVE_INVALID_STATE: return DOE_RESOURCE_STATE;
    case NATIVE_PENDING: case NATIVE_UNKNOWN: case NATIVE_TIMEOUT:
    case NATIVE_FAILURE: return DOE_RESOURCE_EXECUTION_FAILED;
    }
    return DOE_RESOURCE_EXECUTION_FAILED;
}

void wgpuCommandEncoderWriteBuffer(WGPUCommandEncoder handle, WGPUBuffer destination,
    uint64_t offset, const void *data, size_t size)
{
    doe_resource_encoder *encoder = (doe_resource_encoder *)handle;
    native_device *device = doe_resource_encoder_device(encoder);
    if (!device) return;
    doe_resource_encoder_retain(encoder);
    doe_resource_buffer *buffer = (doe_resource_buffer *)destination;
    doe_resource_buffer_retain(buffer);
    native_lock(device);
    bool ended = doe_resource_encoder_ended(encoder);
    doe_resource_status status = doe_resource_encoder_write_buffer_validate(encoder, buffer, offset, size);
    if (status == DOE_RESOURCE_OK && !data && size)
        status = doe_resource_encoder_invalidate(encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    native_unlock(device);
    WGPUBuffer staging = NULL;
    native_failure failure = native_failure_status(NATIVE_OK);
    if (status == DOE_RESOURCE_OK && size) {
        WGPUBufferDescriptor descriptor = {.size=size,
            .usage=WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst};
        native_buffer_creation created = native_device_create_buffer_result(device, &descriptor,
            native_allocation_bound(device));
        staging = created.buffer;
        failure = created.failure;
        status = created.status;
        native_lock(device);
        if (staging) {
            /* Each backend's ordinary write snapshots bytes before returning.
             * Any private upload uses the existing pending/native leases. */
            native_buffer *payload = doe_resource_buffer_native((doe_resource_buffer *)staging);
            failure = native_buffer_write(payload, 0, data, size);
        }
        if (failure.status != NATIVE_OK) status = recording_failure(failure.status);
        if (status == DOE_RESOURCE_OK && staging) {
            doe_resource_buffer_copy copy = {(doe_resource_buffer *)staging, buffer, 0, offset, size};
            status = doe_resource_encoder_copy_buffer(encoder, &copy, DOE_RESOURCE_DISTINCT_BUFFERS);
        } else if (status == DOE_RESOURCE_OK) status = DOE_RESOURCE_OUT_OF_MEMORY;
        ended = doe_resource_encoder_ended(encoder);
        if (status != DOE_RESOURCE_OK) status = doe_resource_encoder_invalidate(encoder, status);
        native_unlock(device);
    }
    if (failure.status != NATIVE_OK) {
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure, "encoder write snapshot failed", message);
        native_error(device, native_result_error(failure.status), message);
    } else native_encoding_error(device, ended, status, "encoder write recording failed");
    /* No lock is held while a release may finalize or join another owner. The
     * recorded copy, or backend's unknown upload, keeps its own storage leases. */
    wgpuBufferRelease(staging);
    doe_resource_buffer_release(buffer);
    doe_resource_encoder_release(encoder);
}
