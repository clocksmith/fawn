/* SPDX-License-Identifier: MIT
 * Link wrappers inject statuses at the real owner boundary, not a second queue. */
#define _POSIX_C_SOURCE 200809L
#include "doe_callbacks.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static const char *fault;
doe_future_status __real_doe_future_new_id(doe_future_queue *, uint64_t *);
doe_future_status __real_doe_future_register(doe_future_queue *, doe_future_completion *, uint64_t);
doe_future_status __real_doe_future_mark_ready(doe_future_completion *);
doe_future_status __real_doe_future_finish(doe_future_completion *);
doe_future_status __real_doe_future_completion_deinit(doe_future_completion *);
doe_future_status __wrap_doe_future_new_id(doe_future_queue *queue, uint64_t *id)
{
    if (strcmp(fault, "identity") == 0) return DOE_FUTURE_EXHAUSTED;
    return __real_doe_future_new_id(queue, id);
}
doe_future_status __wrap_doe_future_register(doe_future_queue *queue, doe_future_completion *node, uint64_t id)
{
    if (strcmp(fault, "register") == 0) return DOE_FUTURE_SYSTEM_ERROR;
    return __real_doe_future_register(queue, node, id);
}
doe_future_status __wrap_doe_future_mark_ready(doe_future_completion *node)
{
    doe_future_status status = __real_doe_future_mark_ready(node);
    if (strcmp(fault, "ready-after") == 0) return DOE_FUTURE_SYSTEM_ERROR;
    return status;
}
doe_future_status __wrap_doe_future_finish(doe_future_completion *node)
{
    doe_future_status status = __real_doe_future_finish(node);
    if (strcmp(fault, "finish-after") == 0) return DOE_FUTURE_SYSTEM_ERROR;
    return status;
}
doe_future_status __wrap_doe_future_completion_deinit(doe_future_completion *node)
{
    if (strcmp(fault, "deinit") == 0) return DOE_FUTURE_INVALID_STATE;
    return __real_doe_future_completion_deinit(node);
}
static void *allocate(void *state, const void *vtable, size_t size, size_t alignment)
{
    (void)state; (void)vtable;
    void *allocation = NULL;
    assert(posix_memalign(&allocation, alignment, size) == 0);
    return allocation;
}
static void deallocate(void *state, const void *vtable, void *allocation, size_t size, size_t alignment)
{
    (void)state; (void)vtable; (void)size; (void)alignment;
    free(allocation);
}
static void lease(void *raw) { (void)raw; }
static void copy(void *to, const void *from) { *(unsigned *)to = *(const unsigned *)from; }
static void deliver(void *raw) { assert(*(unsigned *)raw == 7); }
int main(int argc, char **argv)
{
    assert(argc == 2); fault = argv[1];
    doe_future_storage_requirements required;
    doe_future_queue *queue = NULL;
    assert(doe_future_queue_requirements(1, &required) == DOE_FUTURE_OK);
    void *storage = allocate(NULL, NULL, required.size, required.alignment);
    assert(doe_future_queue_init(1, storage, required.size, &queue) == DOE_FUTURE_OK);
    doe_callback_owner owner = {.queue = queue, .retain = lease, .release = lease};
    unsigned source = 7;
    doe_callback_request request = {.abi_version = 1, .mode = 3, .owner = &owner,
        .allocator = {.allocate = allocate, .deallocate = deallocate},
        .source_context = &source, .context_size = sizeof(source),
        .context_alignment = _Alignof(unsigned), .copy = copy, .deliver = deliver};
    uint64_t id;
    (void)doe_callback_ready(&request, &id);
    abort(); /* Every admitted fault must terminate inside its named operation. */
}
