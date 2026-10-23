/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "doe_instance.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct allocation_state {
    size_t allocations, frees, size, alignment;
    void *memory;
    int fail;
} allocation_state;
typedef struct observations {
    allocation_state object, wait;
    unsigned allocated, removed, guard, callbacks;
} observations;

static void *allocate(void *state, const void *vtable, size_t size, size_t alignment)
{
    allocation_state *allocator = state;
    void *memory = NULL;
    assert(vtable == allocator);
    if (allocator->fail) return NULL;
    if (posix_memalign(&memory, alignment < sizeof(void *) ? sizeof(void *) : alignment, size) != 0)
        return NULL;
    ++allocator->allocations;
    allocator->size = size;
    allocator->alignment = alignment;
    allocator->memory = memory;
    return memory;
}

static void deallocate(void *state, const void *vtable, void *memory, size_t size, size_t alignment)
{
    allocation_state *allocator = state;
    assert(vtable == allocator && memory == allocator->memory);
    assert(size == allocator->size && alignment == allocator->alignment);
    ++allocator->frees;
    free(memory);
}

static void allocated(void *userdata, doe_instance *instance, size_t size, size_t alignment)
{
    observations *seen = userdata;
    assert(instance == seen->object.memory);
    assert(size == seen->object.size && alignment == seen->object.alignment);
    ++seen->allocated;
}

static void remove_label(void *userdata, doe_instance *instance)
{
    observations *seen = userdata;
    assert(instance == seen->object.memory && seen->object.frees == 0);
    ++seen->removed;
}

static uint32_t external_count(void *userdata, doe_instance *instance)
{
    observations *seen = userdata;
    assert(instance == seen->object.memory);
    return seen->guard;
}

static doe_instance_config config_for(observations *seen)
{
    return (doe_instance_config){
        .abi_version = DOE_INSTANCE_ABI_VERSION,
        .object_allocator = {&seen->object, &seen->object, allocate, deallocate},
        .wait_allocator = {&seen->wait, &seen->wait, allocate, deallocate},
        .hook_userdata = seen, .allocated = allocated,
        .external_texture_count = external_count, .remove_label = remove_label
    };
}

typedef struct payload {
    doe_future_completion *completion;
    doe_instance *instance;
    observations *seen;
} payload;

static void deliver(doe_future_completion *completion, void *userdata)
{
    payload *event = userdata;
    doe_instance *instance = event->instance;
    observations *seen = event->seen;
    ++seen->callbacks;
    assert(doe_future_finish(completion) == DOE_FUTURE_OK);
    assert(doe_future_completion_deinit(completion) == DOE_FUTURE_OK);
    free(event->completion);
    free(event);
    doe_instance_release(instance); /* Actual event lease. */
    doe_instance_release(instance); /* Last caller reference. */
    assert(seen->removed == 0); /* Readiness/wait/pump still owns its full call. */
}

static void test_delivery(unsigned mode)
{
    observations seen = {0};
    doe_instance_config config = config_for(&seen);
    doe_instance *instance = NULL;
    doe_future_storage_requirements requirements;
    doe_instance_queue_lease view;
    payload *event = malloc(sizeof(*event));
    uint64_t id;
    assert(event != NULL);
    assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_OK);
    assert(doe_instance_from_handle(instance) == instance);
    view = doe_instance_borrow_queue(instance);
    assert(view.userdata == instance);
    assert(doe_future_completion_requirements(1, &requirements) == DOE_FUTURE_OK);
    assert(posix_memalign((void **)&event->completion, requirements.alignment, requirements.size) == 0);
    event->instance = instance;
    event->seen = &seen;
    assert(doe_future_completion_init(1, event->completion, requirements.size,
        (doe_future_mode)mode, deliver, event, &event->completion) == DOE_FUTURE_OK);
    doe_instance_retain(instance);
    assert(doe_future_new_id(view.queue, &id) == DOE_FUTURE_OK);
    assert(doe_future_register(view.queue, event->completion, id) == DOE_FUTURE_OK);
    view.retain(view.userdata); /* Standalone direct readiness has its own lease. */
    assert(doe_future_mark_ready(event->completion) == DOE_FUTURE_OK);
    view.release(view.userdata);
    if (mode == 1) {
        WGPUFutureWaitInfo infos[2] = {{{id}, 7}, {{id}, 9}};
        assert(seen.callbacks == 0);
        doe_instance_process_events(instance);
        assert(seen.callbacks == 0);
        assert(doe_instance_wait_any(instance, 2, infos, 0) == WGPUWaitStatus_Success);
        assert(infos[0].completed == 1 && infos[1].completed == 1);
        assert(seen.wait.allocations == 1 && seen.wait.frees == 1);
    } else if (mode == 2) {
        assert(seen.callbacks == 0);
        doe_instance_process_events(instance);
    }
    assert(seen.callbacks == 1 && seen.allocated == 1 && seen.removed == 1);
    assert(seen.object.allocations == 1 && seen.object.frees == 1);
}

int main(void)
{
    observations seen = {0};
    doe_instance_config config = config_for(&seen);
    doe_instance *instance = (doe_instance *)(uintptr_t)1;
    WGPUFutureWaitInfo info = {{0}, 71};
    uint32_t wrong = 0;
    assert(doe_instance_create(NULL, NULL) == DOE_INSTANCE_INVALID_ARGUMENT);
    assert(doe_instance_create(NULL, &instance) == DOE_INSTANCE_INVALID_ARGUMENT && instance == NULL);
    config.abi_version = 99;
    assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_UNSUPPORTED_VERSION);
    config.abi_version = 1;
    seen.object.fail = 1;
    assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_OUT_OF_MEMORY);
    assert(seen.allocated == 0 && instance == NULL);
    seen.object.fail = 0;
    assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_OK);
    memset(&config, 0, sizeof(config)); /* The owner copied all bindings. */
    assert(doe_instance_from_handle(&wrong) == NULL && doe_instance_from_handle(NULL) == NULL);
    assert(doe_instance_wait_any(NULL, 0, NULL, UINT64_MAX) == WGPUWaitStatus_Success);
    assert(doe_instance_wait_any(NULL, 1, &info, 0) == WGPUWaitStatus_Error);
    assert(doe_instance_wait_any(instance, SIZE_MAX, &info, 0) == WGPUWaitStatus_Error);
    assert(info.completed == 71);
    seen.wait.fail = 1;
    assert(doe_instance_wait_any(instance, 1, &info, 0) == WGPUWaitStatus_Error);
    assert(info.completed == 71);
    seen.guard = 1;
    doe_instance_retain(instance);
    doe_instance_undo_retain(instance);
    doe_instance_release(instance); /* Guard preserves final original owner. */
    assert(seen.removed == 0);
    doe_instance_retain(instance);
    doe_instance_release(instance); /* Guard decrements only above one. */
    assert(seen.removed == 0);
    seen.guard = 0;
    doe_instance_release(instance);
    assert(seen.removed == 1 && seen.object.frees == 1);
    assert(doe_instance_features().featureCount == 0 && doe_instance_features().features == NULL);
    assert(doe_instance_limits().nextInChain == NULL && doe_instance_limits().timedWaitAnyMaxCount == 0);
    assert(!doe_instance_has_feature((WGPUInstanceFeatureName)UINT32_MAX));
    doe_instance_features_free_members(doe_instance_features());
    doe_instance_retain(NULL);
    doe_instance_release(NULL);
    doe_instance_undo_retain(NULL);
    doe_instance_process_events(NULL);
    for (unsigned mode = 0; mode < 4; ++mode) test_delivery(mode);
    puts("instance C API: passed");
    return 0;
}
