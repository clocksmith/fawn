/* SPDX-License-Identifier: MIT
 * Developer synchronization checks. Caller joins before final destruction. */
#define _POSIX_C_SOURCE 200809L
#include "doe_error_scopes.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

struct shared {
    doe_error_scope_stack *stack;
    pthread_barrier_t start;
    atomic_size_t calls;
};

static void receive(uint32_t type, WGPUStringView message, void *first, void *second)
{
    struct shared *shared = first;
    assert(second == &shared->start);
    assert(type == 2 && message.length == 0 && message.data != NULL);
    atomic_fetch_add(&shared->calls, 1);
}

static void *deliver(void *data)
{
    struct shared *shared = data;
    const int status = pthread_barrier_wait(&shared->start);
    assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
    for (size_t i = 0; i < 1000; ++i)
        assert(doe_error_scope_deliver(shared->stack, 2, NULL, 0).status == DOE_ERROR_SCOPE_OK);
    return NULL;
}

int main(void)
{
    doe_error_scope_requirements requirements;
    assert(doe_error_scope_storage_requirements(1, &requirements).status == DOE_ERROR_SCOPE_OK);
    void *storage = malloc(requirements.size);
    assert(storage != NULL);
    struct shared shared = {.calls = 0};
    assert(doe_error_scope_init(1, storage, requirements.size, &shared.stack).status == DOE_ERROR_SCOPE_OK);
    assert(pthread_barrier_init(&shared.start, NULL, 4) == 0);
    assert(doe_error_scope_set_handler(shared.stack, receive, &shared, &shared.start).status == DOE_ERROR_SCOPE_OK);
    pthread_t threads[4];
    for (size_t i = 0; i < 4; ++i) assert(pthread_create(&threads[i], NULL, deliver, &shared) == 0);
    for (size_t i = 0; i < 4; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(atomic_load(&shared.calls) == 4000);
    assert(pthread_barrier_destroy(&shared.start) == 0);
    assert(doe_error_scope_deinit(shared.stack).status == DOE_ERROR_SCOPE_OK);
    free(storage);
    puts("developer concurrency: four synchronized workers, 4000 same-stack callbacks, joined cleanup passed");
    return 0;
}
