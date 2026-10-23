/* SPDX-License-Identifier: MIT
 * Developer injection at real pthread boundaries, not a replacement owner. */
#include "doe_error_scopes_zig_bridge.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static int init_error;
static int lock_error;
static int unlock_error;
static int destroy_error;
static size_t mutex_calls;
static size_t callbacks;

int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_mutex_lock(pthread_mutex_t *);
int __real_pthread_mutex_unlock(pthread_mutex_t *);
int __real_pthread_mutex_destroy(pthread_mutex_t *);

int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes)
{
    mutex_calls += 1;
    errno = EINTR;
    return init_error != 0 ? init_error : __real_pthread_mutex_init(mutex, attributes);
}

int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    mutex_calls += 1;
    errno = EINTR;
    return lock_error != 0 ? lock_error : __real_pthread_mutex_lock(mutex);
}

int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    mutex_calls += 1;
    errno = EINTR;
    if (unlock_error != 0) return unlock_error;
    return __real_pthread_mutex_unlock(mutex);
}

int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    mutex_calls += 1;
    errno = EINTR;
    return destroy_error != 0 ? destroy_error : __real_pthread_mutex_destroy(mutex);
}

static void check(doe_error_scope_result value, doe_error_scope_status status, int error)
{
    assert(value.status == status);
    assert(value.system_error == error);
    assert(errno == EDOM);
}

static void receive(uint32_t type, WGPUStringView message, void *first, void *second)
{
    assert(type == 2 && message.length == 0 && message.data != NULL);
    assert(first == NULL && second == NULL);
    assert(errno == EDOM);
    callbacks += 1;
    errno = ERANGE;
}

static void preflight(void)
{
    doe_error_scope_requirements requirements = {123, 456};
    errno = EDOM;
    check(doe_error_scope_storage_requirements(0, NULL), DOE_ERROR_SCOPE_INVALID_ARGUMENT, 0);
    check(doe_error_scope_storage_requirements(0, &requirements), DOE_ERROR_SCOPE_UNSUPPORTED_VERSION, 0);
    assert(requirements.size == 123 && requirements.alignment == 456);
    check(doe_error_scope_storage_requirements(1, &requirements), DOE_ERROR_SCOPE_OK, 0);
    unsigned char *storage = malloc(requirements.size + requirements.alignment);
    unsigned char *snapshot = malloc(requirements.size + requirements.alignment);
    assert(storage != NULL && snapshot != NULL);
    memset(storage, 0xA7, requirements.size + requirements.alignment);
    memcpy(snapshot, storage, requirements.size + requirements.alignment);
    doe_error_scope_stack *out = NULL;
    check(doe_error_scope_init(0, storage, 0, NULL), DOE_ERROR_SCOPE_INVALID_ARGUMENT, 0);
    check(doe_error_scope_init(0, NULL, 0, &out), DOE_ERROR_SCOPE_UNSUPPORTED_VERSION, 0);
    check(doe_error_scope_init(1, NULL, 0, &out), DOE_ERROR_SCOPE_INVALID_ARGUMENT, 0);
    check(doe_error_scope_init(1, storage + 1, requirements.size - 1, &out), DOE_ERROR_SCOPE_INSUFFICIENT_STORAGE, 0);
    check(doe_error_scope_init(1, storage + 1, requirements.size, &out), DOE_ERROR_SCOPE_MISALIGNED_STORAGE, 0);
    assert(out == NULL && mutex_calls == 0);
    assert(memcmp(storage, snapshot, requirements.size + requirements.alignment) == 0);
    init_error = EAGAIN;
    check(doe_error_scope_init(1, storage, requirements.size, &out), DOE_ERROR_SCOPE_SYSTEM_ERROR, EAGAIN);
    assert(out == NULL && mutex_calls == 1);
    init_error = 0;
    check(doe_error_scope_init(1, storage, requirements.size, &out), DOE_ERROR_SCOPE_OK, 0);
    check(doe_error_scope_deinit(out), DOE_ERROR_SCOPE_OK, 0);
    free(snapshot);
    free(storage);
}

static void lock_and_cleanup(void)
{
    doe_error_scope_stack stack;
    errno = EDOM;
    check(doe_error_scopes_zig_init(&stack), DOE_ERROR_SCOPE_OK, 0);
    check(doe_error_scope_push(&stack, 1), DOE_ERROR_SCOPE_OK, 0);
    lock_error = EDEADLK;
    check(doe_error_scope_push(&stack, 3), DOE_ERROR_SCOPE_SYSTEM_ERROR, EDEADLK);
    check(doe_error_scope_pop(&stack, receive, NULL, NULL), DOE_ERROR_SCOPE_SYSTEM_ERROR, EDEADLK);
    check(doe_error_scope_deliver(&stack, 2, NULL, 0), DOE_ERROR_SCOPE_SYSTEM_ERROR, EDEADLK);
    check(doe_error_scope_set_handler(&stack, receive, NULL, NULL), DOE_ERROR_SCOPE_SYSTEM_ERROR, EDEADLK);
    assert(stack.depth == 1 && stack.scopes[0].error_type == 1);
    assert(stack.callback == NULL && callbacks == 0);
    lock_error = 0;
    check(doe_error_scope_deliver(&stack, 2, NULL, 0), DOE_ERROR_SCOPE_OK, 0);
    assert(doe_error_scope_pop(&stack, receive, NULL, NULL).status == DOE_ERROR_SCOPE_OK);
    assert(callbacks == 1 && errno == ERANGE);
    errno = EDOM;
    destroy_error = EBUSY;
    check(doe_error_scope_deinit(&stack), DOE_ERROR_SCOPE_CLEANUP_REQUIRED, EBUSY);
    size_t before = mutex_calls;
    check(doe_error_scope_push(&stack, 1), DOE_ERROR_SCOPE_INVALID_STATE, 0);
    check(doe_error_scope_pop(&stack, receive, NULL, NULL), DOE_ERROR_SCOPE_INVALID_STATE, 0);
    check(doe_error_scope_set_handler(&stack, receive, NULL, NULL), DOE_ERROR_SCOPE_INVALID_STATE, 0);
    check(doe_error_scope_deliver(&stack, 2, NULL, 1), DOE_ERROR_SCOPE_INVALID_ARGUMENT, 0);
    check(doe_error_scope_deliver(&stack, 2, (const unsigned char *)1, 1), DOE_ERROR_SCOPE_INVALID_STATE, 0);
    assert(mutex_calls == before && callbacks == 1);
    destroy_error = 0;
    check(doe_error_scope_deinit(&stack), DOE_ERROR_SCOPE_OK, 0);
    before = mutex_calls;
    check(doe_error_scope_deinit(&stack), DOE_ERROR_SCOPE_INVALID_STATE, 0);
    check(doe_error_scope_push(&stack, 0), DOE_ERROR_SCOPE_INVALID_STATE, 0);
    check(doe_error_scope_deliver(NULL, 0, NULL, 1), DOE_ERROR_SCOPE_INVALID_ARGUMENT, 0);
    assert(mutex_calls == before);
    check(doe_error_scopes_zig_init(&stack), DOE_ERROR_SCOPE_OK, 0);
    check(doe_error_scope_deinit(&stack), DOE_ERROR_SCOPE_OK, 0);
}

static void fatal_unlock(unsigned operation)
{
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        const struct rlimit no_core = {0, 0};
        assert(setrlimit(RLIMIT_CORE, &no_core) == 0);
        doe_error_scope_stack stack;
        assert(doe_error_scopes_zig_init(&stack).status == DOE_ERROR_SCOPE_OK);
        assert(doe_error_scope_push(&stack, 1).status == DOE_ERROR_SCOPE_OK);
        unlock_error = EPERM;
        switch (operation) {
        case 0: (void)doe_error_scope_push(&stack, 2); break;
        case 1: (void)doe_error_scope_pop(&stack, NULL, NULL, NULL); break;
        case 2: (void)doe_error_scope_deliver(&stack, 2, NULL, 0); break;
        case 3: (void)doe_error_scope_set_handler(&stack, NULL, NULL, NULL); break;
        default: _exit(99);
        }
        _exit(98);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

int main(void)
{
    preflight();
    lock_and_cleanup();
    for (unsigned operation = 0; operation < 4; ++operation) fatal_unlock(operation);
    puts("developer faults: preflight, init, lock, cleanup retry, errno and four fatal unlocks passed");
    return 0;
}
