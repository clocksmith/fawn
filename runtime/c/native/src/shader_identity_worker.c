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

native_shared_identity *native_shared_identity_start(void)
{
    native_shared_identity *identity = calloc(1, sizeof(*identity));
    if (!identity) return NULL;
    if (doe_mutex_init(&identity->mutex, false).status != DOE_HOST_OK) {
        if (doe_mutex_destroy(&identity->mutex).status != DOE_HOST_OK) abort();
        free(identity);
        return NULL;
    }
    native_identity_worker_start(&identity->worker);
    return identity;
}

native_identity_take native_shared_identity_take(native_shared_identity *identity, uint8_t digest[32])
{
    native_identity_take taken = NATIVE_IDENTITY_TAKE_NONE;
    if (doe_mutex_lock(&identity->mutex).status != DOE_HOST_OK) abort();
    if (!identity->joined) {
        identity->joined = true;
        if (native_identity_worker_claim(&identity->worker)) {
            taken = native_identity_worker_join(&identity->worker, identity->digest);
            identity->valid = taken == NATIVE_IDENTITY_TAKE_READY;
            identity->worker.state = NATIVE_IDENTITY_WORKER_NONE;
        }
    } else if (identity->valid) taken = NATIVE_IDENTITY_TAKE_READY;
    if (taken == NATIVE_IDENTITY_TAKE_READY) memcpy(digest, identity->digest, sizeof(identity->digest));
    if (doe_mutex_unlock(&identity->mutex).status != DOE_HOST_OK) abort();
    return taken;
}

void native_shared_identity_finish(native_shared_identity *identity)
{
    if (!identity) return;
    native_identity_worker_finish(&identity->worker);
    if (doe_mutex_destroy(&identity->mutex).status != DOE_HOST_OK) abort();
    free(identity);
}

void native_identity_worker_finish(native_identity_worker *worker)
{
    /* A CLAIMED worker was joined by its claimer before any device release
     * could finalize: that caller holds a device lease throughout. */
    if (worker->state == NATIVE_IDENTITY_WORKER_RUNNING &&
        doe_thread_join(&worker->thread).status != DOE_HOST_OK) abort();
    worker->state = NATIVE_IDENTITY_WORKER_NONE;
}
