/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "../src/internal.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static int failure, forbid_free, layout;
static unsigned allocations, frees, observations, removals;
static int fail_once(int boundary)
{
    if (failure != boundary) return 0;
    failure = 0;
    return 1;
}
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes)
{
    return fail_once(1) ? EAGAIN : __real_pthread_mutex_init(mutex, attributes);
}
int __real_pthread_condattr_init(pthread_condattr_t *);
int __wrap_pthread_condattr_init(pthread_condattr_t *attributes)
{
    return fail_once(2) ? EAGAIN : __real_pthread_condattr_init(attributes);
}
int __real_pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
int __wrap_pthread_condattr_setclock(pthread_condattr_t *attributes, clockid_t clock)
{
    return fail_once(3) ? EINVAL : __real_pthread_condattr_setclock(attributes, clock);
}
int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __wrap_pthread_cond_init(pthread_cond_t *condition, const pthread_condattr_t *attributes)
{
    return fail_once(4) ? EAGAIN : __real_pthread_cond_init(condition, attributes);
}
int __real_pthread_condattr_destroy(pthread_condattr_t *);
int __wrap_pthread_condattr_destroy(pthread_condattr_t *attributes)
{
    return fail_once(5) ? EBUSY : __real_pthread_condattr_destroy(attributes);
}
int __real_pthread_cond_destroy(pthread_cond_t *);
int __wrap_pthread_cond_destroy(pthread_cond_t *condition)
{
    return fail_once(6) ? EBUSY : __real_pthread_cond_destroy(condition);
}
int __real_pthread_mutex_destroy(pthread_mutex_t *);
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    return fail_once(7) ? EBUSY : __real_pthread_mutex_destroy(mutex);
}
int __real_pthread_mutex_unlock(pthread_mutex_t *);
int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    return fail_once(8) ? EPERM : __real_pthread_mutex_unlock(mutex);
}
doe_future_status __real_doe_future_queue_requirements(uint32_t, doe_future_storage_requirements *);
doe_future_status __wrap_doe_future_queue_requirements(uint32_t version, doe_future_storage_requirements *out)
{
    doe_future_status status = __real_doe_future_queue_requirements(version, out);
    if (status != DOE_FUTURE_OK) return status;
    if (layout == 1) out->alignment = 64;
    if (layout == 2) out->alignment = 3;
    if (layout == 3) out->size = SIZE_MAX;
    return status;
}
static void *allocate(void *state, const void *vtable, size_t size, size_t alignment)
{
    void *memory = NULL;
    assert(state == NULL && vtable == NULL);
    ++allocations;
    return posix_memalign(&memory, alignment, size) == 0 ? memory : NULL;
}
static void deallocate(void *state, const void *vtable, void *memory, size_t size, size_t alignment)
{
    (void)state; (void)vtable; (void)size; (void)alignment;
    if (forbid_free) _exit(91);
    ++frees;
    free(memory);
}
static void observed(void *userdata, doe_instance *instance, size_t size, size_t alignment)
{
    (void)userdata; (void)instance; (void)size;
    assert(layout != 1 || alignment == 64);
    ++observations;
}
static void removed(void *userdata, doe_instance *instance)
{
    (void)userdata; (void)instance;
    if (forbid_free) _exit(92);
    ++removals;
}
static doe_instance_config configuration(void)
{
    const doe_instance_allocator allocator = {NULL, NULL, allocate, deallocate};
    return (doe_instance_config){.abi_version = 1, .object_allocator = allocator,
        .wait_allocator = allocator, .allocated = observed, .remove_label = removed};
}
static void fatal_case(int scenario)
{
    doe_instance *instance = NULL;
    doe_instance_config config = configuration();
    if (scenario == 5) failure = 5;
    forbid_free = 1;
    assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_OK);
    if (scenario >= 6 && scenario <= 8) failure = scenario;
    if (scenario == 20) {
        doe_instance_retain(instance);
        doe_instance_retain(instance);
        doe_instance_retain(instance);
        doe_instance_retain(instance); /* Private reduced limit, production remains UINT32_MAX. */
    }
    if (scenario == 21) doe_instance_undo_retain(instance);
    if (scenario == 22) {
        /* Only this private C-unit fixture can see control state. No stale pointer. */
        atomic_store(&instance->references, 0);
        doe_instance_retain(instance);
    }
    if (scenario == 23) {
        doe_future_storage_requirements requirements;
        doe_future_completion *completion;
        uint64_t id;
        doe_instance_queue_lease queue = doe_instance_borrow_queue(instance);
        assert(doe_future_completion_requirements(1, &requirements) == DOE_FUTURE_OK);
        assert(posix_memalign((void **)&completion, requirements.alignment, requirements.size) == 0);
        assert(doe_future_completion_init(1, completion, requirements.size, 1, NULL, NULL, &completion) == DOE_FUTURE_OK);
        assert(doe_future_new_id(queue.queue, &id) == DOE_FUTURE_OK);
        assert(doe_future_register(queue.queue, completion, id) == DOE_FUTURE_OK);
    }
    if (scenario == 24) atomic_store(&instance->references, 0);
    doe_instance_release(instance);
    _exit(93);
}
int main(void)
{
    struct rlimit core = {0, 0};
    assert(setrlimit(RLIMIT_CORE, &core) == 0);
    for (int boundary = 1; boundary <= 4; ++boundary) {
        doe_instance *instance = NULL;
        doe_instance_config config = configuration();
        unsigned before = frees;
        unsigned labels = removals;
        failure = boundary;
        assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_QUEUE_INITIALIZATION);
        assert(instance == NULL && frees == before + 1 && labels == removals);
        assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_OK);
        doe_instance_release(instance);
    }
    for (layout = 1; layout <= 3; ++layout) {
        doe_instance *instance;
        doe_instance_config config = configuration();
        unsigned before = allocations;
        if (layout == 1) {
            assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_OK);
            doe_instance_release(instance);
        } else {
            assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_LAYOUT_OVERFLOW);
            assert(instance == NULL && allocations == before);
        }
    }
    layout = 0;
    const int scenarios[] = {5, 6, 7, 8, 20, 21, 22, 23, 24};
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
        pid_t child = fork();
        int status;
        assert(child >= 0);
        if (child == 0) fatal_case(scenarios[i]);
        assert(waitpid(child, &status, 0) == child);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    }
    assert(allocations == frees && observations == allocations);
    puts("instance real platform faults and private count boundaries: passed");
    return 0;
}
