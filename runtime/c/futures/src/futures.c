/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Derived from doe_future_ids.zig; see ../PROVENANCE.md. */
#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <time.h>

#if !defined(DOE_FUTURES_TESTING) && \
    (defined(DOE_FUTURES_TEST_INSTANCE_LIMIT) || \
     defined(DOE_FUTURES_TEST_SEQUENCE_LIMIT) || \
     defined(DOE_FUTURES_TEST_LEGACY_LIMIT))
#error "Identity endpoint overrides require DOE_FUTURES_TESTING"
#endif
#ifndef DOE_FUTURES_TEST_INSTANCE_LIMIT
#define DOE_FUTURES_TEST_INSTANCE_LIMIT UINT32_C(0x7fffffff)
#endif
#ifndef DOE_FUTURES_TEST_SEQUENCE_LIMIT
#define DOE_FUTURES_TEST_SEQUENCE_LIMIT UINT32_C(0xfffffffe)
#endif
#ifndef DOE_FUTURES_TEST_LEGACY_LIMIT
#define DOE_FUTURES_TEST_LEGACY_LIMIT (UINT64_MAX - UINT64_C(1))
#endif
#define LEGACY_FIRST UINT64_C(0x8000000000000000)
#define NS_PER_SECOND UINT64_C(1000000000)
_Static_assert(DOE_FUTURES_TEST_INSTANCE_LIMIT >= 1 &&
               DOE_FUTURES_TEST_INSTANCE_LIMIT <= UINT32_C(0x7fffffff),
               "Instance test endpoint must stay in namespace");
_Static_assert(DOE_FUTURES_TEST_SEQUENCE_LIMIT >= 1 &&
               DOE_FUTURES_TEST_SEQUENCE_LIMIT <= UINT32_C(0xfffffffe),
               "Sequence test endpoint must stay in namespace");
_Static_assert(DOE_FUTURES_TEST_LEGACY_LIMIT >= LEGACY_FIRST &&
               DOE_FUTURES_TEST_LEGACY_LIMIT < UINT64_MAX,
               "Legacy test endpoint must stay in namespace");
_Static_assert(sizeof(time_t) <= sizeof(uintmax_t) && (time_t)-1 < 0,
               "This pthread boundary requires signed time_t no wider than uintmax_t");

static _Atomic uint32_t next_instance = 1;
static _Atomic uint64_t next_legacy = LEGACY_FIRST;

static doe_future_status issue_instance(uint32_t *out)
{
    uint32_t current = atomic_load_explicit(&next_instance, memory_order_relaxed);
    for (;;) {
        if (current > DOE_FUTURES_TEST_INSTANCE_LIMIT)
            return DOE_FUTURE_EXHAUSTED;
        if (atomic_compare_exchange_weak_explicit(&next_instance, &current,
                current + 1, memory_order_relaxed, memory_order_relaxed)) {
            *out = current;
            return DOE_FUTURE_OK;
        }
    }
}

doe_future_status doe_future_legacy_id(uint64_t *out)
{
    uint64_t current;
    if (out == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    current = atomic_load_explicit(&next_legacy, memory_order_relaxed);
    for (;;) {
        if (current > DOE_FUTURES_TEST_LEGACY_LIMIT)
            return DOE_FUTURE_EXHAUSTED;
        if (atomic_compare_exchange_weak_explicit(&next_legacy, &current,
                current + 1, memory_order_relaxed, memory_order_relaxed)) {
            *out = current;
            return DOE_FUTURE_OK;
        }
    }
}

doe_future_status doe_future_new_id(doe_future_queue *queue, uint64_t *out)
{
    doe_future_status status;
    if (out == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    status = df_enter(queue);
    if (status != DOE_FUTURE_OK) return status;
    if (queue->instance_id == 0) {
        status = issue_instance(&queue->instance_id);
        if (status != DOE_FUTURE_OK) return df_leave(queue, status);
    }
    if (queue->next_sequence > DOE_FUTURES_TEST_SEQUENCE_LIMIT)
        return df_leave(queue, DOE_FUTURE_EXHAUSTED);
    *out = ((uint64_t)queue->instance_id << 32) | queue->next_sequence;
    queue->next_sequence++;
    return df_leave(queue, DOE_FUTURE_OK);
}

doe_future_status doe_future_register(doe_future_queue *queue,
                                      doe_future_completion *completion,
                                      uint64_t id)
{
    doe_future_status status;
    if (completion == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    if (!completion->initialized) return DOE_FUTURE_INVALID_STATE;
    status = df_enter(queue);
    if (status != DOE_FUTURE_OK) return status;
    if (completion->owner != NULL) return df_leave(queue, DOE_FUTURE_DUPLICATE);
    if (!df_issued(queue, id)) return df_leave(queue, DOE_FUTURE_UNKNOWN_ID);
    for (doe_future_completion *entry = queue->head; entry; entry = entry->next)
        if (entry->id == id) return df_leave(queue, DOE_FUTURE_DUPLICATE);
    completion->owner = queue;
    completion->id = id;
    completion->next = queue->head;
    queue->head = completion;
    return df_leave(queue, DOE_FUTURE_OK);
}

doe_future_status doe_future_begin_delivery(doe_future_completion *completion)
{
    doe_future_queue *queue;
    doe_future_status status;
    if (completion == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    if (!completion->initialized) return DOE_FUTURE_INVALID_STATE;
    queue = completion->owner;
    if (queue == NULL) return DOE_FUTURE_OK;
    status = df_enter(queue);
    if (status != DOE_FUTURE_OK) return status;
    if (completion->delivering) return df_leave(queue, DOE_FUTURE_INVALID_STATE);
    completion->delivering = true;
    completion->delivery_thread = pthread_self();
    return df_leave(queue, DOE_FUTURE_OK);
}

/* Enter and return with queue locked, except unrecoverable synchronization error.
 * The callback can retire and free completion. Never touch it after dispatch. */
static doe_future_status dispatch_locked(doe_future_queue *queue,
                                          doe_future_completion *completion)
{
    const doe_future_dispatch dispatch = completion->dispatch;
    void *const userdata = completion->userdata;
    completion->delivering = true;
    completion->delivery_thread = pthread_self();
    if (pthread_mutex_unlock(&queue->mutex) != 0)
        return DOE_FUTURE_SYSTEM_ERROR;
    dispatch(completion, userdata);
    if (pthread_mutex_lock(&queue->mutex) != 0)
        return DOE_FUTURE_SYSTEM_ERROR;
    return DOE_FUTURE_OK;
}

doe_future_status doe_future_mark_ready(doe_future_completion *completion)
{
    doe_future_queue *queue;
    doe_future_status status;
    bool spontaneous;
    if (completion == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    if (!completion->initialized || completion->dispatch == NULL)
        return DOE_FUTURE_INVALID_STATE;
    queue = completion->owner;
    if (queue == NULL) {
        const doe_future_dispatch dispatch = completion->dispatch;
        void *const userdata = completion->userdata;
        dispatch(completion, userdata);
        return DOE_FUTURE_OK;
    }
    status = df_enter(queue);
    if (status != DOE_FUTURE_OK) return status;
    if (completion->ready || completion->delivering)
        return df_leave(queue, DOE_FUTURE_INVALID_STATE);
    completion->ready = true;
    spontaneous = completion->mode == DOE_FUTURE_MODE_COMPAT_IMMEDIATE ||
                  completion->mode == DOE_FUTURE_MODE_ALLOW_SPONTANEOUS;
    status = pthread_cond_broadcast(&queue->changed) == 0 ?
             DOE_FUTURE_OK : DOE_FUTURE_SYSTEM_ERROR;
    if (spontaneous && dispatch_locked(queue, completion) != DOE_FUTURE_OK)
        return DOE_FUTURE_SYSTEM_ERROR;
    return df_leave(queue, status);
}

doe_future_status doe_future_finish(doe_future_completion *completion)
{
    doe_future_queue *queue;
    doe_future_completion **link;
    doe_future_status status;
    if (completion == NULL) return DOE_FUTURE_INVALID_ARGUMENT;
    if (!completion->initialized) return DOE_FUTURE_INVALID_STATE;
    queue = completion->owner;
    if (queue == NULL) return DOE_FUTURE_OK;
    status = df_enter(queue);
    if (status != DOE_FUTURE_OK) return status;
    for (link = &queue->head; *link != NULL; link = &(*link)->next) {
        if (*link != completion) continue;
        *link = completion->next;
        completion->owner = NULL;
        completion->next = NULL;
        status = pthread_cond_broadcast(&queue->changed) == 0 ?
                 DOE_FUTURE_OK : DOE_FUTURE_SYSTEM_ERROR;
        return df_leave(queue, status);
    }
    return df_leave(queue, DOE_FUTURE_INVALID_STATE);
}

doe_future_status doe_future_process_events(doe_future_queue *queue)
{
    doe_future_status status = df_enter(queue);
    uint32_t boundary;
    if (status != DOE_FUTURE_OK) return status;
    boundary = queue->next_sequence;
    for (;;) {
        doe_future_completion *selected = NULL;
        for (doe_future_completion *entry = queue->head; entry;
             entry = entry->next) {
            if (!entry->ready || entry->delivering ||
                entry->mode == DOE_FUTURE_MODE_WAIT_ANY_ONLY ||
                (uint32_t)entry->id >= boundary) continue;
            if (selected == NULL || entry->id < selected->id) selected = entry;
        }
        if (selected == NULL) return df_leave(queue, DOE_FUTURE_OK);
        if (dispatch_locked(queue, selected) != DOE_FUTURE_OK)
            return DOE_FUTURE_SYSTEM_ERROR;
    }
}

static doe_future_status monotonic_now(struct timespec *out)
{
    if (clock_gettime(CLOCK_MONOTONIC, out) != 0 || out->tv_sec < 0 ||
        out->tv_nsec < 0 || out->tv_nsec >= (long)NS_PER_SECOND)
        return DOE_FUTURE_SYSTEM_ERROR;
    return DOE_FUTURE_OK;
}

static doe_future_status make_deadline(uint64_t timeout, struct timespec *out)
{
    struct timespec start;
    const uintmax_t max_seconds =
        (UINTMAX_C(1) << (sizeof(time_t) * CHAR_BIT - 1)) - UINTMAX_C(1);
    uint64_t seconds, nanoseconds;
    doe_future_status status = monotonic_now(&start);
    if (status != DOE_FUTURE_OK) return status;
    seconds = timeout / NS_PER_SECOND;
    nanoseconds = (uint64_t)start.tv_nsec + timeout % NS_PER_SECOND;
    if (nanoseconds >= NS_PER_SECOND) {
        nanoseconds -= NS_PER_SECOND;
        seconds++;
    }
    if (seconds > max_seconds ||
        (uintmax_t)start.tv_sec > max_seconds - seconds)
        return DOE_FUTURE_TIME_OVERFLOW;
    out->tv_sec = start.tv_sec + (time_t)seconds;
    out->tv_nsec = (long)nanoseconds;
    return DOE_FUTURE_OK;
}

static bool deadline_reached(const struct timespec *now,
                             const struct timespec *deadline)
{
    return now->tv_sec > deadline->tv_sec ||
           (now->tv_sec == deadline->tv_sec && now->tv_nsec >= deadline->tv_nsec);
}

doe_future_status doe_future_wait_any(doe_future_queue *queue,
    doe_future_wait_info *infos, size_t count, uint64_t timeout)
{
    struct timespec deadline = {0, 0};
    doe_future_status status;
    if (count == 0) return DOE_FUTURE_OK;
    if (infos == NULL || count > SIZE_MAX / sizeof(*infos))
        return DOE_FUTURE_INVALID_ARGUMENT;
    if (timeout != DOE_FUTURES_TIMEOUT_INFINITE) {
        status = make_deadline(timeout, &deadline);
        if (status != DOE_FUTURE_OK) return status;
    }
    status = df_enter(queue);
    if (status != DOE_FUTURE_OK) return status;
    for (size_t i = 0; i < count; ++i) {
        infos[i].completed = 0;
        if (!df_issued(queue, infos[i].id))
            return df_leave(queue, DOE_FUTURE_UNKNOWN_ID);
    }
    for (;;) {
        bool completed = false;
        doe_future_completion *selected = NULL;
        const pthread_t current = pthread_self();
        for (size_t i = 0; i < count; ++i) {
            bool pending = false;
            for (doe_future_completion *entry = queue->head; entry;
                 entry = entry->next) {
                if (entry->id != infos[i].id) continue;
                pending = !entry->delivering ||
                          !pthread_equal(entry->delivery_thread, current);
                if (entry->ready && !entry->delivering &&
                    (selected == NULL || entry->id < selected->id))
                    selected = entry;
                break;
            }
            infos[i].completed = pending ? 0 : 1;
            completed = completed || !pending;
        }
        if (selected != NULL) {
            if (dispatch_locked(queue, selected) != DOE_FUTURE_OK)
                return DOE_FUTURE_SYSTEM_ERROR;
            continue;
        }
        if (completed) return df_leave(queue, DOE_FUTURE_OK);
        if (timeout != DOE_FUTURES_TIMEOUT_INFINITE) {
            struct timespec now;
            int error;
            status = monotonic_now(&now);
            if (status != DOE_FUTURE_OK) return df_leave(queue, status);
            if (deadline_reached(&now, &deadline))
                return df_leave(queue, DOE_FUTURE_TIMED_OUT);
#if DF_RELATIVE_CONDITION
            /* Darwin has no condition-clock attribute. Recompute the remaining
             * span from the one absolute monotonic deadline after each wake;
             * never restart the original timeout or consult wall-clock time. */
            struct timespec remaining = {
                deadline.tv_sec - now.tv_sec, deadline.tv_nsec - now.tv_nsec};
            if (remaining.tv_nsec < 0) {
                remaining.tv_sec--;
                remaining.tv_nsec += (long)NS_PER_SECOND;
            }
            error = pthread_cond_timedwait_relative_np(&queue->changed,
                &queue->mutex, &remaining);
#else
            error = pthread_cond_timedwait(&queue->changed, &queue->mutex,
                                           &deadline);
#endif
            /* Do not re-scan after ETIMEDOUT: a later wait observes a race. */
            if (error == ETIMEDOUT) return df_leave(queue, DOE_FUTURE_TIMED_OUT);
            if (error != 0) return df_leave(queue, DOE_FUTURE_SYSTEM_ERROR);
        } else if (pthread_cond_wait(&queue->changed, &queue->mutex) != 0) {
            return df_leave(queue, DOE_FUTURE_SYSTEM_ERROR);
        }
    }
}
