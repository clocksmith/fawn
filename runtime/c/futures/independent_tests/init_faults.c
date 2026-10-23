/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "support.h"
enum { MUTEX_INIT = 1, ATTR_INIT, SET_CLOCK, COND_INIT, ATTR_DESTROY, COND_DESTROY, MUTEX_DESTROY, OP_COUNT };
static unsigned fail[OP_COUNT], attempts[OP_COUNT], successes[OP_COUNT];
static bool mutex_live, attr_live, cond_live;
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_condattr_init(pthread_condattr_t *);
int __real_pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __real_pthread_condattr_destroy(pthread_condattr_t *);
int __real_pthread_cond_destroy(pthread_cond_t *);
int __real_pthread_mutex_destroy(pthread_mutex_t *);
static bool should_fail(unsigned op) {
    attempts[op]++;
    if (!fail[op]) return false;
    --fail[op]; return true;
}
int __wrap_pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
    CHECK(!mutex_live); if (should_fail(MUTEX_INIT)) return EAGAIN;
    int r = __real_pthread_mutex_init(m, a); if (!r) { mutex_live = true; successes[MUTEX_INIT]++; } return r;
}
int __wrap_pthread_condattr_init(pthread_condattr_t *a) {
    CHECK(!attr_live); if (should_fail(ATTR_INIT)) return ENOMEM;
    int r = __real_pthread_condattr_init(a); if (!r) { attr_live = true; successes[ATTR_INIT]++; } return r;
}
int __wrap_pthread_condattr_setclock(pthread_condattr_t *a, clockid_t c) {
    CHECK(attr_live && c == CLOCK_MONOTONIC); if (should_fail(SET_CLOCK)) return EINVAL;
    int r = __real_pthread_condattr_setclock(a, c); if (!r) successes[SET_CLOCK]++; return r;
}
int __wrap_pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) {
    CHECK(!cond_live && attr_live && a); if (should_fail(COND_INIT)) return EAGAIN;
    int r = __real_pthread_cond_init(c, a); if (!r) { cond_live = true; successes[COND_INIT]++; } return r;
}
int __wrap_pthread_condattr_destroy(pthread_condattr_t *a) {
    CHECK(attr_live); if (should_fail(ATTR_DESTROY)) return EBUSY;
    int r = __real_pthread_condattr_destroy(a); if (!r) { attr_live = false; successes[ATTR_DESTROY]++; } return r;
}
int __wrap_pthread_cond_destroy(pthread_cond_t *c) {
    CHECK(cond_live); if (should_fail(COND_DESTROY)) return EBUSY;
    int r = __real_pthread_cond_destroy(c); if (!r) { cond_live = false; successes[COND_DESTROY]++; } return r;
}
int __wrap_pthread_mutex_destroy(pthread_mutex_t *m) {
    CHECK(mutex_live); if (should_fail(MUTEX_DESTROY)) return EBUSY;
    int r = __real_pthread_mutex_destroy(m); if (!r) { mutex_live = false; successes[MUTEX_DESTROY]++; } return r;
}
static void reset(void) {
    CHECK(!mutex_live && !attr_live && !cond_live);
    memset(fail, 0, sizeof(fail)); memset(attempts, 0, sizeof(attempts)); memset(successes, 0, sizeof(successes));
}
static void verify_balanced(void) {
    CHECK(!mutex_live && !attr_live && !cond_live);
    CHECK(successes[MUTEX_INIT] == successes[MUTEX_DESTROY]);
    CHECK(successes[ATTR_INIT] == successes[ATTR_DESTROY]);
    CHECK(successes[COND_INIT] == successes[COND_DESTROY]);
}
static void reuse(void *storage, size_t size) {
    doe_future_queue *q = NULL; OK(doe_future_queue_init(1, storage, size, &q)); CHECK(q);
    CHECK(next_id(q)); OK(doe_future_queue_deinit(q)); verify_balanced();
}
static void test_setup_failures(void *s, size_t size) {
    for (unsigned op = MUTEX_INIT; op <= COND_INIT; ++op) {
        reset(); fail[op] = 1; doe_future_queue *q = (doe_future_queue *)s;
        CHECK(doe_future_queue_init(1, s, size, &q) == DOE_FUTURE_SYSTEM_ERROR);
        CHECK(!q && attempts[op] == 1); verify_balanced(); reuse(s, size);
    }
    report("all_sync_acquisition_failures_cleanup_reuse");
}
static void test_init_cleanup_failures(void *s, size_t size) {
    const unsigned setup[] = {0, SET_CLOCK, COND_INIT};
    const unsigned destroy[] = {ATTR_DESTROY, ATTR_DESTROY, MUTEX_DESTROY};
    for (unsigned i = 0; i < 3; ++i) {
        reset(); if (setup[i]) fail[setup[i]] = 1; fail[destroy[i]] = 1;
        doe_future_queue *q = NULL;
        CHECK(doe_future_queue_init(1, s, size, &q) == DOE_FUTURE_CLEANUP_REQUIRED);
        CHECK(q && attempts[destroy[i]] == 1);
        CHECK(mutex_live || attr_live || cond_live);
        uint64_t untouched = 101;
        CHECK(doe_future_new_id(q, &untouched) != DOE_FUTURE_OK && untouched == 101);
        CHECK(doe_future_process_events(q) != DOE_FUTURE_OK);
        OK(doe_future_queue_deinit(q)); CHECK(attempts[destroy[i]] == 2);
        verify_balanced(); reuse(s, size);
    }
    report("init_rollback_destroy_failures_cleanup_only_retry");
}
static void test_deinit_failures(void *s, size_t size) {
    for (unsigned op = COND_DESTROY; op <= MUTEX_DESTROY; ++op) {
        reset(); doe_future_queue *q = NULL; OK(doe_future_queue_init(1, s, size, &q));
        fail[op] = 2;
        CHECK(doe_future_queue_deinit(q) == DOE_FUTURE_CLEANUP_REQUIRED);
        CHECK(attempts[op] == 1);
        uint64_t untouched = 202;
        CHECK(doe_future_new_id(q, &untouched) != DOE_FUTURE_OK && untouched == 202);
        CHECK(doe_future_process_events(q) != DOE_FUTURE_OK);
        CHECK(doe_future_queue_deinit(q) == DOE_FUTURE_CLEANUP_REQUIRED);
        CHECK(attempts[op] == 2);
        OK(doe_future_queue_deinit(q)); CHECK(attempts[op] == 3);
        verify_balanced(); reuse(s, size);
    }
    report("deinit_failures_exact_live_member_retries");
}
int main(void) {
    doe_future_storage_requirements r; OK(doe_future_queue_requirements(1, &r)); void *s = storage_for(r);
    test_setup_failures(s, r.size); test_init_cleanup_failures(s, r.size); test_deinit_failures(s, r.size);
    free(s); return 0;
}
