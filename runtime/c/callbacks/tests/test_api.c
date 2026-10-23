/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "doe_callbacks.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct observations {
    unsigned allocations, frees, copies, deliveries, destruction, publishers;
    void *allocation;
    size_t size, alignment;
    int fail_allocation;
};
struct instance {
    doe_future_queue *queue;
    void *storage;
    atomic_uint references;
    struct observations *seen;
};
struct reply {
    struct instance *instance;
    struct observations *seen;
    char message[48];
    uint64_t id;
    int release_caller;
};
static const unsigned allocator_identity = 71;
static void retain(void *raw)
{
    struct instance *owner = raw;
    atomic_fetch_add(&owner->references, 1);
}
static void release(void *raw)
{
    struct instance *owner = raw;
    if (atomic_fetch_sub(&owner->references, 1) != 1) return;
    assert(doe_future_queue_deinit(owner->queue) == DOE_FUTURE_OK);
    owner->seen->destruction++;
    free(owner->storage);
    free(owner);
}
static struct instance *create_instance(struct observations *seen)
{
    struct instance *owner = calloc(1, sizeof(*owner));
    doe_future_storage_requirements required;
    assert(owner != NULL);
    assert(doe_future_queue_requirements(1, &required) == DOE_FUTURE_OK);
    assert(posix_memalign(&owner->storage, required.alignment, required.size) == 0);
    assert(doe_future_queue_init(1, owner->storage, required.size, &owner->queue) == DOE_FUTURE_OK);
    atomic_init(&owner->references, 1);
    owner->seen = seen;
    return owner;
}
static void *allocate(void *raw, const void *identity, size_t size, size_t alignment)
{
    struct observations *seen = raw;
    assert(identity == &allocator_identity);
    seen->allocations++;
    if (seen->fail_allocation) return NULL;
    assert(seen->allocation == NULL);
    assert(posix_memalign(&seen->allocation, alignment, size) == 0);
    seen->size = size;
    seen->alignment = alignment;
    return seen->allocation;
}
static void deallocate(void *raw, const void *identity, void *allocation,
                       size_t size, size_t alignment)
{
    struct observations *seen = raw;
    assert(identity == &allocator_identity && seen->allocation == allocation);
    assert(seen->size == size && seen->alignment == alignment);
    assert(seen->deliveries == seen->frees + 1);
    seen->frees++;
    seen->allocation = NULL;
    free(allocation);
}
static void copy_reply(void *destination, const void *source)
{
    const struct reply *original = source;
    original->seen->copies++;
    *(struct reply *)destination = *original;
}
static void deliver(void *raw)
{
    struct reply *reply = raw;
    assert(strcmp(reply->message, "inline diagnostic survives source overwrite") == 0);
    assert((uintptr_t)raw % 64 == 0);
    reply->seen->deliveries++;
    if (reply->id != 0) {
        doe_future_wait_info info = {.id = reply->id, .completed = 0};
        assert(doe_future_wait_any(reply->instance->queue, &info, 1, 0) == DOE_FUTURE_OK);
        assert(info.completed == 1); /* Self-wait does not retire the obligation. */
        assert(doe_future_queue_deinit(reply->instance->queue) == DOE_FUTURE_BUSY);
    }
    if (reply->release_caller) release(reply->instance);
}
static doe_callback_request request_for(struct reply *reply, doe_callback_owner *binding,
                                        unsigned mode)
{
    return (doe_callback_request){
        .abi_version = DOE_CALLBACKS_ABI_VERSION, .mode = mode, .owner = binding,
        .allocator = {.state = reply->seen, .vtable = &allocator_identity,
                      .allocate = allocate, .deallocate = deallocate},
        .source_context = reply, .context_size = sizeof(*reply), .context_alignment = 64,
        .copy = copy_reply, .deliver = deliver
    };
}
static void defer_ready(doe_callback_event *event, void *raw)
{
    doe_callback_event **token = raw;
    *token = event;
}
static void *worker(void *raw)
{
    doe_callback_mark_ready(raw);
    return NULL;
}
static void test_modes(void)
{
    for (unsigned mode = 0; mode != 4; mode++) {
        struct observations seen = {0};
        struct instance *owner = create_instance(&seen);
        doe_callback_owner binding = {.queue = owner->queue, .userdata = owner,
                                      .retain = retain, .release = release};
        struct reply reply = {.instance = owner, .seen = &seen, .release_caller = 1};
        strcpy(reply.message, "inline diagnostic survives source overwrite");
        uint64_t previous;
        assert(doe_future_new_id(owner->queue, &previous) == DOE_FUTURE_OK);
        reply.id = previous + 1;
        doe_callback_request request = request_for(&reply, &binding, mode);
        uint64_t id = 0;
        assert(doe_callback_ready(&request, &id) == DOE_CALLBACK_OK && id != 0);
        memset(reply.message, 'X', sizeof(reply.message));
        memset(&binding, 0, sizeof(binding)); /* Binding was copied, not borrowed. */
        memset(&request.allocator, 0, sizeof(request.allocator));
        if (mode == 1 || mode == 2) {
            assert(seen.deliveries == 0 && seen.frees == 0);
            retain(owner); /* Direct queue caller's independent full-call lease. */
            if (mode == 1) {
                assert(doe_future_process_events(owner->queue) == DOE_FUTURE_OK);
                assert(seen.deliveries == 0);
                doe_future_wait_info infos[2] = {{.id = id}, {.id = id}};
                assert(doe_future_wait_any(owner->queue, infos, 2, 0) == DOE_FUTURE_OK);
                assert(infos[0].completed == 1 && infos[1].completed == 1);
            } else assert(doe_future_process_events(owner->queue) == DOE_FUTURE_OK);
            release(owner);
        }
        assert(seen.allocations == 1 && seen.copies == 1 && seen.deliveries == 1);
        assert(seen.frees == 1 && seen.destruction == 1);
    }
}
static void test_worker(void)
{
    struct observations seen = {0};
    struct instance *owner = create_instance(&seen);
    doe_callback_owner binding = {.queue = owner->queue, .userdata = owner,
                                  .retain = retain, .release = release};
    struct reply reply = {.instance = owner, .seen = &seen, .release_caller = 1};
    strcpy(reply.message, "inline diagnostic survives source overwrite");
    doe_callback_request request = request_for(&reply, &binding, 3);
    doe_callback_event *token = NULL;
    uint64_t id;
    assert(doe_callback_publish(&request, defer_ready, &token, &id) == DOE_CALLBACK_OK);
    assert(seen.deliveries == 0 && atomic_load(&owner->references) == 2);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, worker, token) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(seen.frees == 1 && seen.destruction == 1);
}
static void test_rejections(void)
{
    struct observations seen = {0};
    struct instance *owner = create_instance(&seen);
    doe_callback_owner binding = {.queue = owner->queue, .userdata = owner,
                                  .retain = retain, .release = release};
    struct reply reply = {.instance = owner, .seen = &seen};
    doe_callback_request request = request_for(&reply, &binding, 1);
    uint64_t id = 91, before, after;
    assert(doe_future_new_id(owner->queue, &before) == DOE_FUTURE_OK);
    assert(doe_callback_ready(NULL, &id) == DOE_CALLBACK_INVALID_ARGUMENT);
    request.abi_version = 0; request.mode = 99;
    assert(doe_callback_ready(&request, &id) == DOE_CALLBACK_UNSUPPORTED_VERSION);
    request.abi_version = 1;
    assert(doe_callback_ready(&request, &id) == DOE_CALLBACK_UNSUPPORTED_MODE);
    request.owner = NULL; request.mode = 1; request.deliver = NULL;
    assert(doe_callback_ready(&request, &id) == DOE_CALLBACK_INSTANCE_UNAVAILABLE);
    request.mode = 3;
    assert(doe_callback_ready(&request, &id) == DOE_CALLBACK_INVALID_ARGUMENT);
    request = request_for(&reply, &binding, 1);
    request.context_alignment = 3;
    assert(doe_callback_ready(&request, &id) == DOE_CALLBACK_INVALID_ARGUMENT);
    request.context_alignment = 64; request.context_size = SIZE_MAX;
    assert(doe_callback_ready(&request, &id) == DOE_CALLBACK_LAYOUT_OVERFLOW);
    request.context_size = sizeof(reply); seen.fail_allocation = 1;
    assert(doe_callback_ready(&request, &id) == DOE_CALLBACK_OUT_OF_MEMORY);
    assert(id == 91 && seen.allocations == 1 && seen.copies == 0 && seen.deliveries == 0);
    assert(atomic_load(&owner->references) == 1);
    assert(doe_future_new_id(owner->queue, &after) == DOE_FUTURE_OK && after == before + 1);
    release(owner);
}
struct immediate { unsigned calls; uint64_t inner; int recurse; };
static void immediate_deliver(void *raw)
{
    struct immediate *reply = raw;
    reply->calls++;
    if (reply->recurse) {
        struct immediate inner = {0};
        doe_callback_request request = {.abi_version = 1, .mode = 3,
            .source_context = &inner, .context_alignment = 1, .deliver = immediate_deliver};
        assert(doe_callback_publish(&request, NULL, NULL, &reply->inner) == DOE_CALLBACK_OK);
        assert(inner.calls == 1);
    }
}
static void test_ownerless(void)
{
    for (unsigned mode = 0; mode <= 3; mode += 3) {
        struct immediate original = {.recurse = 1};
        struct immediate local = original;
        doe_callback_request request = {.abi_version = 1, .mode = mode,
            .source_context = &local, .context_size = SIZE_MAX,
            .context_alignment = 1, .deliver = immediate_deliver};
        uint64_t id;
        assert(doe_callback_publish(&request, NULL, NULL, &id) == DOE_CALLBACK_OK);
        assert(local.calls == 1 && original.calls == 0 && id == local.inner + 1);
    }
}
int main(void)
{
    test_rejections(); test_ownerless(); test_modes(); test_worker();
    puts("C callback payload API: validation, transfer, alignment, inline copy, modes, last-owner and worker passed");
    return 0;
}
