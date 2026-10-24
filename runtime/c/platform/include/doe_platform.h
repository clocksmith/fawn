/* SPDX-License-Identifier: MIT */
#ifndef DOE_PLATFORM_H
#define DOE_PLATFORM_H
/* Include before system headers, or select these namespaces in the build. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#if defined(__cplusplus)
static_assert(sizeof(time_t) <= sizeof(uintmax_t) && (time_t)-1 < 0,
    "Monotonic deadlines require signed time_t no wider than uintmax_t");
#else
_Static_assert(sizeof(time_t) <= sizeof(uintmax_t) && (time_t)-1 < 0,
    "Monotonic deadlines require signed time_t no wider than uintmax_t");
#endif

typedef enum doe_host_status {
    DOE_HOST_OK, DOE_HOST_TIMED_OUT, DOE_HOST_SYSTEM_ERROR,
    DOE_HOST_CLEANUP_REQUIRED, DOE_HOST_TIME_OVERFLOW
} doe_host_status;
typedef struct doe_host_result {
    doe_host_status status;
    int native_error; /* Original POSIX/CRT error, or Win32 DWORD bits in int. */
} doe_host_result;
typedef void (*doe_thread_start)(void *);

static inline doe_host_result doe_host_result_make(doe_host_status status, int error)
{
    doe_host_result result = {status, error};
    return result;
}

static inline bool doe_time_valid(const struct timespec *value)
{
    return value && value->tv_sec >= 0 && value->tv_nsec >= 0 && value->tv_nsec < 1000000000L;
}
static inline bool doe_time_reached(const struct timespec *now, const struct timespec *deadline)
{
    return now->tv_sec > deadline->tv_sec ||
        (now->tv_sec == deadline->tv_sec && now->tv_nsec >= deadline->tv_nsec);
}
static inline doe_host_result doe_time_add(const struct timespec *start, uint64_t ns,
    struct timespec *out)
{
    if (!out || !doe_time_valid(start)) return doe_host_result_make(DOE_HOST_SYSTEM_ERROR, EINVAL);
    const uintmax_t maximum = (UINTMAX_C(1) << (sizeof(time_t) * CHAR_BIT - 1)) - 1;
    uint64_t seconds = ns / UINT64_C(1000000000);
    uint64_t nanos = (uint64_t)start->tv_nsec + ns % UINT64_C(1000000000);
    if (nanos >= UINT64_C(1000000000)) { nanos -= UINT64_C(1000000000); ++seconds; }
    if (seconds > maximum || (uintmax_t)start->tv_sec > maximum - seconds)
        return doe_host_result_make(DOE_HOST_TIME_OVERFLOW, 0);
    struct timespec result = {start->tv_sec + (time_t)seconds, (long)nanos};
    *out = result;
    return doe_host_result_make(DOE_HOST_OK, 0);
}

#if defined(_WIN32)
#include "doe_platform_win32.h"
#else
#include "doe_platform_posix.h"
#endif

/* Conditions use monotonic deadlines. Recursive locks must be held at depth1;
 * waits atomically release and reacquire that same lock. Predicate loops and
 * timeout-vs-readiness policy belong to the calling owner. */
static inline doe_host_result doe_condition_wait_until(doe_condition *condition, doe_mutex *mutex,
    const struct timespec *deadline)
{
    struct timespec now;
    doe_host_result result = doe_monotonic_now(&now);
    return result.status == DOE_HOST_OK ? doe_condition_wait_until_at(condition, mutex, deadline, &now) : result;
}
static inline doe_host_result doe_condition_wait_for(doe_condition *condition, doe_mutex *mutex, uint64_t ns)
{
    struct timespec now, deadline;
    doe_host_result result = doe_monotonic_now(&now);
    if (result.status == DOE_HOST_OK) result = doe_time_add(&now, ns, &deadline);
    return result.status == DOE_HOST_OK ? doe_condition_wait_until_at(condition, mutex, &deadline, &now) : result;
}
#endif
