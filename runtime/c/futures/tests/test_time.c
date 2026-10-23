/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _POSIX_C_SOURCE 200809L
#include "doe_futures.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum clock_mode { REAL_TIME, SPURIOUS, OVERFLOW, CLOCK_FAILURE, TIMEOUT_RACE };
static enum clock_mode mode;
static unsigned clock_calls, condition_calls, deliveries;
static struct timespec first_deadline;
static doe_future_completion *pending;
int __real_clock_gettime(clockid_t, struct timespec *);
int __real_pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
int __wrap_clock_gettime(clockid_t id, struct timespec *out)
{
    if (mode == REAL_TIME) return __real_clock_gettime(id, out);
    assert(id == CLOCK_MONOTONIC);
    clock_calls++;
    if (mode == CLOCK_FAILURE) { errno = EIO; return -1; }
    if (mode == OVERFLOW) {
        out->tv_sec = (time_t)((UINTMAX_C(1) << (sizeof(time_t) * CHAR_BIT - 1)) - 1);
        out->tv_nsec = 999999999;
    } else {
        out->tv_sec = 10;
        out->tv_nsec = 900000000;
        if (mode == SPURIOUS && clock_calls >= 3)
            out->tv_nsec += clock_calls == 3 ? 20 : 100;
    }
    return 0;
}
int __wrap_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                 const struct timespec *deadline)
{
    if (mode == REAL_TIME) return __real_pthread_cond_timedwait(cond, mutex, deadline);
    condition_calls++;
    if (condition_calls == 1) first_deadline = *deadline;
    assert(deadline->tv_sec == first_deadline.tv_sec &&
           deadline->tv_nsec == first_deadline.tv_nsec);
    if (mode == SPURIOUS) return 0;
    assert(mode == TIMEOUT_RACE);
    /* Model a condition wait releasing/reacquiring its mutex, with readiness
     * publication winning just before its observable ETIMEDOUT return. */
    assert(pthread_mutex_unlock(mutex) == 0);
    assert(doe_future_mark_ready(pending) == DOE_FUTURE_OK);
    assert(pthread_mutex_lock(mutex) == 0);
    return ETIMEDOUT;
}
static void deliver(doe_future_completion *completion, void *userdata)
{
    (void)userdata;
    deliveries++;
    assert(doe_future_finish(completion) == DOE_FUTURE_OK);
}
int main(void)
{
    doe_future_storage_requirements qreq, creq;
    doe_future_queue *queue;
    uint64_t id;
    assert(doe_future_queue_requirements(1, &qreq) == DOE_FUTURE_OK);
    assert(doe_future_completion_requirements(1, &creq) == DOE_FUTURE_OK);
    void *qs = malloc(qreq.size), *cs = malloc(creq.size);
    assert(qs != NULL && cs != NULL);
    assert(doe_future_queue_init(1, qs, qreq.size, &queue) == DOE_FUTURE_OK);
    assert(doe_future_completion_init(1, cs, creq.size,
        DOE_FUTURE_MODE_WAIT_ANY_ONLY, deliver, NULL, &pending) == DOE_FUTURE_OK);
    assert(doe_future_new_id(queue, &id) == DOE_FUTURE_OK);
    assert(doe_future_register(queue, pending, id) == DOE_FUTURE_OK);
    doe_future_wait_info info = {id, 9};
    mode = SPURIOUS;
    assert(doe_future_wait_any(queue, &info, 1, 100) == DOE_FUTURE_TIMED_OUT);
    assert(clock_calls == 4 && condition_calls == 2 && info.completed == 0);
    assert(first_deadline.tv_sec == 10 && first_deadline.tv_nsec == 900000100);
    mode = OVERFLOW;
    assert(doe_future_wait_any(queue, &info, 1, 2) == DOE_FUTURE_TIME_OVERFLOW);
    mode = CLOCK_FAILURE;
    assert(doe_future_wait_any(queue, &info, 1, 1) == DOE_FUTURE_SYSTEM_ERROR);
    mode = REAL_TIME;
    assert(doe_future_wait_any(queue, &info, 1, 1000000) == DOE_FUTURE_TIMED_OUT);
    mode = TIMEOUT_RACE; condition_calls = 0; clock_calls = 0;
    assert(doe_future_wait_any(queue, &info, 1, 1000) == DOE_FUTURE_TIMED_OUT);
    assert(condition_calls == 1 && deliveries == 0 && info.completed == 0);
    assert(doe_future_wait_any(queue, &info, 1, 0) == DOE_FUTURE_OK);
    assert(deliveries == 1 && info.completed == 1);
    assert(doe_future_completion_deinit(pending) == DOE_FUTURE_OK);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    free(cs); free(qs);
    puts("Developer monotonic deadline, overflow and timeout-race tests passed.");
    return 0;
}
