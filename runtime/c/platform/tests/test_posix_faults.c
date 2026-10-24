/* SPDX-License-Identifier: MIT
 * Real GNU linker-boundary failures; never substitutes a Windows ABI. */
#include "doe_platform.h"
#include <assert.h>
#include <stdio.h>

int __real_pthread_mutexattr_settype(pthread_mutexattr_t *, int);
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_mutexattr_destroy(pthread_mutexattr_t *);
int __real_pthread_join(pthread_t, void **);
int __real_pthread_detach(pthread_t);
int __wrap_pthread_mutexattr_settype(pthread_mutexattr_t *, int);
int __wrap_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __wrap_pthread_mutexattr_destroy(pthread_mutexattr_t *);
int __wrap_pthread_join(pthread_t, void **);
int __wrap_pthread_detach(pthread_t);
static bool fail_type, fail_init, fail_attributes, fail_join, fail_detach;

int __wrap_pthread_mutexattr_settype(pthread_mutexattr_t *attributes, int type)
{
    return fail_type ? EINVAL : __real_pthread_mutexattr_settype(attributes, type);
}
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes)
{
    return fail_init ? EAGAIN : __real_pthread_mutex_init(mutex, attributes);
}
int __wrap_pthread_mutexattr_destroy(pthread_mutexattr_t *attributes)
{
    return fail_attributes ? EBUSY : __real_pthread_mutexattr_destroy(attributes);
}
int __wrap_pthread_join(pthread_t thread, void **value)
{
    return fail_join ? EDEADLK : __real_pthread_join(thread, value);
}
int __wrap_pthread_detach(pthread_t thread)
{
    return fail_detach ? EINVAL : __real_pthread_detach(thread);
}

static void partial_mutex(void)
{
    doe_mutex mutex;
    fail_type = true;
    doe_host_result result = doe_mutex_init(&mutex, true);
    assert(result.status == DOE_HOST_SYSTEM_ERROR && result.native_error == EINVAL);
    assert(!mutex.live && mutex.attributes_live);
    fail_type = false;
    fail_attributes = true;
    assert(doe_mutex_destroy(&mutex).status == DOE_HOST_CLEANUP_REQUIRED);
    assert(!mutex.live && mutex.attributes_live);
    fail_attributes = false;
    assert(doe_mutex_destroy(&mutex).status == DOE_HOST_OK);
    assert(!mutex.live && !mutex.attributes_live);
    fail_init = true;
    result = doe_mutex_init(&mutex, true);
    assert(result.status == DOE_HOST_SYSTEM_ERROR && result.native_error == EAGAIN);
    assert(!mutex.live && mutex.attributes_live);
    fail_init = false;
    assert(doe_mutex_destroy(&mutex).status == DOE_HOST_OK);
    fail_attributes = true;
    result = doe_mutex_init(&mutex, true);
    assert(result.status == DOE_HOST_CLEANUP_REQUIRED && result.native_error == EBUSY);
    assert(mutex.live && mutex.attributes_live);
    assert(doe_mutex_destroy(&mutex).status == DOE_HOST_CLEANUP_REQUIRED);
    assert(!mutex.live && mutex.attributes_live);
    fail_attributes = false;
    assert(doe_mutex_destroy(&mutex).status == DOE_HOST_OK);
}

static void empty_worker(void *argument) { (void)argument; }
static void failed_thread_cleanup(void)
{
    doe_thread thread;
    assert(doe_thread_create(&thread, empty_worker, NULL).status == DOE_HOST_OK);
    fail_join = true;
    doe_host_result result = doe_thread_join(&thread);
    assert(result.status == DOE_HOST_SYSTEM_ERROR && result.native_error == EDEADLK && thread.live);
    fail_detach = true;
    result = doe_thread_detach(&thread);
    assert(result.status == DOE_HOST_SYSTEM_ERROR && result.native_error == EINVAL && thread.live);
    fail_join = fail_detach = false;
    assert(doe_thread_join(&thread).status == DOE_HOST_OK && !thread.live);
    /* Real detach ownership is covered by the shared, entry-acknowledged test. */
}
int main(void)
{
    partial_mutex();
    failed_thread_cleanup();
    puts("POSIX primitive fault control passed: partial init and retained cleanup obligations.");
    return 0;
}
