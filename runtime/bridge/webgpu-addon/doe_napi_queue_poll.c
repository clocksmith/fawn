/*
 * doe_napi_queue_poll.c — Non-blocking queue work-done for onSubmittedWorkDone.
 *
 * queueWorkDoneBegin registers one work-done future (ALLOW_PROCESS_EVENTS,
 * so its callback runs only inside wgpuInstanceProcessEvents on this thread)
 * and returns a handle; queueWorkDonePoll processes events once and reports
 * completion. JavaScript yields to the event loop between polls, so other
 * work (file reads, parsing, other promises) proceeds while the GPU runs,
 * as Dawn's Node binding does, instead of blocking the thread in queueFlush.
 * A handle collected before completion is freed by its callback.
 */
#include "doe_napi_internal.h"
#include "doe_napi_timeout.h"

typedef struct {
    QueueWorkDoneResult result;
    WGPUInstance instance;
    uint64_t started_ns;
    bool abandoned; /* JS released the handle before the callback ran. */
} PendingWorkDone;

static void pending_work_done_callback(uint32_t status, WGPUStringView message, void* userdata1, void* userdata2) {
    PendingWorkDone* pending = (PendingWorkDone*)userdata1;
    queue_work_done_callback(status, message, &pending->result, userdata2);
    if (pending->abandoned) free(pending);
}

static void pending_work_done_finalize(napi_env env, void* data, void* hint) {
    (void)env;
    (void)hint;
    PendingWorkDone* pending = (PendingWorkDone*)data;
    if (pending->result.done) free(pending);
    else pending->abandoned = true;
}

napi_value doe_queue_work_done_begin(napi_env env, napi_callback_info info) {
    NAPI_ASSERT_ARGC(env, info, 2);
    CHECK_LIB_LOADED(env);
    WGPUInstance inst = unwrap_ptr(env, _args[0]);
    WGPUQueue queue = unwrap_ptr(env, _args[1]);
    if (!queue) NAPI_THROW(env, "queueWorkDoneBegin requires queue");
    if (!inst || !pfn_wgpuInstanceProcessEvents) {
        napi_throw_error(env, "DOE_QUEUE_UNAVAILABLE", "queueWorkDoneBegin requires an instance that processes events");
        return NULL;
    }
    PendingWorkDone* pending = (PendingWorkDone*)calloc(1, sizeof(PendingWorkDone));
    if (!pending) NAPI_THROW(env, "queueWorkDoneBegin: out of memory");
    pending->instance = inst;
    pending->started_ns = monotonic_now_ns();
    WGPUQueueWorkDoneCallbackInfo cb_info = {
        .nextInChain = NULL, .mode = WGPU_CALLBACK_MODE_ALLOW_PROCESS_EVENTS,
        .callback = pending_work_done_callback, .userdata1 = pending, .userdata2 = NULL,
    };
    WGPUFuture future = pfn_wgpuQueueOnSubmittedWorkDone(queue, cb_info);
    if (future.id == 0) {
        free(pending);
        NAPI_THROW(env, "queueWorkDoneBegin: queue work-done future unavailable");
    }
    napi_value handle;
    if (napi_create_external(env, pending, pending_work_done_finalize, NULL, &handle) != napi_ok) {
        /* The registered callback still owns the allocation. */
        pending->abandoned = true;
        NAPI_THROW(env, "queueWorkDoneBegin: handle creation failed");
    }
    return handle;
}

/* Returns false while pending and true on success; throws on failure or when
 * an explicit DOE_TIMEOUT_MS/setNativeTimeoutMs deadline passes. */
napi_value doe_queue_work_done_poll(napi_env env, napi_callback_info info) {
    NAPI_ASSERT_ARGC(env, info, 1);
    PendingWorkDone* pending = NULL;
    if (napi_get_value_external(env, _args[0], (void**)&pending) != napi_ok || !pending)
        NAPI_THROW(env, "queueWorkDonePoll requires a queueWorkDoneBegin handle");
    if (!pending->result.done) pfn_wgpuInstanceProcessEvents(pending->instance);
    napi_value result;
    if (!pending->result.done) {
        if (monotonic_now_ns() - pending->started_ns >= gpu_work_timeout_ns(env)) {
            napi_throw_error(env, "DOE_QUEUE_TIMEOUT", "onSubmittedWorkDone: queue wait timed out");
            return NULL;
        }
        napi_get_boolean(env, false, &result);
        return result;
    }
    if (pending->result.status != WGPU_QUEUE_WORK_DONE_STATUS_SUCCESS)
        return throw_status_error(env, "DOE_QUEUE_FLUSH_ERROR", "onSubmittedWorkDone: queue work did not complete",
            pending->result.status, pending->result.message);
    napi_get_boolean(env, true, &result);
    return result;
}
