/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_FUTURES_INTERNAL_H
#define DOE_FUTURES_INTERNAL_H

#include "doe_futures.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#if !defined(__linux__)
#error "The frozen futures platform is Linux/POSIX pthreads"
#endif

enum df_state { DF_DEAD, DF_LIVE, DF_CLEANUP };

struct doe_future_queue {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_condattr_t attributes;
    doe_future_completion *head;
    size_t active;
    uint32_t instance_id;
    uint32_t next_sequence;
    enum df_state state;
    bool mutex_live;
    bool condition_live;
    bool attributes_live;
};

struct doe_future_completion {
    doe_future_queue *owner;
    doe_future_completion *next;
    doe_future_dispatch dispatch;
    void *userdata;
    uint64_t id;
    pthread_t delivery_thread;
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
    if (pthread_mutex_lock(&queue->mutex) != 0) return DOE_FUTURE_SYSTEM_ERROR;
    if (queue->active == SIZE_MAX) {
        (void)pthread_mutex_unlock(&queue->mutex);
        return DOE_FUTURE_EXHAUSTED;
    }
    queue->active++;
    return DOE_FUTURE_OK;
}

static inline doe_future_status df_leave(doe_future_queue *queue,
                                          doe_future_status status)
{
    queue->active--;
    if (pthread_mutex_unlock(&queue->mutex) != 0)
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
