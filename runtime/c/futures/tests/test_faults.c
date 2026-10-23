/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _POSIX_C_SOURCE 200809L
#include "doe_futures.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum fault { MUTEX_INIT, ATTR_INIT, ATTR_CLOCK, CONDITION_INIT,
             ATTR_DESTROY, CONDITION_DESTROY, MUTEX_DESTROY, FAULT_COUNT };
static unsigned failures[FAULT_COUNT];
static unsigned mutexes, attributes, conditions;
static int fail(enum fault kind)
{
    if (failures[kind] == 0) return 0;
    failures[kind]--;
    return 1;
}
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_condattr_init(pthread_condattr_t *);
int __real_pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __real_pthread_mutex_destroy(pthread_mutex_t *);
int __real_pthread_condattr_destroy(pthread_condattr_t *);
int __real_pthread_cond_destroy(pthread_cond_t *);
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    if (fail(MUTEX_INIT)) return EAGAIN;
    int error = __real_pthread_mutex_init(mutex, attr);
    if (error == 0) mutexes++;
    return error;
}
int __wrap_pthread_condattr_init(pthread_condattr_t *attr)
{
    if (fail(ATTR_INIT)) return ENOMEM;
    int error = __real_pthread_condattr_init(attr);
    if (error == 0) attributes++;
    return error;
}
int __wrap_pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock)
{
    assert(clock == CLOCK_MONOTONIC);
    if (fail(ATTR_CLOCK)) return EINVAL;
    return __real_pthread_condattr_setclock(attr, clock);
}
int __wrap_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    if (fail(CONDITION_INIT)) return EAGAIN;
    int error = __real_pthread_cond_init(cond, attr);
    if (error == 0) conditions++;
    return error;
}
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    assert(mutexes == 1);
    if (fail(MUTEX_DESTROY)) return EBUSY;
    int error = __real_pthread_mutex_destroy(mutex);
    if (error == 0) mutexes--;
    return error;
}
int __wrap_pthread_condattr_destroy(pthread_condattr_t *attr)
{
    assert(attributes == 1);
    if (fail(ATTR_DESTROY)) return EBUSY;
    int error = __real_pthread_condattr_destroy(attr);
    if (error == 0) attributes--;
    return error;
}
int __wrap_pthread_cond_destroy(pthread_cond_t *cond)
{
    assert(conditions == 1);
    if (fail(CONDITION_DESTROY)) return EBUSY;
    int error = __real_pthread_cond_destroy(cond);
    if (error == 0) conditions--;
    return error;
}

static void clean_reuse(void *storage, size_t size)
{
    doe_future_queue *queue = NULL;
    assert(mutexes == 0 && attributes == 0 && conditions == 0);
    assert(doe_future_queue_init(1, storage, size, &queue) == DOE_FUTURE_OK);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    assert(mutexes == 0 && attributes == 0 && conditions == 0);
}
int main(void)
{
    doe_future_storage_requirements req;
    doe_future_queue *queue = NULL;
    assert(doe_future_queue_requirements(1, &req) == DOE_FUTURE_OK);
    void *storage = malloc(req.size);
    assert(storage != NULL);
    for (enum fault kind = MUTEX_INIT; kind <= CONDITION_INIT; ++kind) {
        failures[kind] = 1;
        assert(doe_future_queue_init(1, storage, req.size, &queue) == DOE_FUTURE_SYSTEM_ERROR);
        assert(queue == NULL);
        clean_reuse(storage, req.size);
    }
    /* Setup failure plus repeated rollback-attribute failure retains a handle. */
    failures[ATTR_CLOCK] = 1; failures[ATTR_DESTROY] = 2;
    assert(doe_future_queue_init(1, storage, req.size, &queue) == DOE_FUTURE_CLEANUP_REQUIRED);
    assert(queue != NULL && attributes == 1 && mutexes == 1 && conditions == 0);
    uint64_t id = 73;
    assert(doe_future_new_id(queue, &id) != DOE_FUTURE_OK && id == 73);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_CLEANUP_REQUIRED);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    clean_reuse(storage, req.size);
    /* Attribute teardown can fail after condition initialization. */
    failures[ATTR_DESTROY] = 2;
    assert(doe_future_queue_init(1, storage, req.size, &queue) == DOE_FUTURE_CLEANUP_REQUIRED);
    assert(conditions == 1 && attributes == 1 && mutexes == 1);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_CLEANUP_REQUIRED);
    assert(conditions == 0 && attributes == 1 && mutexes == 1);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    clean_reuse(storage, req.size);
    /* Rollback mutex failures skip the attribute that already destroyed. */
    failures[CONDITION_INIT] = 1; failures[MUTEX_DESTROY] = 2;
    assert(doe_future_queue_init(1, storage, req.size, &queue) == DOE_FUTURE_CLEANUP_REQUIRED);
    assert(mutexes == 1 && attributes == 0 && conditions == 0);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_CLEANUP_REQUIRED);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    clean_reuse(storage, req.size);
    /* A live queue becomes cleanup-only; destruction retries preserve each bit. */
    assert(doe_future_queue_init(1, storage, req.size, &queue) == DOE_FUTURE_OK);
    failures[CONDITION_DESTROY] = 2;
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_CLEANUP_REQUIRED);
    assert(doe_future_process_events(queue) != DOE_FUTURE_OK);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_CLEANUP_REQUIRED);
    failures[MUTEX_DESTROY] = 2;
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_CLEANUP_REQUIRED);
    assert(conditions == 0 && mutexes == 1);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_CLEANUP_REQUIRED);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    clean_reuse(storage, req.size);
    free(storage);
    puts("Developer setup/cleanup fault injection passed.");
    return 0;
}
