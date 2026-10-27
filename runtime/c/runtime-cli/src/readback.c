/* SPDX-License-Identifier: MIT
 * One synchronous capture owns one map callback record. WaitAnyOnly delivers
 * on the waiting thread. An unresolved future retains its buffer and userdata
 * until close explicitly retires that exact future; it never borrows a stack. */
#include "runtime_internal.h"
#include <stdlib.h>
#include <string.h>

struct readback_request {
    WGPUBuffer buffer;
    WGPUFuture future;
    bool done;
    WGPUMapAsyncStatus status;
};

static void mapped(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    struct readback_request *request = userdata1;
    request->status = status;
    request->done = true;
}

bool runtime_retire_readback(doe_runtime *runtime)
{
    struct readback_request *request = runtime->readback;
    if (!request) return true;
    if (!request->done) {
        wgpuBufferUnmap(request->buffer);
        if (!request->future.id || !runtime_wait_future(runtime, request->future) || !request->done)
            return false;
    }
    wgpuBufferUnmap(request->buffer);
    wgpuBufferRelease(request->buffer);
    runtime->readback = NULL;
    free(request);
    return true;
}

bool runtime_read_back(doe_runtime *runtime, WGPUBuffer source, uint64_t offset, uint64_t size, uint8_t *out,
    doe_trace_execution *execution)
{
    if (runtime->readback)
        return runtime_fail(execution, "readback_wait_unresolved", "a previous map future still owns its callback storage");
    if (!source || !out || !size || size > SIZE_MAX || size % 4 || offset % 4)
        return runtime_fail(execution, "readback_range", "capture needs a nonempty aligned addressable byte range");
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = size;
    descriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(runtime->device, &descriptor);
    if (!buffer) return runtime_fail(execution, "readback_failed", "readback buffer was not created");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(runtime->device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, offset, buffer, 0, size);
    WGPUCommandBuffer copy = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    bool ok = runtime_submit(runtime, copy, execution);
    if (ok && runtime->options.defer_queue_sync) ok = runtime_wait_queue(runtime, execution);
    if (!ok) {
        wgpuBufferRelease(buffer);
        return false;
    }
    struct readback_request *request = calloc(1, sizeof(*request));
    if (!request) {
        wgpuBufferRelease(buffer);
        return runtime_fail(execution, "out_of_memory", "readback callback allocation failed");
    }
    request->buffer = buffer;
    runtime->readback = request;
    WGPUBufferMapCallbackInfo callback = {
        .mode = WGPUCallbackMode_WaitAnyOnly, .callback = mapped, .userdata1 = request};
    request->future = wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, size, callback);
    if (!runtime_wait_future(runtime, request->future) || !request->done)
        return runtime_fail(execution, "readback_wait_unresolved", "map future did not retire within the wait bound");
    if (request->status != WGPUMapAsyncStatus_Success) {
        ok = runtime_fail(execution, "readback_failed", "readback map completed with status %d", (int)request->status);
    } else {
        const void *bytes = wgpuBufferGetConstMappedRange(buffer, 0, size);
        if (bytes) memcpy(out, bytes, (size_t)size);
        else ok = runtime_fail(execution, "readback_failed", "readback has no mapped range");
    }
    if (!runtime_retire_readback(runtime))
        return runtime_fail(execution, "readback_wait_unresolved", "completed map could not be retired");
    return ok;
}

bool doe_runtime_read_buffer_range(doe_runtime *runtime, uint64_t handle, uint64_t offset, uint64_t size,
    uint8_t **data, doe_trace_execution *execution)
{
    *execution = (doe_trace_execution){.ok = true};
    *data = NULL;
    buffer_entry *entry = runtime_find_buffer(runtime, handle);
    if (!entry) return runtime_fail(execution, "readback_missing", "buffer %llu does not exist", (unsigned long long)handle);
    if (!size || size > SIZE_MAX || offset > entry->size || size > entry->size - offset || offset % 4 || size % 4)
        return runtime_fail(execution, "readback_range", "capture is not an aligned nonempty range of the selected buffer");
    uint8_t *bytes = malloc((size_t)size);
    if (!bytes) return runtime_fail(execution, "out_of_memory", "readback allocation failed");
    runtime_push_scopes(runtime);
    bool ok = runtime_read_back(runtime, entry->buffer, offset, size, bytes, execution);
    if (!runtime_pop_scopes(runtime, execution)) ok = false;
    if (!ok) {
        free(bytes);
        return false;
    }
    *data = bytes;
    return true;
}

bool doe_runtime_read_buffer(doe_runtime *runtime, uint64_t handle, uint8_t **data, size_t *size,
    doe_trace_execution *execution)
{
    *execution = (doe_trace_execution){.ok = true};
    *data = NULL;
    *size = 0;
    buffer_entry *entry = runtime_find_buffer(runtime, handle);
    if (!entry) return runtime_fail(execution, "readback_missing", "buffer %llu does not exist", (unsigned long long)handle);
    if (!doe_runtime_read_buffer_range(runtime, handle, 0, entry->size, data, execution)) return false;
    *size = (size_t)entry->size;
    return true;
}
