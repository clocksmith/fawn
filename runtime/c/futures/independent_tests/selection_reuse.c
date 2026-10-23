/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Supplemental execution of already-frozen inventory 2, 9, 11, 20. */
#include "support.h"
typedef struct schedule {
    doe_future_queue *q;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool entered;
    bool release;
    unsigned log[8];
    unsigned size;
} schedule;
typedef struct item { schedule *s; doe_future_completion *n; uint64_t id; unsigned tag; bool hold; } item;
static void callback(doe_future_completion *n, void *data) {
    item *i = data; schedule *s = i->s;
    s->log[s->size++] = i->tag;
    if (i->hold) {
        POK(pthread_mutex_lock(&s->mutex)); s->entered = true; POK(pthread_cond_broadcast(&s->cond));
        while (!s->release) POK(pthread_cond_wait(&s->cond, &s->mutex));
        POK(pthread_mutex_unlock(&s->mutex));
    }
    OK(doe_future_finish(n));
}
static void *pump(void *data) { OK(doe_future_process_events(data)); return NULL; }
static void test_concurrent_publication(void) {
    schedule s = {.mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};
    s.q = new_queue(); item items[4] = {{&s, NULL, 0, 1, true}, {&s, NULL, 0, 2, false}, {&s, NULL, 0, 3, false}, {&s, NULL, 0, 4, false}};
    for (unsigned i = 0; i < 4; ++i) items[i].n = new_completion(i == 2 ? DOE_FUTURE_MODE_WAIT_ANY_ONLY : DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, callback, &items[i]);
    for (unsigned i = 0; i < 3; ++i) items[i].id = next_id(s.q);
    for (unsigned i = 3; i-- > 0;) OK(doe_future_register(s.q, items[i].n, items[i].id));
    OK(doe_future_mark_ready(items[0].n)); OK(doe_future_mark_ready(items[2].n));
    pthread_t thread; POK(pthread_create(&thread, NULL, pump, s.q));
    POK(pthread_mutex_lock(&s.mutex));
    while (!s.entered) POK(pthread_cond_wait(&s.cond, &s.mutex));
    POK(pthread_mutex_unlock(&s.mutex));
    uint32_t complete; CHECK(poll(s.q, items[0].id, &complete) == DOE_FUTURE_TIMED_OUT && complete == 0);
    OK(doe_future_mark_ready(items[1].n));
    items[3].id = next_id(s.q); OK(doe_future_register(s.q, items[3].n, items[3].id)); OK(doe_future_mark_ready(items[3].n));
    CHECK(doe_future_queue_deinit(s.q) == DOE_FUTURE_BUSY);
    POK(pthread_mutex_lock(&s.mutex)); s.release = true; POK(pthread_cond_broadcast(&s.cond)); POK(pthread_mutex_unlock(&s.mutex));
    POK(pthread_join(thread, NULL));
    CHECK(s.size == 2 && s.log[0] == 1 && s.log[1] == 2);
    CHECK(poll(s.q, items[2].id, &complete) == DOE_FUTURE_OK && complete == 1);
    CHECK(s.size == 3 && s.log[2] == 3);
    OK(doe_future_process_events(s.q)); CHECK(s.size == 4 && s.log[3] == 4);
    for (unsigned i = 0; i < 4; ++i) drop_completion(items[i].n);
    drop_queue(s.q); POK(pthread_cond_destroy(&s.cond)); POK(pthread_mutex_destroy(&s.mutex));
    report("concurrent_publisher_pump_wait/preboundary_readiness/new_issue_deferred");
}
static void test_pump_order(void) {
    schedule s = {.mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER};
    s.q = new_queue(); item items[4];
    for (unsigned i = 0; i < 4; ++i) {
        items[i] = (item){&s, NULL, next_id(s.q), i + 1, false};
        items[i].n = new_completion(i == 1 ? DOE_FUTURE_MODE_WAIT_ANY_ONLY : DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, callback, &items[i]);
    }
    for (unsigned i = 4; i-- > 0;) { OK(doe_future_register(s.q, items[i].n, items[i].id)); OK(doe_future_mark_ready(items[i].n)); }
    OK(doe_future_process_events(s.q)); CHECK(s.size == 3 && s.log[0] == 1 && s.log[1] == 3 && s.log[2] == 4);
    uint32_t complete; CHECK(poll(s.q, items[1].id, &complete) == DOE_FUTURE_OK && complete == 1);
    CHECK(s.size == 4 && s.log[3] == 2);
    for (unsigned i = 0; i < 4; ++i) drop_completion(items[i].n);
    drop_queue(s.q); POK(pthread_cond_destroy(&s.cond)); POK(pthread_mutex_destroy(&s.mutex));
    report("serial_lowest_eligible_pump_order/mixed_wait_only");
}
static void test_completion_reuse(void) {
    doe_future_queue *q = new_queue(); doe_future_storage_requirements r;
    OK(doe_future_completion_requirements(1, &r)); void *storage = storage_for(r);
    uint64_t ids[2];
    for (unsigned i = 0; i < 2; ++i) {
        doe_future_completion *n = NULL;
        OK(doe_future_completion_init(1, storage, r.size, DOE_FUTURE_MODE_WAIT_ANY_ONLY, NULL, NULL, &n));
        ids[i] = next_id(q); OK(doe_future_register(q, n, ids[i]));
        uint32_t complete; CHECK(poll(q, ids[i], &complete) == DOE_FUTURE_TIMED_OUT && complete == 0);
        OK(doe_future_finish(n)); OK(doe_future_completion_deinit(n));
    }
    CHECK(ids[1] > ids[0]); doe_future_wait_info infos[] = {{ids[0], 0}, {ids[1], 0}};
    OK(doe_future_wait_any(q, infos, 2, 0)); CHECK(infos[0].completed == 1 && infos[1].completed == 1);
    free(storage); drop_queue(q); report("completion_deinit_reinit_new_id_reuse_without_tombstones");
}
int main(void) {
    test_concurrent_publication(); test_pump_order(); test_completion_reuse(); return 0;
}
