/* SPDX-License-Identifier: MIT */
#include "shader_identity_worker.h"
#include "shader_cache_identity.h"
#include <stdlib.h>
#include <string.h>

static void compute(void *argument)
{
    native_identity_worker *worker = argument;
    worker->valid = native_shader_cache_identity(worker->digest);
}

void native_identity_worker_start(native_identity_worker *worker)
{
    worker->valid = false;
    worker->state = doe_thread_create(&worker->thread, compute, worker).status == DOE_HOST_OK ?
        NATIVE_IDENTITY_WORKER_RUNNING : NATIVE_IDENTITY_WORKER_NONE;
}

bool native_identity_worker_claim(native_identity_worker *worker)
{
    if (worker->state != NATIVE_IDENTITY_WORKER_RUNNING) return false;
    worker->state = NATIVE_IDENTITY_WORKER_CLAIMED;
    return true;
}

native_identity_take native_identity_worker_join(native_identity_worker *worker, uint8_t digest[32])
{
    /* Join is the publication point for the worker's digest and validity. */
    if (doe_thread_join(&worker->thread).status != DOE_HOST_OK) abort();
    if (!worker->valid) return NATIVE_IDENTITY_TAKE_FAILED;
    memcpy(digest, worker->digest, sizeof(worker->digest));
    return NATIVE_IDENTITY_TAKE_READY;
}

void native_identity_worker_finish(native_identity_worker *worker)
{
    /* A CLAIMED worker was joined by its claimer before any device release
     * could finalize: that caller holds a device lease throughout. */
    if (worker->state == NATIVE_IDENTITY_WORKER_RUNNING &&
        doe_thread_join(&worker->thread).status != DOE_HOST_OK) abort();
    worker->state = NATIVE_IDENTITY_WORKER_NONE;
}
