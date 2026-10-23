/* SPDX-License-Identifier: MIT
 * Host control of the Darwin API branch, NOT an Apple SDK/runtime test. */
#define _POSIX_C_SOURCE 200809L
#include "../src/internal.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef enum test_mode { SPURIOUS, WAIT_ERROR, READY_RACE, NO_WAIT } test_mode;
static test_mode mode;
static unsigned clock_calls, waits, deliveries;
static doe_future_completion *completion;

int __wrap_clock_gettime(clockid_t clock, struct timespec *out);
int __wrap_pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock);

int __wrap_clock_gettime(clockid_t clock, struct timespec *out) {
    assert(clock == CLOCK_MONOTONIC);
    clock_calls++;
    if (clock_calls == 1) *out = (struct timespec){10, 900000000};
    else if (clock_calls == 2) *out = (struct timespec){11, 100000000};
    else if (clock_calls == 3) *out = (struct timespec){12, 300000000};
    else *out = (struct timespec){12, 400000000};
    return 0;
}

int __wrap_pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock) {
    (void)attr; (void)clock;
    assert(!"Darwin must not call the unavailable clock attribute");
    return EINVAL;
}

int pthread_cond_timedwait_relative_np(pthread_cond_t *cond,
    pthread_mutex_t *mutex, const struct timespec *remaining) {
    (void)cond;
    assert(mode != NO_WAIT);
    waits++;
    if (waits == 1) {
        assert(remaining->tv_sec == 1 && remaining->tv_nsec == 300000000);
    } else {
        assert(waits == 2 && remaining->tv_sec == 0 && remaining->tv_nsec == 100000000);
    }
    if (mode == SPURIOUS) return 0;
    if (mode == WAIT_ERROR) return EINVAL;
    assert(mode == READY_RACE);
    assert(pthread_mutex_unlock(mutex) == 0);
    assert(doe_future_mark_ready(completion) == DOE_FUTURE_OK);
    assert(pthread_mutex_lock(mutex) == 0);
    return ETIMEDOUT;
}

static void deliver(doe_future_completion *entry, void *userdata) {
    assert(entry == completion && userdata == &deliveries);
    deliveries++;
    assert(doe_future_finish(entry) == DOE_FUTURE_OK);
}

int main(void) {
    doe_future_storage_requirements qreq, creq;
    doe_future_queue *queue;
    uint64_t id;
    assert(doe_future_queue_requirements(1, &qreq) == DOE_FUTURE_OK);
    assert(doe_future_completion_requirements(1, &creq) == DOE_FUTURE_OK);
    void *qs = malloc(qreq.size), *cs = malloc(creq.size);
    assert(qs && cs);
    assert(doe_future_queue_init(1, qs, qreq.size, &queue) == DOE_FUTURE_OK);
    assert(doe_future_completion_init(1, cs, creq.size,
        DOE_FUTURE_MODE_WAIT_ANY_ONLY, deliver, &deliveries, &completion) == DOE_FUTURE_OK);
    assert(doe_future_new_id(queue, &id) == DOE_FUTURE_OK);
    assert(doe_future_register(queue, completion, id) == DOE_FUTURE_OK);
    doe_future_wait_info info = {id, 9};
    mode = NO_WAIT;
    assert(doe_future_wait_any(queue, &info, 1, 0) == DOE_FUTURE_TIMED_OUT);
    assert(waits == 0 && deliveries == 0 && info.completed == 0);
    mode = SPURIOUS; clock_calls = 0;
    assert(doe_future_wait_any(queue, &info, 1, UINT64_C(1500000000)) == DOE_FUTURE_TIMED_OUT);
    assert(waits == 2 && clock_calls == 4 && deliveries == 0 && info.completed == 0);
    mode = WAIT_ERROR; waits = 0; clock_calls = 0;
    assert(doe_future_wait_any(queue, &info, 1, UINT64_C(1500000000)) == DOE_FUTURE_SYSTEM_ERROR);
    assert(waits == 1 && info.completed == 0);
    mode = READY_RACE; waits = 0; clock_calls = 0;
    assert(doe_future_wait_any(queue, &info, 1, UINT64_C(1500000000)) == DOE_FUTURE_TIMED_OUT);
    assert(waits == 1 && deliveries == 0 && info.completed == 0);
    mode = NO_WAIT; waits = 0; clock_calls = 0;
    assert(doe_future_wait_any(queue, &info, 1, 0) == DOE_FUTURE_OK);
    assert(waits == 0 && deliveries == 1 && info.completed == 1);
    assert(doe_future_completion_deinit(completion) == DOE_FUTURE_OK);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    free(cs); free(qs);
    puts("Darwin branch control: decreasing relative span, borrow, spurious wake, error and timeout race PASS; no SDK/runtime claim");
    return 0;
}
