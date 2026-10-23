/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "doe_instance.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *fault;
static int in_deinit;
static unsigned allocations, frees, observations, labels;
static int is(const char *name) { return fault && strcmp(fault, name) == 0; }
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __wrap_pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
    if (is("init-mutex")) return EAGAIN;
    return __real_pthread_mutex_init(m, a);
}
int __real_pthread_condattr_init(pthread_condattr_t *);
int __wrap_pthread_condattr_init(pthread_condattr_t *a) {
    if (is("init-attributes")) return ENOMEM;
    return __real_pthread_condattr_init(a);
}
int __real_pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
int __wrap_pthread_condattr_setclock(pthread_condattr_t *a, clockid_t c) {
    if (is("init-clock")) return EINVAL;
    return __real_pthread_condattr_setclock(a, c);
}
int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __wrap_pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) {
    if (is("init-condition") || is("init-cleanup")) return EAGAIN;
    return __real_pthread_cond_init(c, a);
}
int __real_pthread_condattr_destroy(pthread_condattr_t *);
int __wrap_pthread_condattr_destroy(pthread_condattr_t *a) {
    if (is("init-attributes-destroy")) return EBUSY;
    return __real_pthread_condattr_destroy(a);
}
int __real_pthread_mutex_destroy(pthread_mutex_t *);
int __wrap_pthread_mutex_destroy(pthread_mutex_t *m) {
    if (is("init-cleanup") || (in_deinit && is("deinit-mutex"))) return EBUSY;
    return __real_pthread_mutex_destroy(m);
}
int __real_pthread_cond_destroy(pthread_cond_t *);
int __wrap_pthread_cond_destroy(pthread_cond_t *c) {
    if (in_deinit && is("deinit-condition")) return EBUSY;
    return __real_pthread_cond_destroy(c);
}
int __real_pthread_mutex_unlock(pthread_mutex_t *);
int __wrap_pthread_mutex_unlock(pthread_mutex_t *m) {
    if (in_deinit && is("deinit-unlock")) return EINVAL;
    return __real_pthread_mutex_unlock(m);
}
doe_future_status __real_doe_future_queue_deinit(doe_future_queue *);
doe_future_status __wrap_doe_future_queue_deinit(doe_future_queue *q) {
    in_deinit = 1;
    if (is("deinit-busy")) return DOE_FUTURE_BUSY;
    if (is("deinit-cleanup")) return DOE_FUTURE_CLEANUP_REQUIRED;
    if (is("deinit-system")) return DOE_FUTURE_SYSTEM_ERROR;
    doe_future_status s = __real_doe_future_queue_deinit(q);
    in_deinit = 0; return s;
}
static void *allocate(void *state, const void *vtable, size_t n, size_t a) {
    assert(!state && !vtable); void *p = NULL;
    assert(posix_memalign(&p, a < sizeof(void *) ? sizeof(void *) : a, n) == 0);
    ++allocations; puts("ALLOCATE"); return p;
}
static void deallocate(void *state, const void *vtable, void *p, size_t n, size_t a) {
    assert(!state && !vtable && n && a && p); ++frees; puts("FREE"); free(p);
}
static void observe(void *u, doe_instance *p, size_t n, size_t a) {
    assert(!u && p && n && a); ++observations; puts("OBSERVE");
}
static void label(void *u, doe_instance *p) { assert(!u && p); ++labels; puts("LABEL"); }
static void never_dispatch(doe_future_completion *c, void *u) { (void)c; (void)u; abort(); }
int main(int argc, char **argv) {
    assert(argc == 2); setvbuf(stdout, NULL, _IONBF, 0); fault = argv[1];
    doe_instance_config c = {.abi_version = 1, .object_allocator = {NULL,NULL,allocate,deallocate},
        .wait_allocator = {NULL,NULL,allocate,deallocate}, .allocated = observe, .remove_label = label};
    doe_instance *p = NULL;
    doe_instance_status s = doe_instance_create(&c, &p);
    if (is("init-mutex") || is("init-attributes") || is("init-clock") || is("init-condition")) {
        assert(s == DOE_INSTANCE_QUEUE_INITIALIZATION && !p);
        assert(allocations == 1 && frees == 1 && observations == 1 && labels == 0);
        fault = NULL;
        assert(doe_instance_create(&c, &p) == DOE_INSTANCE_OK && p);
        WGPUFutureWaitInfo info = {{0}, 99};
        assert(doe_instance_wait_any(p, 1, &info, 0) == WGPUWaitStatus_Error && info.completed == 0);
        doe_instance_release(p);
        assert(allocations == 3 && frees == 3 && observations == 2 && labels == 1);
        puts("PASS ordinary setup rollback and stateless subsequent create/wait/free"); return 0;
    }
    assert(s == DOE_INSTANCE_OK && p);
    if (is("pending")) {
        doe_future_storage_requirements r; doe_future_completion *completion = NULL;
        assert(doe_future_completion_requirements(1, &r) == DOE_FUTURE_OK);
        void *storage = NULL;
        assert(posix_memalign(&storage, r.alignment < sizeof(void *) ? sizeof(void *) : r.alignment, r.size) == 0);
        assert(doe_future_completion_init(1, storage, r.size, DOE_FUTURE_MODE_WAIT_ANY_ONLY, never_dispatch, NULL, &completion) == DOE_FUTURE_OK);
        doe_instance_queue_lease l = doe_instance_borrow_queue(p); uint64_t id;
        assert(doe_future_new_id(l.queue, &id) == DOE_FUTURE_OK);
        assert(doe_future_register(l.queue, completion, id) == DOE_FUTURE_OK);
    }
    if (is("undo-floor")) doe_instance_undo_retain(p);
    else doe_instance_release(p);
    fputs("UNEXPECTED RETURN\n", stderr); return 77;
}
