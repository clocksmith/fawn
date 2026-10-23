/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Independent public-contract evaluator; no candidate implementation included.
 */
#ifndef EVALUATOR_SUPPORT_H
#define EVALUATOR_SUPPORT_H
#define _POSIX_C_SOURCE 200809L
#include "doe_futures.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
#define OK(x) CHECK((x) == DOE_FUTURE_OK)
#define POK(x) CHECK((x) == 0)
static inline void *storage_for(doe_future_storage_requirements r) {
    CHECK(r.size > 0 && r.alignment > 0);
    CHECK((r.alignment & (r.alignment - 1)) == 0);
    size_t a = r.alignment < sizeof(void *) ? sizeof(void *) : r.alignment;
    CHECK(r.size <= SIZE_MAX - (a - 1));
    void *p = aligned_alloc(a, (r.size + a - 1) / a * a);
    CHECK(p != NULL);
    return p;
}
typedef struct allocation_entry { void *handle; void *storage; } allocation_entry;
static allocation_entry allocations[256];
static pthread_mutex_t allocation_mutex = PTHREAD_MUTEX_INITIALIZER;
static inline void save_storage(void *handle, void *storage) {
    POK(pthread_mutex_lock(&allocation_mutex));
    size_t i;
    for (i = 0; i < 256 && allocations[i].handle; ++i) {}
    CHECK(i < 256);
    allocations[i] = (allocation_entry){handle, storage};
    POK(pthread_mutex_unlock(&allocation_mutex));
}
static inline void free_storage(void *handle) {
    POK(pthread_mutex_lock(&allocation_mutex));
    size_t i;
    for (i = 0; i < 256 && allocations[i].handle != handle; ++i) {}
    CHECK(i < 256);
    free(allocations[i].storage);
    allocations[i] = (allocation_entry){NULL, NULL};
    POK(pthread_mutex_unlock(&allocation_mutex));
}
static inline doe_future_queue *new_queue(void) {
    doe_future_storage_requirements r;
    OK(doe_future_queue_requirements(DOE_FUTURES_ABI_VERSION, &r));
    void *s = storage_for(r);
    doe_future_queue *q = NULL;
    OK(doe_future_queue_init(DOE_FUTURES_ABI_VERSION, s, r.size, &q));
    save_storage(q, s);
    return q;
}
static inline doe_future_completion *new_completion(doe_future_mode mode,
        doe_future_dispatch dispatch, void *userdata) {
    doe_future_storage_requirements r;
    OK(doe_future_completion_requirements(DOE_FUTURES_ABI_VERSION, &r));
    void *s = storage_for(r);
    doe_future_completion *n = NULL;
    OK(doe_future_completion_init(DOE_FUTURES_ABI_VERSION, s, r.size,
                                  mode, dispatch, userdata, &n));
    save_storage(n, s);
    return n;
}
static inline uint64_t next_id(doe_future_queue *q) {
    uint64_t id = 0;
    OK(doe_future_new_id(q, &id));
    CHECK(id != 0 && (id >> 63) == 0);
    return id;
}
static inline void drop_queue(doe_future_queue *q) {
    OK(doe_future_queue_deinit(q));
    free_storage(q);
}
static inline void drop_completion(doe_future_completion *n) {
    OK(doe_future_completion_deinit(n));
    free_storage(n);
}
static inline doe_future_status poll(doe_future_queue *q, uint64_t id,
                                     uint32_t *completed) {
    doe_future_wait_info info = {id, 99};
    doe_future_status status = doe_future_wait_any(q, &info, 1, 0);
    *completed = info.completed;
    return status;
}
static inline void report(const char *name) { printf("PASS %s\n", name); }
#endif
