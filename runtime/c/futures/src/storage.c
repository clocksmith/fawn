/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Derived-owner attribution and lifetime boundary: ../PROVENANCE.md. */
#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <time.h>

static doe_future_status requirements(uint32_t version,
    doe_future_storage_requirements *out, size_t size, size_t alignment)
{
    if (out == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    if (version != DOE_FUTURES_ABI_VERSION)
        return DOE_FUTURE_UNSUPPORTED_VERSION;
    *out = (doe_future_storage_requirements){size, alignment};
    return DOE_FUTURE_OK;
}

doe_future_status doe_future_queue_requirements(
    uint32_t version, doe_future_storage_requirements *out)
{
    return requirements(version, out, sizeof(doe_future_queue),
                        _Alignof(doe_future_queue));
}

doe_future_status doe_future_completion_requirements(
    uint32_t version, doe_future_storage_requirements *out)
{
    return requirements(version, out, sizeof(doe_future_completion),
                        _Alignof(doe_future_completion));
}

static doe_future_status preflight(uint32_t version, void *storage, size_t size,
                                    size_t needed, size_t alignment)
{
    if (version != DOE_FUTURES_ABI_VERSION)
        return DOE_FUTURE_UNSUPPORTED_VERSION;
    if (storage == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    if (size < needed) return DOE_FUTURE_INSUFFICIENT_STORAGE;
    if ((uintptr_t)storage % alignment != 0)
        return DOE_FUTURE_MISALIGNED_STORAGE;
    return DOE_FUTURE_OK;
}

/* Exclusive recovery path, including partially initialized queues. Stop at the
 * first failed destroy and preserve every not-yet-released ownership bit. */
static doe_future_status cleanup(doe_future_queue *queue)
{
    queue->state = DF_CLEANUP;
    if (queue->condition_live) {
        if (pthread_cond_destroy(&queue->changed) != 0)
            return DOE_FUTURE_CLEANUP_REQUIRED;
        queue->condition_live = false;
    }
    if (queue->attributes_live) {
        if (pthread_condattr_destroy(&queue->attributes) != 0)
            return DOE_FUTURE_CLEANUP_REQUIRED;
        queue->attributes_live = false;
    }
    if (queue->mutex_live) {
        if (pthread_mutex_destroy(&queue->mutex) != 0)
            return DOE_FUTURE_CLEANUP_REQUIRED;
        queue->mutex_live = false;
    }
    queue->state = DF_DEAD;
    return DOE_FUTURE_OK;
}

doe_future_status doe_future_queue_init(uint32_t version, void *storage,
                                         size_t size, doe_future_queue **out)
{
    doe_future_status status;
    doe_future_queue *queue;
    if (out == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    *out = NULL;
    status = preflight(version, storage, size, sizeof(*queue), _Alignof(doe_future_queue));
    if (status != DOE_FUTURE_OK) return status;
    queue = storage;
    *queue = (doe_future_queue){.next_sequence = 1, .state = DF_CLEANUP};
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) goto setup_failed;
    queue->mutex_live = true;
    if (pthread_condattr_init(&queue->attributes) != 0) goto setup_failed;
    queue->attributes_live = true;
    if (pthread_condattr_setclock(&queue->attributes, CLOCK_MONOTONIC) != 0)
        goto setup_failed;
    if (pthread_cond_init(&queue->changed, &queue->attributes) != 0)
        goto setup_failed;
    queue->condition_live = true;
    if (pthread_condattr_destroy(&queue->attributes) != 0) {
        *out = queue;
        return DOE_FUTURE_CLEANUP_REQUIRED;
    }
    queue->attributes_live = false;
    queue->state = DF_LIVE;
    *out = queue;
    return DOE_FUTURE_OK;

setup_failed:
    status = cleanup(queue);
    if (status == DOE_FUTURE_CLEANUP_REQUIRED) *out = queue;
    return status == DOE_FUTURE_OK ? DOE_FUTURE_SYSTEM_ERROR : status;
}

doe_future_status doe_future_completion_init(uint32_t version, void *storage,
    size_t size, doe_future_mode mode, doe_future_dispatch dispatch,
    void *userdata, doe_future_completion **out)
{
    doe_future_status status;
    doe_future_completion *completion;
    if (out == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    *out = NULL;
    status = preflight(version, storage, size, sizeof(*completion),
                       _Alignof(doe_future_completion));
    if (status != DOE_FUTURE_OK) return status;
    if (mode < DOE_FUTURE_MODE_COMPAT_IMMEDIATE ||
        mode > DOE_FUTURE_MODE_ALLOW_SPONTANEOUS)
        return DOE_FUTURE_INVALID_ARGUMENT;
    completion = storage;
    *completion = (doe_future_completion){
        .dispatch = dispatch, .userdata = userdata, .mode = mode,
        .initialized = true};
    *out = completion;
    return DOE_FUTURE_OK;
}

doe_future_status doe_future_queue_deinit(doe_future_queue *queue)
{
    if (queue == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    if (queue->state == DF_CLEANUP) return cleanup(queue);
    if (queue->state != DF_LIVE) return DOE_FUTURE_INVALID_STATE;
    if (pthread_mutex_lock(&queue->mutex) != 0) return DOE_FUTURE_SYSTEM_ERROR;
    if (queue->head != NULL || queue->active != 0) {
        (void)pthread_mutex_unlock(&queue->mutex);
        return DOE_FUTURE_BUSY;
    }
    queue->state = DF_CLEANUP;
    if (pthread_mutex_unlock(&queue->mutex) != 0)
        return DOE_FUTURE_CLEANUP_REQUIRED;
    return cleanup(queue);
}

doe_future_status doe_future_completion_deinit(doe_future_completion *completion)
{
    if (completion == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    if (!completion->initialized) return DOE_FUTURE_INVALID_STATE;
    if (completion->owner != NULL) return DOE_FUTURE_BUSY;
    completion->initialized = false;
    return DOE_FUTURE_OK;
}
