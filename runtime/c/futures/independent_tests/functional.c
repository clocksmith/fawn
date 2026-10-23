/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "support.h"

typedef struct probe {
    doe_future_queue *q;
    doe_future_completion *node;
    uint64_t id;
    unsigned calls;
    int tag;
    int *log;
    unsigned *log_size;
    struct probe *follower;
    unsigned behavior;
    pthread_t expected_thread;
} probe;

enum { FREE_NODE = 1, LEAVE_PENDING = 2, READY_OLD = 4,
       ISSUE_NEW = 8, RECURSE = 16, REENTER = 32, CHECK_THREAD = 64 };

static void deliver(doe_future_completion *node, void *userdata) {
    probe *p = userdata;
    p->calls++;
    CHECK(p->calls == 1);
    if (p->behavior & CHECK_THREAD) CHECK(pthread_equal(pthread_self(), p->expected_thread));
    if (p->log) p->log[(*p->log_size)++] = p->tag;
    if (p->behavior & REENTER) {
        uint32_t complete;
        CHECK(poll(p->q, p->id, &complete) == DOE_FUTURE_OK && complete == 1);
        CHECK(doe_future_queue_deinit(p->q) == DOE_FUTURE_BUSY);
        (void)next_id(p->q);
        OK(doe_future_process_events(p->q));
    }
    if (p->behavior & ISSUE_NEW) {
        p->follower->id = next_id(p->q);
        OK(doe_future_register(p->q, p->follower->node, p->follower->id));
        OK(doe_future_mark_ready(p->follower->node));
    }
    if (p->behavior & READY_OLD) OK(doe_future_mark_ready(p->follower->node));
    if (p->behavior & RECURSE) OK(doe_future_process_events(p->q));
    if (!(p->behavior & LEAVE_PENDING)) OK(doe_future_finish(node));
    if (p->behavior & FREE_NODE) {
        drop_completion(node);
        p->node = NULL;
        CHECK(doe_future_queue_deinit(p->q) == DOE_FUTURE_BUSY);
    }
}
static void init_probe(probe *p, doe_future_queue *q, doe_future_mode mode,
                       unsigned behavior) {
    memset(p, 0, sizeof(*p));
    p->q = q;
    p->behavior = behavior;
    p->expected_thread = pthread_self();
    p->node = new_completion(mode, deliver, p);
}
static void register_probe(probe *p) {
    p->id = next_id(p->q);
    OK(doe_future_register(p->q, p->node, p->id));
}
static void release_probe(probe *p) {
    if (p->node) drop_completion(p->node);
}
static void test_storage(void) {
    doe_future_storage_requirements qr = {33, 44}, nr = {55, 66};
    CHECK(doe_future_queue_requirements(0, &qr) == DOE_FUTURE_UNSUPPORTED_VERSION);
    CHECK(qr.size == 33 && qr.alignment == 44);
    CHECK(doe_future_completion_requirements(2, &nr) == DOE_FUTURE_UNSUPPORTED_VERSION);
    CHECK(nr.size == 55 && nr.alignment == 66);
    CHECK(doe_future_queue_requirements(1, NULL) == DOE_FUTURE_INVALID_ARGUMENT);
    CHECK(doe_future_completion_requirements(1, NULL) == DOE_FUTURE_INVALID_ARGUMENT);
    OK(doe_future_queue_requirements(1, &qr));
    OK(doe_future_completion_requirements(1, &nr));
    size_t a = qr.alignment > nr.alignment ? qr.alignment : nr.alignment;
    if (a < alignof(max_align_t)) a = alignof(max_align_t);
    size_t len = ((qr.size > nr.size ? qr.size : nr.size) + a - 1) / a * a;
    unsigned char *raw = aligned_alloc(a, len + 2 * a);
    CHECK(raw != NULL); memset(raw, 0xa7, len + 2 * a);
    void *s = raw + a;
    doe_future_queue *q = (doe_future_queue *)s;
    CHECK(doe_future_queue_init(0, s, len, &q) == DOE_FUTURE_UNSUPPORTED_VERSION && !q);
    CHECK(doe_future_queue_init(1, NULL, len, &q) == DOE_FUTURE_INVALID_ARGUMENT && !q);
    CHECK(doe_future_queue_init(1, s, qr.size - 1, &q) == DOE_FUTURE_INSUFFICIENT_STORAGE && !q);
    CHECK(doe_future_queue_init(1, s, len, NULL) == DOE_FUTURE_INVALID_ARGUMENT);
    if (qr.alignment > 1) CHECK(doe_future_queue_init(1, raw + a + 1, len, &q) == DOE_FUTURE_MISALIGNED_STORAGE && !q);
    OK(doe_future_queue_init(1, s, len, &q)); OK(doe_future_queue_deinit(q));
    OK(doe_future_queue_init(1, s, len, &q)); OK(doe_future_queue_deinit(q));
    doe_future_completion *n = (doe_future_completion *)s;
    CHECK(doe_future_completion_init(0, s, len, 0, NULL, NULL, &n) == DOE_FUTURE_UNSUPPORTED_VERSION && !n);
    CHECK(doe_future_completion_init(1, NULL, len, 0, NULL, NULL, &n) == DOE_FUTURE_INVALID_ARGUMENT && !n);
    CHECK(doe_future_completion_init(1, s, nr.size - 1, 0, NULL, NULL, &n) == DOE_FUTURE_INSUFFICIENT_STORAGE && !n);
    CHECK(doe_future_completion_init(1, s, len, (doe_future_mode)4, NULL, NULL, &n) == DOE_FUTURE_INVALID_ARGUMENT && !n);
    CHECK(doe_future_completion_init(1, s, len, 0, NULL, NULL, NULL) == DOE_FUTURE_INVALID_ARGUMENT);
    if (nr.alignment > 1) CHECK(doe_future_completion_init(1, raw + a + 1, len, 0, NULL, NULL, &n) == DOE_FUTURE_MISALIGNED_STORAGE && !n);
    OK(doe_future_completion_init(1, s, len, 0, NULL, NULL, &n)); OK(doe_future_completion_deinit(n));
    for (size_t i = 0; i < a; ++i) CHECK(raw[i] == 0xa7 && raw[a + len + i] == 0xa7);
    free(raw);
    report("storage/version/guards/reuse");
}
static void test_ids_registration(void) {
    doe_future_queue *q = new_queue(), *other = new_queue();
    uint64_t a = next_id(q), b = next_id(q), foreign = next_id(other), legacy;
    CHECK((uint32_t)a == 1 && b == a + 1 && foreign != a);
    OK(doe_future_legacy_id(&legacy)); CHECK((legacy >> 63) == 1);
    uint64_t legacy2; OK(doe_future_legacy_id(&legacy2)); CHECK(legacy2 == legacy + 1);
    doe_future_completion *n = new_completion(DOE_FUTURE_MODE_WAIT_ANY_ONLY, NULL, NULL);
    doe_future_completion *dup = new_completion(DOE_FUTURE_MODE_WAIT_ANY_ONLY, NULL, NULL);
    CHECK(doe_future_register(q, n, 0) == DOE_FUTURE_UNKNOWN_ID);
    CHECK(doe_future_register(q, n, foreign) == DOE_FUTURE_UNKNOWN_ID);
    CHECK(doe_future_register(q, n, b + 1) == DOE_FUTURE_UNKNOWN_ID);
    OK(doe_future_register(q, n, a));
    CHECK(doe_future_register(q, n, b) == DOE_FUTURE_DUPLICATE);
    CHECK(doe_future_register(q, dup, a) == DOE_FUTURE_DUPLICATE);
    CHECK(doe_future_completion_deinit(n) == DOE_FUTURE_BUSY);
    CHECK(doe_future_queue_deinit(q) == DOE_FUTURE_BUSY);
    CHECK(doe_future_mark_ready(n) != DOE_FUTURE_OK);
    uint32_t completed;
    CHECK(poll(q, a, &completed) == DOE_FUTURE_TIMED_OUT && completed == 0);
    CHECK(poll(q, b, &completed) == DOE_FUTURE_OK && completed == 1);
    doe_future_wait_info infos[] = {{a, 77}, {foreign, 88}, {b, 99}};
    CHECK(doe_future_wait_any(q, infos, 3, 0) == DOE_FUTURE_UNKNOWN_ID);
    CHECK(infos[0].completed == 0 && infos[1].completed == 0 && infos[2].completed == 99);
    OK(doe_future_wait_any(q, NULL, 0, 0));
    OK(doe_future_begin_delivery(n));
    CHECK(doe_future_begin_delivery(n) == DOE_FUTURE_INVALID_STATE);
    CHECK(poll(q, a, &completed) == DOE_FUTURE_OK && completed == 1);
    OK(doe_future_finish(n)); OK(doe_future_finish(n));
    CHECK(poll(q, a, &completed) == DOE_FUTURE_OK && completed == 1);
    drop_completion(n); drop_completion(dup);
    doe_future_storage_requirements qr; OK(doe_future_queue_requirements(1, &qr));
    OK(doe_future_queue_deinit(q));
    OK(doe_future_queue_init(1, q, qr.size, &q));
    CHECK(next_id(q) != a);
    drop_queue(q); drop_queue(other);
    report("identity/registration/flags/manual");
}
static void test_modes_and_free(void) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        doe_future_queue *q = new_queue(); probe p;
        init_probe(&p, q, (doe_future_mode)mode, FREE_NODE | REENTER | CHECK_THREAD);
        register_probe(&p); OK(doe_future_mark_ready(p.node));
        if (mode == 1 || mode == 2) CHECK(p.calls == 0);
        if (mode == 1) { OK(doe_future_process_events(q)); CHECK(p.calls == 0); }
        if (mode == 2) OK(doe_future_process_events(q));
        uint32_t completed; CHECK(poll(q, p.id, &completed) == DOE_FUTURE_OK && completed == 1);
        CHECK(p.calls == 1 && !p.node);
        OK(doe_future_process_events(q)); drop_queue(q);
    }
    probe p; init_probe(&p, NULL, DOE_FUTURE_MODE_WAIT_ANY_ONLY, 0);
    OK(doe_future_mark_ready(p.node)); CHECK(p.calls == 1); release_probe(&p);
    report("all_modes/ownerless/self_wait/reentrant_free");
}
static void test_order_wait(void) {
    doe_future_queue *q = new_queue(), *other = new_queue();
    probe p[4]; int log[4]; unsigned size = 0;
    for (unsigned i = 0; i < 4; ++i) {
        init_probe(&p[i], q, DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, 0);
        p[i].id = next_id(q); p[i].tag = (int)i; p[i].log = log; p[i].log_size = &size;
    }
    for (unsigned i = 4; i-- > 0;) { OK(doe_future_register(q, p[i].node, p[i].id)); OK(doe_future_mark_ready(p[i].node)); }
    OK(doe_future_process_events(other)); CHECK(size == 0);
    doe_future_wait_info infos[] = {{p[3].id, 0}, {p[1].id, 0}, {p[3].id, 0}, {next_id(q), 0}, {p[2].id, 0}, {p[0].id, 0}};
    OK(doe_future_wait_any(q, infos, 6, 0));
    CHECK(size == 4); for (unsigned i = 0; i < 4; ++i) CHECK(log[i] == (int)i);
    for (unsigned i = 0; i < 6; ++i) CHECK(infos[i].completed == 1);
    for (unsigned i = 0; i < 4; ++i) release_probe(&p[i]);
    drop_queue(q); drop_queue(other);
    report("lowest_wait_order/duplicates/ready_before_complete");
}
static void test_boundaries(void) {
    for (unsigned kind = 0; kind < 3; ++kind) {
        doe_future_queue *q = new_queue(); probe leader, follower;
        unsigned behavior = kind == 0 ? READY_OLD : ISSUE_NEW;
        if (kind == 2) behavior |= RECURSE;
        init_probe(&leader, q, DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, behavior);
        init_probe(&follower, q, DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, 0);
        leader.follower = &follower;
        register_probe(&leader);
        if (kind == 0) register_probe(&follower);
        OK(doe_future_mark_ready(leader.node)); OK(doe_future_process_events(q));
        CHECK(leader.calls == 1 && follower.calls == (kind == 1 ? 0u : 1u));
        OK(doe_future_process_events(q)); CHECK(follower.calls == 1);
        release_probe(&leader); release_probe(&follower); drop_queue(q);
    }
    report("issuance_boundary/late_readiness/recursive_boundary");
}
static void *other_poll(void *userdata) {
    probe *p = userdata; uint32_t completed;
    CHECK(poll(p->q, p->id, &completed) == DOE_FUTURE_TIMED_OUT && completed == 0);
    return NULL;
}
static void test_unfinished(void) {
    doe_future_queue *q = new_queue(); probe p;
    init_probe(&p, q, DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, LEAVE_PENDING);
    register_probe(&p); OK(doe_future_mark_ready(p.node)); OK(doe_future_process_events(q));
    OK(doe_future_process_events(q)); CHECK(p.calls == 1);
    pthread_t thread; POK(pthread_create(&thread, NULL, other_poll, &p)); POK(pthread_join(thread, NULL));
    CHECK(doe_future_queue_deinit(q) == DOE_FUTURE_BUSY);
    OK(doe_future_finish(p.node)); release_probe(&p); drop_queue(q);
    report("callback_without_finish_stays_pending");
}
int main(void) {
    test_storage(); test_ids_registration(); test_modes_and_free();
    test_order_wait(); test_boundaries(); test_unfinished();
    return 0;
}
