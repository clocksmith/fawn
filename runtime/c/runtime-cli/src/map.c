/* SPDX-License-Identifier: MIT
 * map_async with the semantics of doe-zig-runtime's WebGPU path
 * (wgpu_commands_copy.zig executeMapAsync): one runtime-owned buffer, MapRead |
 * CopyDst for read and MapWrite | CopySrc for write, is reused while it is
 * large enough and of the mode's usage, otherwise replaced. Each command maps
 * bytes [0, bytes), waits for the map, zero-fills the range in write mode and
 * unmaps. Read mode does not touch the mapped bytes, matching the retained
 * workload. Content checks belong in the tests. The native Vulkan lane reports
 * these commands unsupported. */
#include "runtime_internal.h"
#include <stdlib.h>
#include <string.h>

static void map_done(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    doe_runtime *runtime = userdata1;
    runtime->map_done = true;
    runtime->map_status = status;
}

static WGPUBuffer map_buffer(doe_runtime *runtime, uint64_t bytes, bool write, doe_trace_execution *execution)
{
    if (runtime->map_buffer && runtime->map_size >= bytes && runtime->map_write == write) return runtime->map_buffer;
    if (runtime->map_buffer) {
        wgpuBufferDestroy(runtime->map_buffer);
        wgpuBufferRelease(runtime->map_buffer);
        runtime->map_buffer = NULL;
    }
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = bytes;
    descriptor.usage = write ? WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc : WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    runtime->map_buffer = wgpuDeviceCreateBuffer(runtime->device, &descriptor);
    if (!runtime->map_buffer) {
        runtime_fail(execution, "map_async_buffer_failed", "the %llu-byte map buffer was not created", (unsigned long long)bytes);
        return NULL;
    }
    runtime->map_size = bytes;
    runtime->map_write = write;
    return runtime->map_buffer;
}

bool runtime_execute_map(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    uint64_t bytes = command->as.map.bytes;
    bool write = command->as.map.write;
    if (bytes > SIZE_MAX) return runtime_fail(execution, "map_async_buffer_failed", "the map exceeds host addressing");
    uint64_t start = doe_runtime_monotonic_ns();
    runtime_push_scopes(runtime);
    WGPUBuffer buffer = map_buffer(runtime, bytes, write, execution);
    bool ok = buffer != NULL;
    runtime->map_done = false;
    runtime->map_status = 0;
    WGPUFuture future = {0};
    if (ok) {
        WGPUBufferMapCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = map_done, .userdata1 = runtime};
        future = wgpuBufferMapAsync(buffer, write ? WGPUMapMode_Write : WGPUMapMode_Read, 0, (size_t)bytes, callback);
    }
    uint64_t setup_end = doe_runtime_monotonic_ns();
    if (ok && (!runtime_wait_future(runtime, future) || !runtime->map_done || runtime->map_status != WGPUMapAsyncStatus_Success))
        ok = runtime_fail(execution, "map_async_failed", "the %s map of %llu bytes did not resolve", write ? "write" : "read",
            (unsigned long long)bytes);
    uint64_t wait_end = doe_runtime_monotonic_ns();
    if (ok && write) {
        void *range = wgpuBufferGetMappedRange(buffer, 0, (size_t)bytes);
        if (range) memset(range, 0, (size_t)bytes);
        else ok = runtime_fail(execution, "map_async_range_missing", "the write map has no mapped range");
    }
    /* Also cancels a still-pending map after timeout. The callback payload is
     * runtime-owned until instance release; timeout cannot retire it. */
    if (buffer) wgpuBufferUnmap(buffer);
    if (!runtime_pop_scopes(runtime, execution)) ok = false;
    execution->setup_ns = setup_end - start;
    execution->submit_wait_ns = wait_end - setup_end;
    return ok;
}
