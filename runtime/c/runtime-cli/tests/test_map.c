/* SPDX-License-Identifier: MIT
 * Real-provider map reuse, replacement and write contents. Inspection happens
 * outside the measured command so read mappings keep the retained workload. */
#include "runtime_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct reply {
    bool done;
    WGPUMapAsyncStatus status;
} reply;

static void mapped(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    reply *result = userdata1;
    result->status = status;
    result->done = true;
}

static void inspect_map(doe_runtime *runtime, bool write, size_t size)
{
    reply result = {0};
    WGPUBufferMapCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = mapped, .userdata1 = &result};
    WGPUFuture future = wgpuBufferMapAsync(runtime->map_buffer, write ? WGPUMapMode_Write : WGPUMapMode_Read,
        0, size, callback);
    assert(runtime_wait_future(runtime, future) && result.done && result.status == WGPUMapAsyncStatus_Success);
}

static void execute(doe_runtime *runtime, uint64_t bytes, bool write)
{
    doe_command command = {.kind = DOE_COMMAND_MAP_ASYNC, .label = "map_async", .as.map = {.bytes = bytes, .write = write}};
    doe_trace_execution execution;
    bool ok = doe_runtime_execute(runtime, &command, &execution);
    if (!ok) fprintf(stderr, "%s: %s\n", execution.status_code, execution.message);
    assert(ok && execution.ok && execution.submit_count == 0 && execution.dispatch_count == 0);
    assert(wgpuBufferGetMapState(runtime->map_buffer) == WGPUBufferMapState_Unmapped);
}

int main(void)
{
    doe_runtime_options options = {.adapter = DOE_ADAPTER_SOFTWARE, .upload_submit_every = 1};
    char message[256];
    doe_runtime *runtime = doe_runtime_open(&options, message, sizeof(message));
    if (!runtime) fprintf(stderr, "%s\n", message);
    assert(runtime);
    execute(runtime, 64, true);
    inspect_map(runtime, true, 64);
    uint8_t *data = wgpuBufferGetMappedRange(runtime->map_buffer, 0, 64);
    assert(data);
    for (size_t i = 0; i < 64; i++) assert(data[i] == 0);
    memset(data, 0xa5, 64);
    wgpuBufferUnmap(runtime->map_buffer);
    WGPUBuffer original = runtime->map_buffer;
    execute(runtime, 16, true);
    assert(runtime->map_buffer == original && wgpuBufferGetSize(original) == 64);
    inspect_map(runtime, true, 64);
    data = wgpuBufferGetMappedRange(runtime->map_buffer, 0, 64);
    assert(data);
    for (size_t i = 0; i < 64; i++) assert(data[i] == (i < 16 ? 0 : 0xa5));
    wgpuBufferUnmap(runtime->map_buffer);
    execute(runtime, 128, true);
    assert(wgpuBufferGetSize(runtime->map_buffer) == 128);
    execute(runtime, 64, false);
    assert(wgpuBufferGetUsage(runtime->map_buffer) == (WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst));
    inspect_map(runtime, false, 64);
    const uint8_t *read = wgpuBufferGetConstMappedRange(runtime->map_buffer, 0, 64);
    assert(read);
    for (size_t i = 0; i < 64; i++) assert(read[i] == 0);
    wgpuBufferUnmap(runtime->map_buffer);
    original = runtime->map_buffer;
    execute(runtime, 4, false);
    assert(runtime->map_buffer == original);
    execute(runtime, 4, true);
    assert(wgpuBufferGetUsage(runtime->map_buffer) == (WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc));
    const doe_runtime_adapter *adapter = doe_runtime_adapter_info(runtime);
    printf("PASS map reuse, growth, mode replacement, partial zero fill and unmap: %s (%s, %s)\n",
        adapter->device, adapter->adapter_type, adapter->backend);
    doe_runtime_close(runtime);
    return 0;
}
