/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "support.h"
int __real_clock_gettime(clockid_t, struct timespec *);
int __real_pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int __real_pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
static int clock_mode, wait_mode;
static struct timespec fake_now, deadlines[4];
static unsigned timed_calls, infinite_calls;
static pthread_mutex_t race_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t race_cond = PTHREAD_COND_INITIALIZER;
static unsigned race_stage;
static doe_future_completion *race_node;
int __wrap_clock_gettime(clockid_t id, struct timespec *out) {
    CHECK(id == CLOCK_MONOTONIC);
    if (clock_mode == 1) { errno = EIO; return -1; }
    if (clock_mode == 2) { *out = fake_now; return 0; }
    return __real_clock_gettime(id, out);
}
int __wrap_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    ++infinite_calls;
    if (wait_mode == 2) return EINVAL;
    return __real_pthread_cond_wait(cond, mutex);
}
int __wrap_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                 const struct timespec *deadline) {
    CHECK(timed_calls < 4); deadlines[timed_calls++] = *deadline;
    if (wait_mode == 1) { fake_now.tv_nsec++; return timed_calls < 3 ? 0 : ETIMEDOUT; }
    if (wait_mode == 2) return EINVAL;
    if (wait_mode == 3) {
        POK(pthread_mutex_unlock(mutex));
        POK(pthread_mutex_lock(&race_mutex)); race_stage = 1; POK(pthread_cond_broadcast(&race_cond));
        while (race_stage < 2) POK(__real_pthread_cond_wait(&race_cond, &race_mutex));
        POK(pthread_mutex_unlock(&race_mutex)); POK(pthread_mutex_lock(mutex));
        return ETIMEDOUT;
    }
    return __real_pthread_cond_timedwait(cond, mutex, deadline);
}
static void reset(void) { clock_mode = wait_mode = 0; timed_calls = infinite_calls = 0; }
static doe_future_completion *pending(doe_future_queue *q, uint64_t *id) {
    doe_future_completion *n = new_completion(DOE_FUTURE_MODE_WAIT_ANY_ONLY, NULL, NULL);
    *id = next_id(q); OK(doe_future_register(q, n, *id)); return n;
}
static void test_deadlines(void) {
    doe_future_queue *q = new_queue(); uint64_t id;
    doe_future_completion *n = pending(q, &id); doe_future_wait_info info = {id, 5};
    reset(); clock_mode = 2; fake_now = (struct timespec){10, 500}; wait_mode = 1;
    CHECK(doe_future_wait_any(q, &info, 1, 1000) == DOE_FUTURE_TIMED_OUT && info.completed == 0);
    CHECK(timed_calls == 3);
    for (unsigned i = 0; i < 3; ++i) CHECK(deadlines[i].tv_sec == 10 && deadlines[i].tv_nsec == 1500);
    CHECK(doe_future_queue_deinit(q) == DOE_FUTURE_BUSY);
    reset(); clock_mode = 1;
    CHECK(doe_future_wait_any(q, &info, 1, 1) == DOE_FUTURE_SYSTEM_ERROR);
    reset(); wait_mode = 2;
    CHECK(doe_future_wait_any(q, &info, 1, 1000000000) == DOE_FUTURE_SYSTEM_ERROR);
    reset(); wait_mode = 2;
    CHECK(doe_future_wait_any(q, &info, 1, UINT64_MAX) == DOE_FUTURE_SYSTEM_ERROR);
    CHECK(infinite_calls == 1 && timed_calls == 0);
    reset(); clock_mode = 2;
    CHECK((time_t)-1 < (time_t)0 && sizeof(time_t) <= sizeof(uintmax_t));
    fake_now.tv_sec = (time_t)((UINTMAX_C(1) << (sizeof(time_t) * CHAR_BIT - 1)) - 1);
    fake_now.tv_nsec = 999999999;
    CHECK(doe_future_wait_any(q, &info, 1, 1) == DOE_FUTURE_TIME_OVERFLOW);
    reset(); CHECK(doe_future_wait_any(q, &info, 1, UINT64_C(1000000)) == DOE_FUTURE_TIMED_OUT);
    CHECK(info.completed == 0 && timed_calls >= 1);
    OK(doe_future_finish(n)); drop_completion(n); drop_queue(q);
    report("one_monotonic_deadline/spurious_wakes/errors/overflow/real_timeout");
}
static void elapsed_callback(doe_future_completion *n, void *data) {
    unsigned *calls = data; ++*calls; fake_now.tv_sec += 100; OK(doe_future_finish(n));
}
static void test_callback_elapsed(void) {
    doe_future_queue *q = new_queue(); unsigned calls = 0;
    doe_future_completion *n = new_completion(DOE_FUTURE_MODE_WAIT_ANY_ONLY, elapsed_callback, &calls);
    uint64_t id = next_id(q); OK(doe_future_register(q, n, id)); OK(doe_future_mark_ready(n));
    reset(); clock_mode = 2; fake_now = (struct timespec){10, 0};
    doe_future_wait_info info = {id, 0}; OK(doe_future_wait_any(q, &info, 1, 1));
    CHECK(calls == 1 && info.completed == 1 && fake_now.tv_sec == 110 && timed_calls == 0);
    reset(); drop_completion(n); drop_queue(q);
    report("callback_elapsed_scan_complete_wins_without_preemption");
}
static void *race_settler(void *unused) {
    (void)unused; POK(pthread_mutex_lock(&race_mutex));
    while (race_stage < 1) POK(__real_pthread_cond_wait(&race_cond, &race_mutex));
    POK(pthread_mutex_unlock(&race_mutex)); OK(doe_future_finish(race_node));
    POK(pthread_mutex_lock(&race_mutex)); race_stage = 2; POK(pthread_cond_broadcast(&race_cond));
    POK(pthread_mutex_unlock(&race_mutex)); return NULL;
}
static void test_timeout_race(void) {
    doe_future_queue *q = new_queue(); uint64_t id; race_node = pending(q, &id);
    reset(); wait_mode = 3; race_stage = 0;
    pthread_t thread; POK(pthread_create(&thread, NULL, race_settler, NULL));
    doe_future_wait_info info = {id, 88};
    CHECK(doe_future_wait_any(q, &info, 1, UINT64_C(1000000000)) == DOE_FUTURE_TIMED_OUT);
    POK(pthread_join(thread, NULL)); CHECK(race_stage == 2);
    reset(); OK(doe_future_wait_any(q, &info, 1, 0)); CHECK(info.completed == 1);
    drop_completion(race_node); drop_queue(q);
    report("injected_etimedout_race_settles_before_mutex_reacquisition");
}
int main(void) {
    test_deadlines(); test_callback_elapsed(); test_timeout_race();
    POK(pthread_cond_destroy(&race_cond)); POK(pthread_mutex_destroy(&race_mutex));
    return 0;
}
