/* SPDX-License-Identifier: MIT
 * Developer synchronization boundaries; injected failures precede the real call.
 */
#include "doe_labels.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_mutex_destroy(pthread_mutex_t *);
int __real_pthread_mutex_lock(pthread_mutex_t *);
int __real_pthread_mutex_unlock(pthread_mutex_t *);
int __wrap_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __wrap_pthread_mutex_destroy(pthread_mutex_t *);
int __wrap_pthread_mutex_lock(pthread_mutex_t *);
int __wrap_pthread_mutex_unlock(pthread_mutex_t *);

static int fail_init;
static int fail_destroy;
static int fail_lock;
static int fail_unlock;
static size_t live_allocations;
static size_t lock_calls;
static size_t destroy_calls;

int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    return fail_init ? EAGAIN : __real_pthread_mutex_init(mutex, attr);
}
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex) {
    ++destroy_calls;
    return fail_destroy ? EBUSY : __real_pthread_mutex_destroy(mutex);
}
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) {
    ++lock_calls;
    return fail_lock ? EINVAL : __real_pthread_mutex_lock(mutex);
}
int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex) {
    return fail_unlock ? EPERM : __real_pthread_mutex_unlock(mutex);
}

static void *allocate(void *state, const void *vtable, size_t size, size_t alignment) {
    (void)state;
    (void)vtable;
    (void)alignment;
    void *memory = malloc(size);
    if (memory != NULL) ++live_allocations;
    return memory;
}
static void deallocate(void *state, const void *vtable, void *memory,
                       size_t size, size_t alignment) {
    (void)state;
    (void)vtable;
    (void)size;
    (void)alignment;
    assert(live_allocations != 0);
    --live_allocations;
    free(memory);
}

static doe_label_config config = {DOE_LABELS_ABI_VERSION,
    {NULL, NULL, allocate, deallocate}};

static void cleanup_retry(void) {
    doe_label_store *store = NULL;
    fail_init = 1;
    assert(doe_label_store_create(&config, &store) == DOE_LABEL_SYNCHRONIZATION_INIT);
    assert(store == NULL && live_allocations == 0 && destroy_calls == 0);
    fail_init = 0;
    assert(doe_label_store_create(&config, &store) == DOE_LABEL_OK);
    unsigned char value = 33;
    assert(doe_label_store_set(store, (void *)1, &value, 1) == DOE_LABEL_OK);
    size_t live_before = live_allocations;
    size_t locks_before = lock_calls;
    fail_destroy = 1;
    assert(doe_label_store_destroy(store) == DOE_LABEL_CLEANUP_REQUIRED);
    assert(live_allocations == live_before);
    assert(doe_label_store_set(store, NULL, &value, 1) == DOE_LABEL_CLEANUP_REQUIRED);
    assert(doe_label_store_set(store, (void *)1, NULL, SIZE_MAX) == DOE_LABEL_CLEANUP_REQUIRED);
    assert(doe_label_store_set(store, (void *)1, &value, 0) == DOE_LABEL_CLEANUP_REQUIRED);
    assert(doe_label_store_remove(store, NULL) == DOE_LABEL_CLEANUP_REQUIRED);
    doe_label_view view = {&value, 81};
    assert(doe_label_store_get(store, NULL, &view) == DOE_LABEL_CLEANUP_REQUIRED);
    assert(view.data == &value && view.length == 81);
    assert(lock_calls == locks_before && live_allocations == live_before);
    fail_destroy = 0;
    assert(doe_label_store_destroy(store) == DOE_LABEL_OK);
    assert(live_allocations == 0 && destroy_calls == 2);
}

static void fatal_errors(void) {
    for (int fault = 0; fault < 2; ++fault) {
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            doe_label_store *store = NULL;
            assert(doe_label_store_create(&config, &store) == DOE_LABEL_OK);
            fail_lock = fault == 0;
            fail_unlock = fault == 1;
            unsigned char value = 1;
            (void)doe_label_store_set(store, (void *)1, &value, 1);
            _exit(71);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    }
}

int main(void) {
    cleanup_retry();
    fatal_errors();
    return 0;
}
