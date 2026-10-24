/* SPDX-License-Identifier: MIT */
#ifndef DOE_PLATFORM_WIN32_H
#define DOE_PLATFORM_WIN32_H
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#include <string.h>
#if defined(__cplusplus)
static_assert(sizeof(int) == sizeof(DWORD), "Native error bits require 32-bit int");
#else
_Static_assert(sizeof(int) == sizeof(DWORD), "Native error bits require 32-bit int");
#endif

typedef struct doe_mutex {
    union { SRWLOCK plain; CRITICAL_SECTION recursive; } native;
    bool live, recursive;
} doe_mutex;
typedef struct doe_condition { CONDITION_VARIABLE native; bool live; } doe_condition;
#define DOE_MUTEX_STATIC_INITIALIZER { .native = { .plain = SRWLOCK_INIT }, .live = true }
typedef DWORD doe_thread_id;
typedef struct doe_thread {
    HANDLE native;
    doe_thread_id identity;
    doe_thread_start start;
    void *argument;
    bool live;
} doe_thread;

static inline int doe_win32_error_bits(DWORD code)
{
    int bits;
    /* Preserve every bit without implementation-defined unsigned narrowing. */
    memcpy(&bits, &code, sizeof(bits));
    return bits;
}
static inline doe_host_result doe_win32_failure(DWORD code)
{
    return doe_host_result_make(DOE_HOST_SYSTEM_ERROR, doe_win32_error_bits(code));
}
static inline doe_host_result doe_mutex_init(doe_mutex *mutex, bool recursive)
{
    memset(mutex, 0, sizeof(*mutex));
    mutex->recursive = recursive;
    if (recursive) {
        if (!InitializeCriticalSectionEx(&mutex->native.recursive, 0, 0)) return doe_win32_failure(GetLastError());
    } else InitializeSRWLock(&mutex->native.plain);
    mutex->live = true;
    return doe_host_result_make(DOE_HOST_OK, 0);
}
static inline doe_host_result doe_mutex_destroy(doe_mutex *mutex)
{
    if (mutex->live && mutex->recursive) DeleteCriticalSection(&mutex->native.recursive);
    mutex->live = false;
    return doe_host_result_make(DOE_HOST_OK, 0);
}
static inline doe_host_result doe_mutex_lock(doe_mutex *mutex)
{
    if (mutex->recursive) EnterCriticalSection(&mutex->native.recursive);
    else AcquireSRWLockExclusive(&mutex->native.plain);
    return doe_host_result_make(DOE_HOST_OK, 0);
}
static inline doe_host_result doe_mutex_unlock(doe_mutex *mutex)
{
    if (mutex->recursive) LeaveCriticalSection(&mutex->native.recursive);
    else ReleaseSRWLockExclusive(&mutex->native.plain);
    return doe_host_result_make(DOE_HOST_OK, 0);
}
static inline doe_host_result doe_condition_init(doe_condition *condition)
{
    InitializeConditionVariable(&condition->native);
    condition->live = true;
    return doe_host_result_make(DOE_HOST_OK, 0);
}
static inline doe_host_result doe_condition_destroy(doe_condition *condition)
{
    /* Win32 CV/SRW storage owns no kernel handle. Exclusion remains required. */
    condition->live = false;
    return doe_host_result_make(DOE_HOST_OK, 0);
}
static inline doe_host_result doe_win32_condition_wait(doe_condition *condition, doe_mutex *mutex, DWORD ms)
{
    BOOL completed = mutex->recursive ?
        SleepConditionVariableCS(&condition->native, &mutex->native.recursive, ms) :
        SleepConditionVariableSRW(&condition->native, &mutex->native.plain, ms, 0);
    if (completed) return doe_host_result_make(DOE_HOST_OK, 0);
    DWORD error = GetLastError();
    return doe_host_result_make(error == ERROR_TIMEOUT ? DOE_HOST_TIMED_OUT : DOE_HOST_SYSTEM_ERROR,
        doe_win32_error_bits(error));
}
static inline doe_host_result doe_condition_wait(doe_condition *condition, doe_mutex *mutex)
{
    return doe_win32_condition_wait(condition, mutex, INFINITE);
}
static inline doe_host_result doe_condition_signal(doe_condition *condition)
{
    WakeConditionVariable(&condition->native);
    return doe_host_result_make(DOE_HOST_OK, 0);
}
static inline doe_host_result doe_condition_broadcast(doe_condition *condition)
{
    WakeAllConditionVariable(&condition->native);
    return doe_host_result_make(DOE_HOST_OK, 0);
}
/* floor(remainder*1e9/frequency), using integers only. QPC inputs are positive
 * signed64, so doubled residues and each added remainder fit unsigned64. */
static inline uint64_t doe_win32_fraction(uint64_t remainder, uint64_t frequency)
{
    if (remainder <= UINT64_MAX / UINT64_C(1000000000)) return remainder * UINT64_C(1000000000) / frequency;
    uint64_t quotient = 0, residue = 0;
    for (uint32_t mask = UINT32_C(1) << 29; mask; mask >>= 1) {
        quotient *= 2;
        residue *= 2;
        if (residue >= frequency) { residue -= frequency; ++quotient; }
        if (UINT32_C(1000000000) & mask) {
            residue += remainder;
            if (residue >= frequency) { residue -= frequency; ++quotient; }
        }
    }
    return quotient;
}
static inline doe_host_result doe_monotonic_now(struct timespec *out)
{
    LARGE_INTEGER ticks, rate;
    if (!QueryPerformanceFrequency(&rate) || !QueryPerformanceCounter(&ticks)) return doe_win32_failure(GetLastError());
    if (ticks.QuadPart < 0 || rate.QuadPart <= 0) return doe_win32_failure(ERROR_INVALID_DATA);
    uint64_t frequency = (uint64_t)rate.QuadPart, count = (uint64_t)ticks.QuadPart;
    uint64_t seconds = count / frequency;
    const uintmax_t maximum = (UINTMAX_C(1) << (sizeof(time_t) * CHAR_BIT - 1)) - 1;
    if (seconds > maximum) return doe_host_result_make(DOE_HOST_TIME_OVERFLOW, 0);
    struct timespec result = {(time_t)seconds, (long)doe_win32_fraction(count % frequency, frequency)};
    *out = result;
    return doe_host_result_make(DOE_HOST_OK, 0);
}
static inline doe_host_result doe_condition_wait_until_at(doe_condition *condition, doe_mutex *mutex,
    const struct timespec *deadline, const struct timespec *observed)
{
    if (!doe_time_valid(deadline) || !doe_time_valid(observed)) return doe_win32_failure(ERROR_INVALID_PARAMETER);
    struct timespec now = *observed;
    for (;;) {
        if (doe_time_reached(&now, deadline))
            return doe_host_result_make(DOE_HOST_TIMED_OUT, doe_win32_error_bits(ERROR_TIMEOUT));
        uint64_t seconds = (uint64_t)(deadline->tv_sec - now.tv_sec);
        long nanos = deadline->tv_nsec - now.tv_nsec;
        if (nanos < 0) { --seconds; nanos += 1000000000L; }
        /* Finite spans never become INFINITE or truncate to an early timeout. */
        uint64_t ms = seconds > (MAXDWORD - 1u) / 1000u ? MAXDWORD - 1u :
            seconds * 1000u + (uint64_t)nanos / 1000000u + ((uint64_t)nanos % 1000000u != 0u);
        if (ms >= MAXDWORD) ms = MAXDWORD - 1u;
        doe_host_result result = doe_win32_condition_wait(condition, mutex, (DWORD)ms);
        if (result.status != DOE_HOST_TIMED_OUT) return result;
        result = doe_monotonic_now(&now);
        if (result.status != DOE_HOST_OK) return result;
    }
}
static inline doe_host_result doe_sleep_for(uint64_t ns)
{
    uint64_t ms = ns / UINT64_C(1000000) + (ns % UINT64_C(1000000) != 0);
    while (ms) {
        DWORD chunk = ms >= MAXDWORD ? MAXDWORD - 1u : (DWORD)ms;
        Sleep(chunk);
        ms -= chunk;
    }
    return doe_host_result_make(DOE_HOST_OK, 0);
}
static inline doe_thread_id doe_thread_current(void) { return GetCurrentThreadId(); }
static inline bool doe_thread_equal(doe_thread_id a, doe_thread_id b) { return a == b; }
static inline doe_thread_id doe_thread_identity(const doe_thread *thread) { return thread->identity; }
static inline unsigned __stdcall doe_win32_thread_start(void *storage)
{
    doe_thread *thread = (doe_thread *)storage;
    doe_thread_start start = thread->start;
    void *argument = thread->argument;
    start(argument);
    return 0;
}
static inline doe_host_result doe_thread_create(doe_thread *thread, doe_thread_start start, void *argument)
{
    if (!start) return doe_host_result_make(DOE_HOST_SYSTEM_ERROR, EINVAL);
    thread->start = start;
    thread->argument = argument;
    thread->live = false;
    unsigned identity = 0;
    uintptr_t handle = _beginthreadex(NULL, 0, doe_win32_thread_start, thread, 0, &identity);
    if (!handle) return doe_host_result_make(DOE_HOST_SYSTEM_ERROR, errno);
    thread->native = (HANDLE)handle;
    thread->identity = identity;
    thread->live = true;
    return doe_host_result_make(DOE_HOST_OK, 0);
}
static inline doe_host_result doe_thread_join(doe_thread *thread)
{
    if (!thread->live) return doe_win32_failure(ERROR_INVALID_HANDLE);
    DWORD waited = WaitForSingleObject(thread->native, INFINITE);
    if (waited != WAIT_OBJECT_0) return doe_win32_failure(GetLastError());
    if (!CloseHandle(thread->native)) return doe_win32_failure(GetLastError());
    thread->live = false;
    thread->native = NULL;
    return doe_host_result_make(DOE_HOST_OK, 0);
}
static inline doe_host_result doe_thread_detach(doe_thread *thread)
{
    if (!thread->live) return doe_win32_failure(ERROR_INVALID_HANDLE);
    if (!CloseHandle(thread->native)) return doe_win32_failure(GetLastError());
    thread->live = false;
    thread->native = NULL;
    return doe_host_result_make(DOE_HOST_OK, 0);
}
#endif
