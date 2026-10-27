/* SPDX-License-Identifier: MIT
 * Mock delayed callbacks: timeout cannot release borrowed userdata. */
#include "runtime_internal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static WGPUBufferMapCallbackInfo saved;
static bool deliver, callback_delivered, reject_map;
static unsigned releases, unmaps;
static const uint32_t values[] = {17, 29};
static buffer_entry entry = {.handle = 31, .buffer = (WGPUBuffer)(uintptr_t)1, .size = sizeof(values)};

bool runtime_fail(doe_trace_execution *execution, const char *code, const char *format, ...)
{
    execution->ok = false;
    execution->status_code = code;
    va_list args;
    va_start(args, format);
    vsnprintf(execution->message, sizeof(execution->message), format, args);
    va_end(args);
    return false;
}

bool runtime_wait_future(doe_runtime *runtime, WGPUFuture future)
{
    (void)runtime;
    assert(future.id == 7);
    if (!deliver) return false;
    assert(saved.callback && saved.userdata1 && !callback_delivered);
    saved.callback(reject_map ? WGPUMapAsyncStatus_Error : WGPUMapAsyncStatus_Success,
        (WGPUStringView){0}, saved.userdata1, saved.userdata2);
    callback_delivered = true;
    return true;
}

buffer_entry *runtime_find_buffer(doe_runtime *runtime, uint64_t handle)
{
    (void)runtime;
    return handle == entry.handle ? &entry : NULL;
}
void runtime_push_scopes(doe_runtime *runtime) { (void)runtime; }
bool runtime_pop_scopes(doe_runtime *runtime, doe_trace_execution *execution)
{ (void)runtime; (void)execution; return true; }
bool runtime_submit(doe_runtime *runtime, WGPUCommandBuffer commands, doe_trace_execution *execution)
{ (void)runtime; (void)commands; (void)execution; return true; }
bool runtime_wait_queue(doe_runtime *runtime, doe_trace_execution *execution)
{ (void)runtime; (void)execution; return true; }

WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice device, const WGPUBufferDescriptor *descriptor)
{ (void)device; assert(descriptor->size == sizeof(values)); return (WGPUBuffer)(uintptr_t)2; }
WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice device, const WGPUCommandEncoderDescriptor *descriptor)
{ (void)device; (void)descriptor; return (WGPUCommandEncoder)(uintptr_t)3; }
void wgpuCommandEncoderCopyBufferToBuffer(WGPUCommandEncoder encoder, WGPUBuffer source, uint64_t source_offset,
    WGPUBuffer destination, uint64_t destination_offset, uint64_t size)
{
    (void)encoder;
    assert(source == entry.buffer && source_offset == 0 && destination == (WGPUBuffer)(uintptr_t)2);
    assert(destination_offset == 0 && size == sizeof(values));
}
WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder encoder, const WGPUCommandBufferDescriptor *descriptor)
{ (void)encoder; (void)descriptor; return (WGPUCommandBuffer)(uintptr_t)4; }
void wgpuCommandEncoderRelease(WGPUCommandEncoder encoder) { (void)encoder; }
WGPUFuture wgpuBufferMapAsync(WGPUBuffer buffer, WGPUMapMode mode, size_t offset, size_t size,
    WGPUBufferMapCallbackInfo callback)
{
    assert(buffer == (WGPUBuffer)(uintptr_t)2 && mode == WGPUMapMode_Read && offset == 0 && size == sizeof(values));
    assert(callback.mode == WGPUCallbackMode_WaitAnyOnly);
    saved = callback;
    callback_delivered = false;
    return (WGPUFuture){.id = 7};
}
const void *wgpuBufferGetConstMappedRange(WGPUBuffer buffer, size_t offset, size_t size)
{
    assert(buffer == (WGPUBuffer)(uintptr_t)2 && offset == 0 && size == sizeof(values));
    assert(callback_delivered && !reject_map);
    return values;
}
void wgpuBufferUnmap(WGPUBuffer buffer) { assert(buffer == (WGPUBuffer)(uintptr_t)2); unmaps++; }
void wgpuBufferRelease(WGPUBuffer buffer) { assert(buffer == (WGPUBuffer)(uintptr_t)2); releases++; }

int main(void)
{
    doe_runtime runtime = {0};
    doe_trace_execution execution;
    uint8_t *bytes = NULL;
    deliver = true;
    assert(doe_runtime_read_buffer_range(&runtime, entry.handle, 0, sizeof(values), &bytes, &execution));
    assert(!memcmp(bytes, values, sizeof(values)) && runtime.readback == NULL && releases == 1);
    free(bytes);
    deliver = false;
    assert(!doe_runtime_read_buffer_range(&runtime, entry.handle, 0, sizeof(values), &bytes, &execution));
    assert(bytes == NULL && runtime.readback && releases == 1 && !callback_delivered);
    void *owner = runtime.readback;
    assert(!runtime_retire_readback(&runtime));
    assert(runtime.readback == owner && releases == 1 && !callback_delivered);
    assert(!doe_runtime_read_buffer_range(&runtime, entry.handle, 0, sizeof(values), &bytes, &execution));
    assert(!strcmp(execution.status_code, "readback_wait_unresolved"));
    /* Deliver the callback after both callers returned, with its live heap owner. */
    deliver = true;
    assert(runtime_retire_readback(&runtime));
    assert(callback_delivered && runtime.readback == NULL && releases == 2);
    reject_map = true;
    assert(!doe_runtime_read_buffer_range(&runtime, entry.handle, 0, sizeof(values), &bytes, &execution));
    assert(runtime.readback == NULL && releases == 3);
    assert(!strcmp(execution.status_code, "readback_failed"));
    assert(!doe_runtime_read_buffer_range(&runtime, entry.handle, 4, sizeof(values), &bytes, &execution));
    assert(!strcmp(execution.status_code, "readback_range") && releases == 3 && unmaps >= 3);
    puts("readback delayed-callback/retirement/blocked-reuse controls passed");
    return 0;
}
