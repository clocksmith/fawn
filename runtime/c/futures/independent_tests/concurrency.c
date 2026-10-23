/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "support.h"

typedef struct gate { pthread_mutex_t mutex; pthread_cond_t cond; unsigned value; } gate;
#define GATE_INIT {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0}
int __real_pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int __real_pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
static gate entered_wait = GATE_INIT;
static _Thread_local bool capture_wait;
static void advance(gate *g, unsigned value) {
    POK(pthread_mutex_lock(&g->mutex)); g->value = value;
    POK(pthread_cond_broadcast(&g->cond)); POK(pthread_mutex_unlock(&g->mutex));
}
static void await(gate *g, unsigned value) {
    POK(pthread_mutex_lock(&g->mutex));
    while (g->value < value) POK(__real_pthread_cond_wait(&g->cond, &g->mutex));
    POK(pthread_mutex_unlock(&g->mutex));
}
static void destroy_gate(gate *g) {
    POK(pthread_cond_destroy(&g->cond)); POK(pthread_mutex_destroy(&g->mutex));
}
int __wrap_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    if (capture_wait) { capture_wait = false; advance(&entered_wait, 1); }
    return __real_pthread_cond_wait(cond, mutex);
}
int __wrap_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                 const struct timespec *deadline) {
    if (capture_wait) { capture_wait = false; advance(&entered_wait, 1); }
    return __real_pthread_cond_timedwait(cond, mutex, deadline);
}
typedef struct wait_job { doe_future_queue *q; uint64_t id; uint64_t timeout; } wait_job;
static void *wait_worker(void *data) {
    wait_job *job = data; doe_future_wait_info info = {job->id, 55};
    capture_wait = true;
    OK(doe_future_wait_any(job->q, &info, 1, job->timeout));
    CHECK(info.completed == 1); return NULL;
}
static void settle(doe_future_completion *n, void *data) {
    unsigned *calls = data; ++*calls; OK(doe_future_finish(n));
}
static void test_wait_wake(bool finite) {
    advance(&entered_wait, 0);
    doe_future_queue *q = new_queue(); unsigned calls = 0;
    doe_future_completion *n = new_completion(DOE_FUTURE_MODE_WAIT_ANY_ONLY, settle, &calls);
    uint64_t id = next_id(q); OK(doe_future_register(q, n, id));
    wait_job job = {q, id, finite ? UINT64_C(5000000000) : UINT64_MAX};
    pthread_t waiter; POK(pthread_create(&waiter, NULL, wait_worker, &job));
    await(&entered_wait, 1);
    CHECK(doe_future_queue_deinit(q) == DOE_FUTURE_BUSY);
    if (finite) OK(doe_future_finish(n)); else OK(doe_future_mark_ready(n));
    POK(pthread_join(waiter, NULL)); CHECK(calls == (finite ? 0u : 1u));
    drop_completion(n); drop_queue(q);
    report(finite ? "finite_wait_external_finish_handshake" : "infinite_wait_concurrent_readiness_handshake");
}
typedef struct hold_job {
    doe_future_queue *q;
    doe_future_completion *n;
    uint64_t id;
    gate progress;
    gate permission;
    unsigned calls;
} hold_job;
static void hold_callback(doe_future_completion *n, void *data) {
    hold_job *job = data; ++job->calls;
    uint32_t complete; CHECK(poll(job->q, job->id, &complete) == DOE_FUTURE_OK && complete == 1);
    advance(&job->progress, 1); await(&job->permission, 1);
    OK(doe_future_finish(n)); drop_completion(n);
    CHECK(doe_future_queue_deinit(job->q) == DOE_FUTURE_BUSY);
    advance(&job->progress, 2); await(&job->permission, 2);
}
static void *pump_worker(void *data) { OK(doe_future_process_events(data)); return NULL; }
static void test_callback_lifetime(void) {
    hold_job job = {.progress = GATE_INIT, .permission = GATE_INIT};
    job.q = new_queue(); job.n = new_completion(DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, hold_callback, &job);
    job.id = next_id(job.q); OK(doe_future_register(job.q, job.n, job.id)); OK(doe_future_mark_ready(job.n));
    pthread_t pump; POK(pthread_create(&pump, NULL, pump_worker, job.q)); await(&job.progress, 1);
    uint32_t complete; CHECK(poll(job.q, job.id, &complete) == DOE_FUTURE_TIMED_OUT && complete == 0);
    OK(doe_future_process_events(job.q)); CHECK(doe_future_queue_deinit(job.q) == DOE_FUTURE_BUSY);
    advance(&job.permission, 1); await(&job.progress, 2);
    CHECK(poll(job.q, job.id, &complete) == DOE_FUTURE_OK && complete == 1);
    CHECK(doe_future_queue_deinit(job.q) == DOE_FUTURE_BUSY);
    advance(&job.permission, 2); POK(pthread_join(pump, NULL)); CHECK(job.calls == 1);
    drop_queue(job.q); destroy_gate(&job.progress); destroy_gate(&job.permission);
    report("self_vs_other_wait/callback_free/active_call_destroy_busy");
}
typedef struct overlap {
    doe_future_queue *q;
    gate *count;
    gate *release;
    unsigned calls;
} overlap;
static void overlap_callback(doe_future_completion *n, void *data) {
    overlap *job = data; ++job->calls;
    OK(doe_future_finish(n)); drop_completion(n);
    POK(pthread_mutex_lock(&job->count->mutex)); ++job->count->value;
    POK(pthread_cond_broadcast(&job->count->cond)); POK(pthread_mutex_unlock(&job->count->mutex));
    await(job->release, 1);
}
static void test_concurrent_pumps(void) {
    doe_future_queue *q = new_queue(); gate count = GATE_INIT, release = GATE_INIT;
    overlap jobs[2] = {{q, &count, &release, 0}, {q, &count, &release, 0}};
    for (unsigned i = 0; i < 2; ++i) {
        doe_future_completion *n = new_completion(DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, overlap_callback, &jobs[i]);
        OK(doe_future_register(q, n, next_id(q))); OK(doe_future_mark_ready(n));
    }
    pthread_t pumps[2]; POK(pthread_create(&pumps[0], NULL, pump_worker, q)); await(&count, 1);
    POK(pthread_create(&pumps[1], NULL, pump_worker, q)); await(&count, 2);
    CHECK(doe_future_queue_deinit(q) == DOE_FUTURE_BUSY);
    advance(&release, 1);
    for (unsigned i = 0; i < 2; ++i) { POK(pthread_join(pumps[i], NULL)); CHECK(jobs[i].calls == 1); }
    drop_queue(q); destroy_gate(&count); destroy_gate(&release);
    report("concurrent_pumps_exactly_once_no_global_order_assumption");
}
#define ISSUERS 8
#define IDS_PER_ISSUER 128
typedef struct issue_job { doe_future_queue *q; pthread_barrier_t *start; uint64_t ids[IDS_PER_ISSUER]; } issue_job;
static void *issue_worker(void *data) {
    issue_job *job = data; int status = pthread_barrier_wait(job->start);
    CHECK(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned i = 0; i < IDS_PER_ISSUER; ++i) job->ids[i] = next_id(job->q);
    return NULL;
}
static int compare_id(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y);
}
static void test_concurrent_ids(void) {
    doe_future_queue *q = new_queue(); pthread_barrier_t barrier;
    POK(pthread_barrier_init(&barrier, NULL, ISSUERS)); issue_job jobs[ISSUERS]; pthread_t threads[ISSUERS];
    for (unsigned i = 0; i < ISSUERS; ++i) {
        jobs[i].q = q; jobs[i].start = &barrier;
        POK(pthread_create(&threads[i], NULL, issue_worker, &jobs[i]));
    }
    uint64_t ids[ISSUERS * IDS_PER_ISSUER];
    for (unsigned i = 0; i < ISSUERS; ++i) {
        POK(pthread_join(threads[i], NULL)); memcpy(&ids[i * IDS_PER_ISSUER], jobs[i].ids, sizeof(jobs[i].ids));
        for (unsigned j = 1; j < IDS_PER_ISSUER; ++j) CHECK(jobs[i].ids[j] > jobs[i].ids[j - 1]);
    }
    qsort(ids, ISSUERS * IDS_PER_ISSUER, sizeof(ids[0]), compare_id);
    for (unsigned i = 1; i < ISSUERS * IDS_PER_ISSUER; ++i) CHECK(ids[i] == ids[i - 1] + 1);
    POK(pthread_barrier_destroy(&barrier)); drop_queue(q);
    report("concurrent_id_uniqueness_1024");
}
int main(void) {
    test_wait_wake(false); test_wait_wake(true); test_callback_lifetime();
    test_concurrent_pumps(); test_concurrent_ids(); destroy_gate(&entered_wait);
    return 0;
}
