/* SPDX-License-Identifier: MIT
 * Public WebGPU completion strategies and final deferred drain. Callback storage
 * remains runtime-owned through instance destruction, including a timed-out wait. */
#include "runtime_internal.h"
#include "host_io.h"

static void work_done(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    doe_runtime *runtime = userdata1;
    runtime->queue_done = true;
    runtime->queue_status = status;
}

bool runtime_wait_queue(doe_runtime *runtime, doe_trace_execution *execution)
{
    if (runtime->queue_wait_failed)
        return runtime_fail(execution, "queue_wait_unresolved", "a previous queue wait failed; completion state cannot be reused");
    runtime->queue_done = false;
    WGPUQueueWorkDoneCallbackInfo callback = {.mode = runtime->options.queue_wait == DOE_WAIT_PROCESS_EVENTS ?
        WGPUCallbackMode_AllowProcessEvents : WGPUCallbackMode_WaitAnyOnly, .callback = work_done, .userdata1 = runtime};
    WGPUFuture future = wgpuQueueOnSubmittedWorkDone(runtime->queue, callback);
    uint64_t start = doe_runtime_monotonic_ns();
    uint64_t bound = runtime->options.queue_wait_timeout_ns ? runtime->options.queue_wait_timeout_ns : WAIT_TIMEOUT_NS;
    bool valid = future.id != 0;
    while (valid && !runtime->queue_done) {
        if (runtime->options.queue_wait == DOE_WAIT_PROCESS_EVENTS) {
            wgpuInstanceProcessEvents(runtime->instance);
        } else {
            /* Zero-timeout WaitAny polling does not depend on optional timed
             * wait support; the same explicit bound covers the whole wait. */
            WGPUFutureWaitInfo wait = {.future = future};
            WGPUWaitStatus status = wgpuInstanceWaitAny(runtime->instance, 1, &wait, 0);
            if (status != WGPUWaitStatus_Success && status != WGPUWaitStatus_TimedOut) valid = false;
        }
        if (runtime->queue_done) break;
        if (doe_runtime_monotonic_ns() - start >= bound) { valid = false; break; }
        doe_host_pause_ns(20000);
    }
    if (!valid || !runtime->queue_done) {
        runtime->queue_wait_failed = true;
        return runtime_fail(execution, "queue_wait_unresolved", "queue work did not complete within the wait bound");
    }
    if (runtime->queue_status != WGPUQueueWorkDoneStatus_Success) {
        runtime->queue_wait_failed = true;
        return runtime_fail(execution, "queue_work_failed", "queue work completed with status %d", (int)runtime->queue_status);
    }
    runtime->queue_pending = false;
    runtime->uploads_since_wait = 0;
    return true;
}

bool runtime_after_submit(doe_runtime *runtime, doe_trace_execution *execution)
{
    if (runtime->queue_wait_failed)
        return runtime_fail(execution, "queue_wait_unresolved", "a previous queue wait failed");
    runtime->queue_pending = true;
    return runtime->options.defer_queue_sync || runtime_wait_queue(runtime, execution);
}

bool doe_runtime_flush(doe_runtime *runtime, doe_trace_execution *execution)
{
    *execution = (doe_trace_execution){.ok = true};
    if (runtime->readback)
        return runtime_fail(execution, "readback_wait_unresolved", "a capture map future remains unresolved");
    if (!runtime->queue_pending && !runtime->uploads_since_wait) return true;
    uint64_t start = doe_runtime_monotonic_ns();
    bool ok = runtime_wait_queue(runtime, execution);
    execution->duration_ns = doe_runtime_monotonic_ns() - start;
    execution->submit_wait_ns = execution->duration_ns;
    return ok;
}

bool doe_runtime_gpu_completion_unknown(const doe_runtime *runtime)
{
    return runtime && ((runtime->queue_wait_failed && (!runtime->queue_done || runtime->queue_status != WGPUQueueWorkDoneStatus_Success)) || runtime->readback != NULL);
}
