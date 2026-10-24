/* SPDX-License-Identifier: MIT */
#ifndef DOE_PLATFORM_POSIX_H
#define DOE_PLATFORM_POSIX_H
#include <pthread.h>
#if defined(__APPLE__) || defined(DOE_PLATFORM_TEST_DARWIN_RELATIVE)
#define DOE_HOST_RELATIVE_CONDITION 1
#else
#define DOE_HOST_RELATIVE_CONDITION 0
#endif
#if defined(DOE_PLATFORM_TEST_DARWIN_RELATIVE)
int pthread_cond_timedwait_relative_np(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
#endif

typedef struct doe_mutex {
    pthread_mutex_t native;
    pthread_mutexattr_t attributes;
    bool live, attributes_live;
} doe_mutex;
typedef struct doe_condition {
    pthread_cond_t native;
    pthread_condattr_t attributes;
    bool live, attributes_live;
} doe_condition;
/* C11 static owners use the native constant initializer, with no init race. */
#define DOE_MUTEX_STATIC_INITIALIZER { .native = PTHREAD_MUTEX_INITIALIZER, .live = true }
typedef pthread_t doe_thread_id;
typedef struct doe_thread {
    pthread_t native;
    doe_thread_start start;
    void *argument;
    bool live;
} doe_thread;

static inline doe_host_result doe_posix_result(int error)
{
    return doe_host_result_make(error ? DOE_HOST_SYSTEM_ERROR : DOE_HOST_OK, error);
}
static inline doe_host_result doe_mutex_init(doe_mutex *mutex, bool recursive)
{
    memset(mutex, 0, sizeof(*mutex));
    int error;
    if (recursive) {
        error = pthread_mutexattr_init(&mutex->attributes);
        if (error) return doe_posix_result(error);
        mutex->attributes_live = true;
        error = pthread_mutexattr_settype(&mutex->attributes, PTHREAD_MUTEX_RECURSIVE);
        if (error) return doe_posix_result(error);
    }
    error = pthread_mutex_init(&mutex->native, recursive ? &mutex->attributes : NULL);
    if (error) return doe_posix_result(error);
    mutex->live = true;
    if (mutex->attributes_live) {
        error = pthread_mutexattr_destroy(&mutex->attributes);
        if (error) return doe_host_result_make(DOE_HOST_CLEANUP_REQUIRED, error);
        mutex->attributes_live = false;
    }
    return doe_posix_result(0);
}
static inline doe_host_result doe_mutex_destroy(doe_mutex *mutex)
{
    if (mutex->live) {
        int error = pthread_mutex_destroy(&mutex->native);
        if (error) return doe_host_result_make(DOE_HOST_CLEANUP_REQUIRED, error);
        mutex->live = false;
    }
    if (mutex->attributes_live) {
        int error = pthread_mutexattr_destroy(&mutex->attributes);
        if (error) return doe_host_result_make(DOE_HOST_CLEANUP_REQUIRED, error);
        mutex->attributes_live = false;
    }
    return doe_posix_result(0);
}
static inline doe_host_result doe_mutex_lock(doe_mutex *mutex) { return doe_posix_result(pthread_mutex_lock(&mutex->native)); }
static inline doe_host_result doe_mutex_unlock(doe_mutex *mutex) { return doe_posix_result(pthread_mutex_unlock(&mutex->native)); }

static inline doe_host_result doe_condition_init(doe_condition *condition)
{
    memset(condition, 0, sizeof(*condition));
    int error = pthread_condattr_init(&condition->attributes);
    if (error) return doe_posix_result(error);
    condition->attributes_live = true;
#if !DOE_HOST_RELATIVE_CONDITION
    error = pthread_condattr_setclock(&condition->attributes, CLOCK_MONOTONIC);
    if (error) return doe_posix_result(error);
#endif
    error = pthread_cond_init(&condition->native, &condition->attributes);
    if (error) return doe_posix_result(error);
    condition->live = true;
    error = pthread_condattr_destroy(&condition->attributes);
    if (error) return doe_host_result_make(DOE_HOST_CLEANUP_REQUIRED, error);
    condition->attributes_live = false;
    return doe_posix_result(0);
}
static inline doe_host_result doe_condition_destroy(doe_condition *condition)
{
    if (condition->live) {
        int error = pthread_cond_destroy(&condition->native);
        if (error) return doe_host_result_make(DOE_HOST_CLEANUP_REQUIRED, error);
        condition->live = false;
    }
    if (condition->attributes_live) {
        int error = pthread_condattr_destroy(&condition->attributes);
        if (error) return doe_host_result_make(DOE_HOST_CLEANUP_REQUIRED, error);
        condition->attributes_live = false;
    }
    return doe_posix_result(0);
}
static inline doe_host_result doe_condition_wait(doe_condition *condition, doe_mutex *mutex)
{
    return doe_posix_result(pthread_cond_wait(&condition->native, &mutex->native));
}
static inline doe_host_result doe_condition_signal(doe_condition *condition) { return doe_posix_result(pthread_cond_signal(&condition->native)); }
static inline doe_host_result doe_condition_broadcast(doe_condition *condition) { return doe_posix_result(pthread_cond_broadcast(&condition->native)); }
static inline doe_host_result doe_monotonic_now(struct timespec *out)
{
    if (clock_gettime(CLOCK_MONOTONIC, out)) return doe_posix_result(errno);
    return doe_time_valid(out) ? doe_posix_result(0) : doe_posix_result(EINVAL);
}
static inline doe_host_result doe_condition_wait_until_at(doe_condition *condition, doe_mutex *mutex,
    const struct timespec *deadline, const struct timespec *now)
{
    if (!doe_time_valid(deadline) || !doe_time_valid(now)) return doe_posix_result(EINVAL);
    if (doe_time_reached(now, deadline)) return doe_host_result_make(DOE_HOST_TIMED_OUT, ETIMEDOUT);
    int error;
#if DOE_HOST_RELATIVE_CONDITION
    struct timespec remaining = {deadline->tv_sec - now->tv_sec, deadline->tv_nsec - now->tv_nsec};
    if (remaining.tv_nsec < 0) { --remaining.tv_sec; remaining.tv_nsec += 1000000000L; }
    error = pthread_cond_timedwait_relative_np(&condition->native, &mutex->native, &remaining);
#else
    (void)now;
    error = pthread_cond_timedwait(&condition->native, &mutex->native, deadline);
#endif
    return doe_host_result_make(error == ETIMEDOUT ? DOE_HOST_TIMED_OUT :
        error ? DOE_HOST_SYSTEM_ERROR : DOE_HOST_OK, error);
}
static inline doe_host_result doe_sleep_for(uint64_t ns)
{
    const struct timespec zero = {0, 0};
    struct timespec delay;
    doe_host_result result = doe_time_add(&zero, ns, &delay);
    if (result.status != DOE_HOST_OK) return result;
    while (nanosleep(&delay, &delay)) if (errno != EINTR) return doe_posix_result(errno);
    return doe_posix_result(0);
}
static inline doe_thread_id doe_thread_current(void) { return pthread_self(); }
static inline bool doe_thread_equal(doe_thread_id a, doe_thread_id b) { return pthread_equal(a, b) != 0; }
static inline doe_thread_id doe_thread_identity(const doe_thread *thread) { return thread->native; }
static inline void *doe_posix_thread_start(void *storage)
{
    doe_thread *thread = (doe_thread *)storage;
    doe_thread_start start = thread->start;
    void *argument = thread->argument;
    start(argument);
    return NULL;
}
static inline doe_host_result doe_thread_create(doe_thread *thread, doe_thread_start start, void *argument)
{
    if (!start) return doe_host_result_make(DOE_HOST_SYSTEM_ERROR, EINVAL);
    thread->start = start;
    thread->argument = argument;
    thread->live = false;
    int error = pthread_create(&thread->native, NULL, doe_posix_thread_start, thread);
    if (!error) thread->live = true;
    return doe_posix_result(error);
}
static inline doe_host_result doe_thread_join(doe_thread *thread)
{
    if (!thread->live) return doe_posix_result(EINVAL);
    int error = pthread_join(thread->native, NULL);
    if (!error) thread->live = false;
    return doe_posix_result(error);
}
static inline doe_host_result doe_thread_detach(doe_thread *thread)
{
    if (!thread->live) return doe_posix_result(EINVAL);
    int error = pthread_detach(thread->native);
    if (!error) thread->live = false;
    return doe_posix_result(error);
}
#endif
