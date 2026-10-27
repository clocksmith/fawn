/* SPDX-License-Identifier: MIT
 * Observe actual completion callback mode and count at the public ABI, then
 * verify bytes after a deferred flush. No completion is synthesized. */
#include "runtime_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned waits;
static WGPUCallbackMode last_mode;
static bool reject_future, delay_queue;
static WGPUFuture queue_future;
void __real_wgpuInstanceProcessEvents(WGPUInstance);
WGPUWaitStatus __real_wgpuInstanceWaitAny(WGPUInstance, size_t, WGPUFutureWaitInfo *, uint64_t);
WGPUFuture __real_wgpuQueueOnSubmittedWorkDone(WGPUQueue, WGPUQueueWorkDoneCallbackInfo);
WGPUFuture __wrap_wgpuQueueOnSubmittedWorkDone(WGPUQueue queue, WGPUQueueWorkDoneCallbackInfo callback)
{
    waits++;
    last_mode = callback.mode;
    if (reject_future) return (WGPUFuture){0};
    queue_future = __real_wgpuQueueOnSubmittedWorkDone(queue, callback);
    return queue_future;
}

void __wrap_wgpuInstanceProcessEvents(WGPUInstance instance)
{
    if (!delay_queue) __real_wgpuInstanceProcessEvents(instance);
}

WGPUWaitStatus __wrap_wgpuInstanceWaitAny(WGPUInstance instance, size_t count, WGPUFutureWaitInfo *infos, uint64_t timeout)
{
    if (delay_queue && count == 1 && infos[0].future.id == queue_future.id) return WGPUWaitStatus_TimedOut;
    return __real_wgpuInstanceWaitAny(instance, count, infos, timeout);
}

int main(void)
{
    for (unsigned strategy = 0; strategy < 4; strategy++) {
        waits = 0;
        reject_future = false;
        doe_runtime_options options = {.adapter = DOE_ADAPTER_SOFTWARE, .upload_submit_every = 1,
            .defer_queue_sync = (strategy & 1) != 0, .queue_wait = strategy & 2 ? DOE_WAIT_ANY : DOE_WAIT_PROCESS_EVENTS};
        char message[256];
        doe_runtime *runtime = doe_runtime_open(&options, message, sizeof(message));
        if (!runtime) fprintf(stderr, "%s\n", message);
        assert(runtime);
        assert(!doe_runtime_gpu_completion_unknown(runtime));
        uint32_t word = 0x12345678;
        doe_command write = {.kind = DOE_COMMAND_BUFFER_WRITE, .label = "buffer_write",
            .as.buffer_write = {.handle = 10, .buffer_size = 4, .words = &word, .word_count = 1}};
        doe_trace_execution execution;
        assert(doe_runtime_execute(runtime, &write, &execution));
        word = 0xa1b2c3d4;
        assert(doe_runtime_execute(runtime, &write, &execution));
        assert(waits == (options.defer_queue_sync ? 0u : 2u));
        assert(doe_runtime_flush(runtime, &execution));
        assert(waits == (options.defer_queue_sync ? 1u : 2u));
        assert(last_mode == (options.queue_wait == DOE_WAIT_ANY ? WGPUCallbackMode_WaitAnyOnly : WGPUCallbackMode_AllowProcessEvents));
        uint8_t *bytes = NULL;
        size_t size = 0;
        assert(doe_runtime_read_buffer(runtime, 10, &bytes, &size, &execution));
        assert(size == 4 && !memcmp(bytes, &word, 4));
        free(bytes);
        /* A missing completion future must fail and cannot reuse callback state. */
        reject_future = (strategy & 1) != 0;
        delay_queue = !reject_future;
        runtime->options.queue_wait_timeout_ns = 1;
        execution = (doe_trace_execution){.ok = true};
        assert(!runtime_wait_queue(runtime, &execution));
        assert(doe_runtime_gpu_completion_unknown(runtime));
        delay_queue = false;
        if (!reject_future) {
            /* Deliver the real callback after the wait returned: its payload
             * must still be alive, and failure must remain latched. */
            if (options.queue_wait == DOE_WAIT_ANY) {
                WGPUFutureWaitInfo info = {.future = queue_future};
                assert(wgpuInstanceWaitAny(runtime->instance, 1, &info, 0) == WGPUWaitStatus_Success);
            } else wgpuInstanceProcessEvents(runtime->instance);
            assert(runtime->queue_done && runtime->queue_wait_failed);
        }
        unsigned failed_waits = waits;
        assert(doe_runtime_gpu_completion_unknown(runtime) == reject_future);
        assert(!strcmp(execution.status_code, "queue_wait_unresolved"));
        execution = (doe_trace_execution){.ok = true};
        assert(!runtime_wait_queue(runtime, &execution) && waits == failed_waits);
        execution = (doe_trace_execution){.ok = true};
        assert(!doe_runtime_execute(runtime, &write, &execution));
        assert(!strcmp(execution.status_code, "runtime_unresolved"));
        assert(waits == failed_waits);
        const doe_runtime_adapter *adapter = doe_runtime_adapter_info(runtime);
        printf("PASS queue strategy %u: actual callback modes/counts, final bytes, failed-future/late-callback guard: %s (%s)\n",
            strategy, adapter->device, adapter->adapter_type);
        doe_runtime_close(runtime);
    }
    return 0;
}
