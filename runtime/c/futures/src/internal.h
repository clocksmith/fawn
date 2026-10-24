/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_FUTURES_INTERNAL_H
#define DOE_FUTURES_INTERNAL_H

#if defined(DOE_FUTURES_TEST_DARWIN) && !defined(DOE_FUTURES_TESTING)
#error "A host Darwin boundary control requires DOE_FUTURES_TESTING"
#endif
#if defined(DOE_FUTURES_TEST_DARWIN)
#define DOE_PLATFORM_TEST_DARWIN_RELATIVE 1
#endif
#include "../../platform/include/doe_platform.h"
#include "doe_futures.h"

enum df_state { DF_DEAD, DF_LIVE, DF_CLEANUP };

struct doe_future_queue {
    doe_mutex mutex;
    doe_condition changed;
    doe_future_completion *head;
    size_t active;
    uint32_t instance_id;
    uint32_t next_sequence;
    enum df_state state;
};

struct doe_future_completion {
    doe_future_queue *owner;
    doe_future_completion *next;
    doe_future_dispatch dispatch;
    void *userdata;
    uint64_t id;
    doe_thread_id delivery_thread;
    doe_future_mode mode;
    bool ready;
    bool delivering;
    bool initialized;
};

/* Caller provides lifetime exclusion against destruction/new entry. Active
 * calls stay counted while the mutex is released for waits and callbacks. */
static inline doe_future_status df_enter(doe_future_queue *queue)
{
    if (queue == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    if (queue->state != DF_LIVE) return DOE_FUTURE_INVALID_STATE;
    if (doe_mutex_lock(&queue->mutex).status != DOE_HOST_OK) return DOE_FUTURE_SYSTEM_ERROR;
    if (queue->active == SIZE_MAX) {
        (void)doe_mutex_unlock(&queue->mutex);
        return DOE_FUTURE_EXHAUSTED;
    }
    queue->active++;
    return DOE_FUTURE_OK;
}

static inline doe_future_status df_leave(doe_future_queue *queue,
                                          doe_future_status status)
{
    queue->active--;
    if (doe_mutex_unlock(&queue->mutex).status != DOE_HOST_OK)
        return DOE_FUTURE_SYSTEM_ERROR;
    return status;
}

static inline bool df_issued(const doe_future_queue *queue, uint64_t id)
{
    const uint32_t sequence = (uint32_t)id;
    return queue->instance_id != 0 && id >> 32 == queue->instance_id &&
           sequence != 0 && sequence < queue->next_sequence;
}

#endif
