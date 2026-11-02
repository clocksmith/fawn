/* SPDX-License-Identifier: MIT
 * Real public future admission, deferred read access and stale map generations.
 * Exact byte readback and generation checks do not assume that real GPU work
 * is still pending when callback registration begins. */
#include "consumer.h"

typedef struct map_expectation {
    WGPUMapAsyncStatus expected;
    unsigned called;
} map_expectation;

static void map_reply(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    map_expectation *expected = userdata;
    if (status != expected->expected)
        fprintf(stderr,"map callback mismatch: actual=%u expected=%u priorCalls=%u\n",
            (unsigned)status,(unsigned)expected->expected,expected->called);
    assert(status == expected->expected);
    expected->called++;
}

static void read_and_queue_obligations(state *s)
{
    const uint32_t expected[] = {31, 41, 59, 26};
    WGPUBuffer source = buffer(s, sizeof(expected), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer result = buffer(s, sizeof(expected), WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    wgpuQueueWriteBuffer(s->queue, source, 0, expected, sizeof(expected));
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, result, 0, sizeof(expected));
    submit(s, encoder);
    map_expectation observed = {.expected=WGPUMapAsyncStatus_Success};
    WGPUBufferMapCallbackInfo map_info = {.mode=WGPUCallbackMode_WaitAnyOnly,
        .callback=map_reply,.userdata1=&observed};
    unsigned done = 0;
    WGPUQueueWorkDoneCallbackInfo done_info = {.mode=WGPUCallbackMode_WaitAnyOnly,
        .callback=work_done_reply,.userdata1=&done};
    WGPUFuture map_future = wgpuBufferMapAsync(result, WGPUMapMode_Read, 0, sizeof(expected), map_info);
    WGPUFuture done_future = wgpuQueueOnSubmittedWorkDone(s->queue, done_info);
    /* These real submissions may already be terminal. Readiness may publish
     * during admission; WaitAnyOnly still defers the foreign callback. The
     * controlled Metal ledger separately proves actual pending/unknown work. */
    assert(map_future.id && done_future.id);
    assert(!observed.called && !done && wgpuBufferGetMapState(result) == WGPUBufferMapState_Pending);
    wait_future(s, map_future);
    wait_future(s, done_future);
    assert(observed.called == 1 && done == 1);
    assert(!memcmp(wgpuBufferGetConstMappedRange(result, 0, sizeof(expected)), expected, sizeof(expected)));
    wgpuBufferUnmap(result);
    wgpuBufferRelease(result);
    wgpuBufferRelease(source);
}

static void cancelled_generation_does_not_replace_new_mapping(state *s)
{
    WGPUBuffer writable = buffer(s, 16, WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc);
    map_expectation cancelled = {.expected=WGPUMapAsyncStatus_Aborted};
    map_expectation current = {.expected=WGPUMapAsyncStatus_Success};
    WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_WaitAnyOnly,
        .callback=map_reply,.userdata1=&cancelled};
    WGPUFuture old = wgpuBufferMapAsync(writable, WGPUMapMode_Write, 0, 16, callback);
    /* WaitAnyOnly defers delivery even if a direct UPLOAD lend is already
     * ready. Do not deliver the old generation before canceling it. */
    assert(old.id && !cancelled.called && wgpuBufferGetMapState(writable) == WGPUBufferMapState_Pending);
    wgpuBufferUnmap(writable);
    assert(!cancelled.called && wgpuBufferGetMapState(writable) == WGPUBufferMapState_Unmapped);
    callback.userdata1 = &current;
    WGPUFuture fresh = wgpuBufferMapAsync(writable, WGPUMapMode_Write, 0, 16, callback);
    assert(fresh.id && !current.called && !cancelled.called &&
        wgpuBufferGetMapState(writable) == WGPUBufferMapState_Pending);
    wait_future(s, fresh);
    assert(current.called == 1);
    uint32_t *bytes = wgpuBufferGetMappedRange(writable, 0, 16);
    assert(bytes);
    bytes[0] = 0x12345678;
    /* Deliver the obsolete generation after the fresh mapping is published. */
    wait_future(s, old);
    assert(cancelled.called == 1 && wgpuBufferGetMapState(writable) == WGPUBufferMapState_Mapped);
    bytes = wgpuBufferGetMappedRange(writable, 0, 16);
    assert(bytes && bytes[0] == 0x12345678);
    wgpuBufferUnmap(writable);
    wgpuBufferRelease(writable);
}

int main(void)
{
    consumer_line_output();
    state s = {0};
    consumer_init(&s);
    read_and_queue_obligations(&s);
    cancelled_generation_does_not_replace_new_mapping(&s);
    consumer_finish(&s);
    printf("PASS asynchronous future admission, exact readback and stale generation cancellation %s\n",
        adapter_class_scope());
    return 0;
}
