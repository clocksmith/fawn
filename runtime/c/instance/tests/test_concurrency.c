/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "doe_instance.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic unsigned frees;
static void *allocate(void *state, const void *vtable, size_t size, size_t alignment)
{
    void *memory = NULL;
    assert(state == NULL && vtable == NULL);
    return posix_memalign(&memory, alignment, size) == 0 ? memory : NULL;
}
static void deallocate(void *state, const void *vtable, void *memory, size_t size, size_t alignment)
{
    (void)state; (void)vtable; (void)size; (void)alignment;
    atomic_fetch_add(&frees, 1);
    free(memory);
}
static void *worker(void *raw)
{
    doe_instance *instance = raw;
    for (unsigned i = 0; i < 50000; ++i) {
        doe_instance_retain(instance);
        doe_instance_release(instance);
    }
    return NULL;
}
int main(void)
{
    const doe_instance_allocator allocator = {NULL, NULL, allocate, deallocate};
    const doe_instance_config config = {.abi_version = 1, .object_allocator = allocator, .wait_allocator = allocator};
    doe_instance *instance;
    pthread_t threads[4];
    assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_OK);
    for (size_t i = 0; i < 4; ++i) assert(pthread_create(&threads[i], NULL, worker, instance) == 0);
    for (size_t i = 0; i < 4; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(atomic_load(&frees) == 0);
    doe_instance_release(instance);
    assert(atomic_load(&frees) == 1);
    puts("instance count contention: passed");
    return 0;
}
