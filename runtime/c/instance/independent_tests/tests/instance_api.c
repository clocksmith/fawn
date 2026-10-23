/* SPDX-License-Identifier: MIT
 * Independently authored Instance 1 contract tests. No candidate internals. */
#define _GNU_SOURCE
#include "doe_instance.h"
#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static _Atomic unsigned queue_calls;
static _Atomic unsigned queue_inits;
static _Atomic unsigned queue_deinits;
static _Atomic unsigned wait_entries;
static _Atomic unsigned serial;
static unsigned init_serial, deinit_serial;
static size_t requirement_alignment, requirement_size;
static int override_alignment;
static unsigned next_serial(void) { return atomic_fetch_add(&serial, 1) + 1; }

doe_future_status __real_doe_future_queue_requirements(uint32_t, doe_future_storage_requirements *);
doe_future_status __wrap_doe_future_queue_requirements(uint32_t v, doe_future_storage_requirements *out) {
    doe_future_status s = __real_doe_future_queue_requirements(v, out);
    if (s == DOE_FUTURE_OK && override_alignment) out->alignment = requirement_alignment;
    if (s == DOE_FUTURE_OK && requirement_size) out->size = requirement_size;
    return s;
}
doe_future_status __real_doe_future_queue_init(uint32_t, void *, size_t, doe_future_queue **);
doe_future_status __wrap_doe_future_queue_init(uint32_t v, void *p, size_t n, doe_future_queue **out) {
    atomic_fetch_add(&queue_inits, 1); init_serial = next_serial();
    return __real_doe_future_queue_init(v, p, n, out);
}
doe_future_status __real_doe_future_queue_deinit(doe_future_queue *);
doe_future_status __wrap_doe_future_queue_deinit(doe_future_queue *q) {
    assert(atomic_load(&queue_calls) == 0);
    doe_future_status s = __real_doe_future_queue_deinit(q);
    if (s == DOE_FUTURE_OK) { atomic_fetch_add(&queue_deinits, 1); deinit_serial = next_serial(); }
    return s;
}
doe_future_status __real_doe_future_wait_any(doe_future_queue *, doe_future_wait_info *, size_t, uint64_t);
doe_future_status __wrap_doe_future_wait_any(doe_future_queue *q, doe_future_wait_info *w, size_t n, uint64_t t) {
    atomic_fetch_add(&queue_calls, 1); atomic_fetch_add(&wait_entries, 1);
    doe_future_status s = __real_doe_future_wait_any(q, w, n, t);
    atomic_fetch_sub(&queue_calls, 1); return s;
}
doe_future_status __real_doe_future_process_events(doe_future_queue *);
doe_future_status __wrap_doe_future_process_events(doe_future_queue *q) {
    atomic_fetch_add(&queue_calls, 1);
    doe_future_status s = __real_doe_future_process_events(q);
    atomic_fetch_sub(&queue_calls, 1); return s;
}
doe_future_status __real_doe_future_mark_ready(doe_future_completion *);
doe_future_status __wrap_doe_future_mark_ready(doe_future_completion *p) {
    atomic_fetch_add(&queue_calls, 1);
    doe_future_status s = __real_doe_future_mark_ready(p);
    atomic_fetch_sub(&queue_calls, 1); return s;
}

typedef struct Observation Observation;
typedef struct WaitState { Observation *observation; } WaitState;
struct Observation {
    _Atomic unsigned allocations, frees, wait_allocations, wait_frees, external_calls;
    _Atomic uint32_t external;
    unsigned observed, labels, allocated_serial, label_serial, free_serial;
    void *pointer; size_t size, alignment;
    int fail_object, fail_wait;
    WaitState wait_state;
};
static const unsigned object_vtable = 173, wait_vtable = 199;
typedef struct WaitRecord {
    void *pointer; void *state; const void *vtable; size_t size, alignment;
    struct WaitRecord *next;
} WaitRecord;
static pthread_mutex_t wait_record_mutex = PTHREAD_MUTEX_INITIALIZER;
static WaitRecord *wait_records;

static void *object_allocate(void *state, const void *vtable, size_t n, size_t a) {
    Observation *o = state; void *p = NULL;
    assert(vtable == &object_vtable && n > 0 && a > 0 && (a & (a - 1)) == 0);
    if (o->fail_object) return NULL;
    assert(posix_memalign(&p, a < sizeof(void *) ? sizeof(void *) : a, n) == 0);
    o->pointer = p; o->size = n; o->alignment = a;
    atomic_fetch_add(&o->allocations, 1); return p;
}
static void object_free(void *state, const void *vtable, void *p, size_t n, size_t a) {
    Observation *o = state;
    assert(vtable == &object_vtable && p == o->pointer && n == o->size && a == o->alignment);
    assert(atomic_load(&queue_calls) == 0);
    o->free_serial = next_serial(); atomic_fetch_add(&o->frees, 1); free(p);
}
static void *wait_allocate(void *state, const void *vtable, size_t n, size_t a) {
    WaitState *binding = state; Observation *o = binding->observation; void *p = NULL;
    assert(binding == &o->wait_state);
    assert(vtable == &wait_vtable && n > 0 && a > 0);
    if (o->fail_wait) return NULL;
    assert(posix_memalign(&p, a < sizeof(void *) ? sizeof(void *) : a, n) == 0);
    WaitRecord *record = malloc(sizeof(*record)); assert(record);
    *record = (WaitRecord){p, state, vtable, n, a, NULL};
    assert(pthread_mutex_lock(&wait_record_mutex) == 0);
    record->next = wait_records; wait_records = record;
    assert(pthread_mutex_unlock(&wait_record_mutex) == 0);
    atomic_fetch_add(&o->wait_allocations, 1); return p;
}
static void wait_free(void *state, const void *vtable, void *p, size_t n, size_t a) {
    WaitState *binding = state; Observation *o = binding->observation;
    assert(binding == &o->wait_state);
    assert(vtable == &wait_vtable && p && n > 0 && a > 0);
    assert(atomic_load(&o->frees) == 0);
    assert(pthread_mutex_lock(&wait_record_mutex) == 0);
    WaitRecord **slot = &wait_records;
    while (*slot && (*slot)->pointer != p) slot = &(*slot)->next;
    assert(*slot); WaitRecord *record = *slot;
    assert(record->state == state && record->vtable == vtable && record->size == n && record->alignment == a);
    *slot = record->next;
    assert(pthread_mutex_unlock(&wait_record_mutex) == 0);
    free(record); atomic_fetch_add(&o->wait_frees, 1); free(p);
}
static void allocated(void *userdata, doe_instance *p, size_t n, size_t a) {
    Observation *o = userdata;
    assert((void *)p == o->pointer && n == o->size && a == o->alignment);
    ++o->observed; o->allocated_serial = next_serial();
}
static uint32_t external_count(void *userdata, doe_instance *p) {
    Observation *o = userdata; assert((void *)p == o->pointer);
    atomic_fetch_add(&o->external_calls, 1); return atomic_load(&o->external);
}
static void remove_label(void *userdata, doe_instance *p) {
    Observation *o = userdata; assert((void *)p == o->pointer);
    assert(atomic_load(&o->frees) == 0 && deinit_serial > init_serial);
    ++o->labels; o->label_serial = next_serial();
}
static doe_instance_config config(Observation *o) {
    o->wait_state.observation = o;
    return (doe_instance_config){ .abi_version = 1,
        .object_allocator = {o, &object_vtable, object_allocate, object_free},
        .wait_allocator = {&o->wait_state, &wait_vtable, wait_allocate, wait_free},
        .hook_userdata = o, .allocated = allocated,
        .external_texture_count = external_count, .remove_label = remove_label };
}
static doe_instance *create(Observation *o) {
    doe_instance *p = NULL; doe_instance_config c = config(o);
    assert(doe_instance_create(&c, &p) == DOE_INSTANCE_OK && p);
    assert(o->allocated_serial < init_serial && o->observed == 1);
    return p;
}
static void final(Observation *o) {
    assert(atomic_load(&o->allocations) == 1 && atomic_load(&o->frees) == 1);
    assert(o->labels == 1 && deinit_serial < o->label_serial && o->label_serial < o->free_serial);
    assert(atomic_load(&o->wait_allocations) == atomic_load(&o->wait_frees));
}
static uint64_t new_id(doe_instance *p) {
    uint64_t id = 0; doe_instance_queue_lease l = doe_instance_borrow_queue(p);
    assert(doe_future_new_id(l.queue, &id) == DOE_FUTURE_OK && id); return id;
}

static void validation(void) {
    Observation o = {0}; doe_instance_config c = config(&o); doe_instance *p = (doe_instance *)&o;
    assert(doe_instance_create(NULL, NULL) == DOE_INSTANCE_INVALID_ARGUMENT);
    assert(doe_instance_create(&c, NULL) == DOE_INSTANCE_INVALID_ARGUMENT);
    assert(doe_instance_create(NULL, &p) == DOE_INSTANCE_INVALID_ARGUMENT && !p);
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char *m = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(m != MAP_FAILED && mprotect(m + page, page, PROT_NONE) == 0);
    uint32_t *v = (uint32_t *)(m + page - _Alignof(doe_instance_config));
    for (unsigned i = 0; i < 2; ++i) {
        *v = i ? 2 : 0; p = (doe_instance *)&o;
        assert(doe_instance_create((const doe_instance_config *)v, &p) == DOE_INSTANCE_UNSUPPORTED_VERSION && !p);
    }
    assert(munmap(m, page * 2) == 0);
    for (unsigned i = 0; i < 4; ++i) {
        c = config(&o);
        if (i == 0) c.object_allocator.allocate = NULL;
        if (i == 1) c.object_allocator.deallocate = NULL;
        if (i == 2) c.wait_allocator.allocate = NULL;
        if (i == 3) c.wait_allocator.deallocate = NULL;
        assert(doe_instance_create(&c, &p) == DOE_INSTANCE_INVALID_ARGUMENT && !p);
    }
    assert(!atomic_load(&o.allocations) && !o.observed);
    doe_instance_retain(NULL); doe_instance_release(NULL); doe_instance_undo_retain(NULL); doe_instance_process_events(NULL);
    assert(!doe_instance_from_handle(NULL));
    _Alignas(64) uint32_t readable[16] = {0xDEADC0DE};
    assert(!doe_instance_from_handle(readable));
    for (unsigned i = 0; i < 3; ++i) {
        uint64_t t = i == 2 ? UINT64_MAX : i ? 17 : 0;
        assert(doe_instance_wait_any(NULL, 0, NULL, t) == WGPUWaitStatus_Success);
    }
    WGPUFutureWaitInfo w = {{0}, 0x11};
    assert(doe_instance_wait_any(NULL, 1, &w, 0) == WGPUWaitStatus_Error && w.completed == 0x11);
}
static void lifecycle(void) {
    for (unsigned i = 0; i < 257; ++i) {
        Observation o = {0}; doe_instance *p = create(&o);
        assert(doe_instance_from_handle(p) == p);
        doe_instance_queue_lease a = doe_instance_borrow_queue(p), b = a;
        a = (doe_instance_queue_lease){0};
        assert(b.userdata == p && b.queue && b.retain && b.release);
        doe_future_storage_requirements r;
        assert(doe_future_queue_requirements(1, &r) == DOE_FUTURE_OK);
        assert((uintptr_t)b.queue % r.alignment == 0 && (uintptr_t)b.queue > (uintptr_t)p);
        assert((uintptr_t)b.queue + r.size <= (uintptr_t)p + o.size);
        doe_instance_retain(p); b.retain(b.userdata);
        doe_instance_release(p); b.release(b.userdata);
        assert(!atomic_load(&o.frees)); doe_instance_release(p); final(&o);
    }
    Observation o = {0}; doe_instance_config c = config(&o); doe_instance *p = NULL;
    assert(doe_instance_create(&c, &p) == DOE_INSTANCE_OK);
    memset(&c, 0xCD, sizeof(c));
    WGPUFutureWaitInfo w = {{new_id(p)}, 0x11};
    assert(doe_instance_wait_any(p, 1, &w, 0) == WGPUWaitStatus_Success && w.completed == 1);
    doe_instance_release(p); final(&o);
}
static void guard(void) {
    Observation o = {0}; doe_instance *p = create(&o); atomic_store(&o.external, 7);
    doe_instance_release(p); doe_instance_retain(p); doe_instance_retain(p);
    doe_instance_release(p); doe_instance_release(p); doe_instance_release(p);
    assert(!atomic_load(&o.frees));
    unsigned calls = atomic_load(&o.external_calls);
    doe_instance_retain(p); doe_instance_undo_retain(p);
    assert(atomic_load(&o.external_calls) == calls && !atomic_load(&o.frees));
    doe_instance_release(p); assert(!atomic_load(&o.frees));
    atomic_store(&o.external, 0); doe_instance_release(p); final(&o);
}
static void failures_flags(void) {
    Observation o = {0}; o.fail_object = 1; doe_instance_config c = config(&o); doe_instance *p = NULL;
    unsigned inits = atomic_load(&queue_inits);
    assert(doe_instance_create(&c, &p) == DOE_INSTANCE_OUT_OF_MEMORY && !p);
    assert(!o.observed && !atomic_load(&o.frees) && atomic_load(&queue_inits) == inits);
    o.fail_object = 0; p = create(&o);
    uint64_t a = new_id(p), b = new_id(p), legacy;
    WGPUFutureWaitInfo w[3] = {{{a}, 0x11}, {{0}, 0x22}, {{b}, 0x33}};
    o.fail_wait = 1;
    assert(doe_instance_wait_any(p, 3, w, 0) == WGPUWaitStatus_Error);
    assert(w[0].completed == 0x11 && w[1].completed == 0x22 && w[2].completed == 0x33);
    o.fail_wait = 0;
    assert(doe_instance_wait_any(p, 3, w, 0) == WGPUWaitStatus_Error);
    assert(w[0].completed == 0 && w[1].completed == 0 && w[2].completed == 0x33);
    Observation other = {0}; doe_instance *q = create(&other);
    assert(doe_future_legacy_id(&legacy) == DOE_FUTURE_OK);
    const uint64_t wrong[2] = {new_id(q), legacy};
    for (unsigned i = 0; i < 2; ++i) {
        w[0].completed = 0x11; w[1].completed = 0x22; w[2].completed = 0x33; w[1].future.id = wrong[i];
        assert(doe_instance_wait_any(p, 3, w, 0) == WGPUWaitStatus_Error);
        assert(w[0].completed == 0 && w[1].completed == 0 && w[2].completed == 0x33);
    }
    unsigned waits = atomic_load(&wait_entries);
    w[0].completed = 0x44;
    assert(doe_instance_wait_any(p, SIZE_MAX / sizeof(doe_future_wait_info) + 1, w, 0) == WGPUWaitStatus_Error);
    assert(w[0].completed == 0x44 && waits == atomic_load(&wait_entries));
    doe_instance_release(q); final(&other); doe_instance_release(p); final(&o);
}

typedef struct Event Event;
typedef struct Trace { unsigned rows[32], count; unsigned freed; } Trace;
struct Event {
    doe_instance *instance; doe_future_completion *completion; void *storage;
    uint64_t id; Trace *trace; unsigned label; int drop_caller, self_wait, recurse;
    Event **follower;
};
static Event *event_create(doe_instance *, unsigned, unsigned, Trace *);
static void publish(Event *e) {
    doe_instance_queue_lease l = doe_instance_borrow_queue(e->instance);
    doe_future_completion *p = e->completion;
    l.retain(l.userdata);
    assert(doe_future_mark_ready(p) == DOE_FUTURE_OK);
    l.release(l.userdata);
}
static void *other_wait(void *userdata) {
    Event *e = userdata; WGPUFutureWaitInfo w = {{e->id}, 0x44};
    assert(doe_instance_wait_any(e->instance, 1, &w, 0) == WGPUWaitStatus_TimedOut && w.completed == 0);
    return NULL;
}
static void dispatch(doe_future_completion *completion, void *userdata) {
    Event *e = userdata; doe_instance *p = e->instance; Trace *trace = e->trace;
    assert(completion == e->completion && trace->count < 32);
    trace->rows[trace->count++] = e->label;
    if (e->self_wait) {
        WGPUFutureWaitInfo w = {{e->id}, 0x44};
        assert(doe_instance_wait_any(p, 1, &w, UINT64_MAX) == WGPUWaitStatus_Success && w.completed == 1);
        pthread_t other; assert(pthread_create(&other, NULL, other_wait, e) == 0);
        assert(pthread_join(other, NULL) == 0);
    }
    if (e->follower) {
        *e->follower = event_create(p, 2, 4, trace); publish(*e->follower);
        if (e->recurse) { doe_instance_process_events(p); trace->rows[trace->count++] = 9; }
    }
    if (e->drop_caller) doe_instance_release(p);
    assert(doe_future_finish(completion) == DOE_FUTURE_OK);
    assert(doe_future_completion_deinit(completion) == DOE_FUTURE_OK);
    free(e->storage); free(e); ++trace->freed;
    doe_instance_release(p);
}
static Event *event_create(doe_instance *p, unsigned mode, unsigned label, Trace *trace) {
    Event *e = calloc(1, sizeof(*e)); doe_future_storage_requirements r; assert(e);
    assert(doe_future_completion_requirements(1, &r) == DOE_FUTURE_OK);
    assert(posix_memalign(&e->storage, r.alignment < sizeof(void *) ? sizeof(void *) : r.alignment, r.size) == 0);
    e->instance = p; e->id = new_id(p); e->trace = trace; e->label = label;
    assert(doe_future_completion_init(1, e->storage, r.size, (doe_future_mode)mode, dispatch, e, &e->completion) == DOE_FUTURE_OK);
    doe_instance_retain(p);
    assert(doe_future_register(doe_instance_borrow_queue(p).queue, e->completion, e->id) == DOE_FUTURE_OK);
    return e;
}
static void *publisher(void *arg) { publish(arg); return NULL; }
static void modes(void) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        Observation o = {0}; Trace trace = {0}; doe_instance *p = create(&o);
        Event *e = event_create(p, mode, mode + 1, &trace); uint64_t id = e->id;
        WGPUFutureWaitInfo w[2] = {{{id}, 0x11}, {{id}, 0x22}};
        assert(doe_instance_wait_any(p, 2, w, 0) == WGPUWaitStatus_TimedOut && !w[0].completed && !w[1].completed);
        pthread_t t; assert(pthread_create(&t, NULL, publisher, e) == 0); assert(pthread_join(t, NULL) == 0);
        assert(trace.count == (unsigned)(mode == 0 || mode == 3));
        doe_instance_process_events(p); assert(trace.count == (unsigned)(mode != 1));
        assert(doe_instance_wait_any(p, 2, w, UINT64_MAX) == WGPUWaitStatus_Success && w[0].completed == 1 && w[1].completed == 1);
        assert(doe_instance_wait_any(p, 2, w, 0) == WGPUWaitStatus_Success && trace.count == 1 && trace.freed == 1);
        doe_instance_release(p); final(&o);
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        Observation o = {0}; Trace trace = {0}; doe_instance *p = create(&o);
        Event *e = event_create(p, mode, 1, &trace); uint64_t id = e->id;
        e->drop_caller = 1; e->self_wait = 1;
        pthread_t worker; assert(pthread_create(&worker, NULL, publisher, e) == 0);
        assert(pthread_join(worker, NULL) == 0);
        if (mode == 1) { WGPUFutureWaitInfo w = {{id}, 0x11}; assert(doe_instance_wait_any(p, 1, &w, 0) == WGPUWaitStatus_Success && w.completed == 1); }
        if (mode == 2) doe_instance_process_events(p);
        assert(trace.count == 1 && trace.freed == 1); final(&o);
    }
}
static void ordering(void) {
    for (unsigned recursive = 0; recursive < 2; ++recursive) {
        Observation o = {0}; Trace trace = {0}; doe_instance *p = create(&o);
        Event *a = event_create(p, 2, 1, &trace), *b = event_create(p, 2, 2, &trace), *c = event_create(p, 2, 3, &trace), *d = NULL;
        a->follower = &d; a->recurse = (int)recursive;
        publish(c); publish(a); publish(b); doe_instance_process_events(p);
        if (recursive) { const unsigned expected[5] = {1,2,3,4,9}; assert(trace.count == 5 && !memcmp(trace.rows, expected, sizeof(expected))); }
        else { assert(trace.count == 3 && trace.rows[0] == 1 && trace.rows[1] == 2 && trace.rows[2] == 3); doe_instance_process_events(p); assert(trace.count == 4 && trace.rows[3] == 4); }
        assert(trace.freed == 4); doe_instance_release(p); final(&o);
    }
}
static void *retain_worker(void *arg) {
    doe_instance *p = arg;
    for (unsigned i = 0; i < 10003; ++i) { doe_instance_retain(p); doe_instance_release(p); }
    return NULL;
}
static void contention(void) {
    Observation o = {0}; doe_instance *p = create(&o); pthread_t threads[8];
    for (unsigned i = 0; i < 8; ++i) assert(pthread_create(&threads[i], NULL, retain_worker, p) == 0);
    for (unsigned i = 0; i < 8; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(!atomic_load(&o.frees)); doe_instance_release(p); final(&o);
}
static void layout(void) {
    override_alignment = 1; requirement_alignment = 4096;
    Observation o = {0}; doe_instance *p = create(&o);
    assert(o.alignment == 4096 && (uintptr_t)doe_instance_borrow_queue(p).queue % 4096 == 0);
    doe_instance_release(p); final(&o); requirement_alignment = 0;
    const size_t aligns[3] = {0, 3, 4096};
    for (unsigned i = 0; i < 3; ++i) {
        Observation f = {0}; doe_instance_config c = config(&f); p = (doe_instance *)&f;
        requirement_alignment = aligns[i]; requirement_size = i == 2 ? SIZE_MAX : 0;
        assert(doe_instance_create(&c, &p) == DOE_INSTANCE_LAYOUT_OVERFLOW && !p);
        assert(!atomic_load(&f.allocations) && !f.observed);
    }
    override_alignment = 0; requirement_alignment = 0; requirement_size = 0;
}
static void capabilities(void) {
    WGPUSupportedInstanceFeatures f = doe_instance_features();
    assert(f.featureCount == 0 && !f.features);
    WGPUInstanceLimits l = doe_instance_limits();
    assert(!l.nextInChain && l.timedWaitAnyMaxCount == 0);
    const uint32_t values[] = {0,1,2,3,0x7fffffff,0xffffffff};
    for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i) assert(!doe_instance_has_feature((WGPUInstanceFeatureName)values[i]));
    doe_instance_features_free_members(f);
    WGPUInstanceFeatureName stack_feature = (WGPUInstanceFeatureName)1;
    f.featureCount = 1; f.features = &stack_feature; doe_instance_features_free_members(f);
}
int main(void) {
    validation(); lifecycle(); guard(); failures_flags(); modes(); ordering(); contention(); layout(); capabilities();
    assert(wait_records == NULL);
    puts("PASS independent public instance cases C01-C05/C09-C14/C16-C18 (named partial coverage; separate faults/native complete remaining obligations)");
    return 0;
}
