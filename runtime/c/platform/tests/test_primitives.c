/* SPDX-License-Identifier: MIT
 * CPU control shared by actual POSIX and Windows SDK builds. */
#include "doe_platform.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); abort(); } } while (0)
#define OK(call) REQUIRE((call).status == DOE_HOST_OK)

typedef struct control {
    doe_mutex mutex;
    doe_condition changed;
    doe_thread thread;
    bool entered, release, done;
    unsigned value;
} control;

static struct timespec deadline_after(uint64_t ns)
{
    struct timespec now, deadline;
    OK(doe_monotonic_now(&now));
    OK(doe_time_add(&now, ns, &deadline));
    return deadline;
}

static void await_flag(control *state, const bool *flag)
{
    const struct timespec deadline = deadline_after(UINT64_C(3000000000));
    while (!*flag) OK(doe_condition_wait_until(&state->changed, &state->mutex, &deadline));
}

static void worker(void *argument)
{
    control *state = argument;
    OK(doe_mutex_lock(&state->mutex));
    REQUIRE(doe_thread_equal(doe_thread_current(), doe_thread_identity(&state->thread)));
    REQUIRE(!state->entered && state->value == 41);
    state->entered = true;
    OK(doe_condition_signal(&state->changed));
    await_flag(state, &state->release);
    state->value += 1;
    state->done = true;
    OK(doe_condition_broadcast(&state->changed));
    /* No state access after unlock: a detached controller may now destroy it. */
    OK(doe_mutex_unlock(&state->mutex));
}

static void transfer(bool recursive, bool detach)
{
    control state = {0};
    OK(doe_mutex_init(&state.mutex, recursive));
    OK(doe_condition_init(&state.changed));
    OK(doe_mutex_lock(&state.mutex));
    if (recursive) {
        OK(doe_mutex_lock(&state.mutex));
        OK(doe_mutex_unlock(&state.mutex));
    }
    /* Exactly depth1 at all waits; excludes entry until handle publication. */
    state.value = 41;
    OK(doe_thread_create(&state.thread, worker, &state));
    REQUIRE(state.thread.live);
    await_flag(&state, &state.entered);
    REQUIRE(state.value == 41 && !state.done);
    if (detach) {
        /* Entry acknowledges trampoline copied its borrowed startup record. */
        OK(doe_thread_detach(&state.thread));
        REQUIRE(!state.thread.live);
        REQUIRE(doe_thread_detach(&state.thread).status == DOE_HOST_SYSTEM_ERROR);
    }
#if defined(_WIN32)
    if (!detach) {
        /* Real SDK failure: retain a duplicate for recovery while probing an
         * invalid stored handle. Neither failed operation consumes its lease. */
        HANDLE saved = NULL;
        REQUIRE(DuplicateHandle(GetCurrentProcess(), state.thread.native,
            GetCurrentProcess(), &saved, 0, FALSE, DUPLICATE_SAME_ACCESS));
        REQUIRE(CloseHandle(state.thread.native));
        state.thread.native = NULL;
        REQUIRE(doe_thread_join(&state.thread).status == DOE_HOST_SYSTEM_ERROR);
        REQUIRE(state.thread.live);
        REQUIRE(doe_thread_detach(&state.thread).status == DOE_HOST_SYSTEM_ERROR);
        REQUIRE(state.thread.live);
        state.thread.native = saved;
    }
#endif
    state.release = true;
    OK(doe_condition_broadcast(&state.changed));
    await_flag(&state, &state.done);
    REQUIRE(state.value == 42);
    OK(doe_mutex_unlock(&state.mutex));
    if (!detach) OK(doe_thread_join(&state.thread));
    REQUIRE(!state.thread.live);
    REQUIRE(doe_thread_join(&state.thread).status == DOE_HOST_SYSTEM_ERROR);
    OK(doe_condition_destroy(&state.changed));
    OK(doe_mutex_destroy(&state.mutex));
}

static void deadlines(void)
{
    doe_mutex mutex;
    doe_condition condition;
    OK(doe_mutex_init(&mutex, false));
    OK(doe_condition_init(&condition));
    OK(doe_mutex_lock(&mutex));
    const struct timespec deadline = deadline_after(UINT64_C(1000000));
    doe_host_result waited;
    /* Spurious success cannot turn a finite timeout into completion. */
    do { waited = doe_condition_wait_until(&condition, &mutex, &deadline); }
    while (waited.status == DOE_HOST_OK);
    REQUIRE(waited.status == DOE_HOST_TIMED_OUT);
    struct timespec observed;
    OK(doe_monotonic_now(&observed));
    REQUIRE(doe_time_reached(&observed, &deadline));
    const struct timespec invalid = {0, 1000000000L};
    REQUIRE(doe_condition_wait_until_at(&condition, &mutex, &invalid, &observed).status == DOE_HOST_SYSTEM_ERROR);
    REQUIRE(doe_condition_wait_for(&condition, &mutex, 0).status == DOE_HOST_TIMED_OUT);
    OK(doe_mutex_unlock(&mutex));
    OK(doe_condition_destroy(&condition));
    OK(doe_mutex_destroy(&mutex));
    const time_t maximum = (time_t)((UINTMAX_C(1) << (sizeof(time_t) * CHAR_BIT - 1)) - 1);
    const struct timespec start = {maximum, 999999999L};
    struct timespec untouched = {17, 23};
    REQUIRE(doe_time_add(&start, 1, &untouched).status == DOE_HOST_TIME_OVERFLOW);
    REQUIRE(untouched.tv_sec == 17 && untouched.tv_nsec == 23);
    REQUIRE(doe_time_add(&invalid, 1, &untouched).status == DOE_HOST_SYSTEM_ERROR);
    REQUIRE(untouched.tv_sec == 17 && untouched.tv_nsec == 23);
    const struct timespec carry = {3, 999999999L};
    OK(doe_time_add(&carry, UINT64_C(1000000001), &untouched));
    REQUIRE(untouched.tv_sec == 5 && untouched.tv_nsec == 0);
    doe_thread empty = {0};
    REQUIRE(doe_thread_create(&empty, NULL, NULL).status == DOE_HOST_SYSTEM_ERROR);
    REQUIRE(!empty.live);
    OK(doe_sleep_for(1));
#if defined(_WIN32)
    REQUIRE(doe_win32_fraction(UINT64_C(9000000000000000000), UINT64_C(9000000000000000001)) == UINT64_C(999999999));
    REQUIRE(doe_win32_fraction(UINT64_C(4500000000000000000), UINT64_C(9000000000000000000)) == UINT64_C(500000000));
#endif
}

int main(void)
{
    deadlines();
    transfer(false, false);
    transfer(true, false);
    transfer(false, true);
    transfer(true, true);
    puts("Host primitive CPU control passed: deadlines, signal ordering, recursive depth1 wait, join/detach ownership.");
    return 0;
}
