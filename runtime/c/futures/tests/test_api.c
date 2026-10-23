/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _POSIX_C_SOURCE 200809L
#include "doe_futures.h"
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define OK(call) assert((call) == DOE_FUTURE_OK)

static doe_future_queue *new_queue(void)
{
    doe_future_storage_requirements req;
    doe_future_queue *queue = NULL;
    void *storage;
    OK(doe_future_queue_requirements(DOE_FUTURES_ABI_VERSION, &req));
    storage = malloc(req.size);
    assert(storage != NULL && (uintptr_t)storage % req.alignment == 0);
    OK(doe_future_queue_init(DOE_FUTURES_ABI_VERSION, storage, req.size, &queue));
    return queue;
}

static void delete_queue(doe_future_queue *queue)
{
    OK(doe_future_queue_deinit(queue));
    free(queue);
}

struct gate;
struct probe {
    doe_future_queue *queue;
    doe_future_completion *completion;
    uint64_t id;
    unsigned count;
    unsigned tag;
    unsigned *log;
    unsigned *log_count;
    struct probe *follower;
    struct probe *make_ready;
    struct gate *gate;
    bool recursive;
    bool free_on_dispatch;
};

static void init_probe(struct probe *probe, doe_future_queue *queue,
                       doe_future_mode mode, doe_future_dispatch dispatch)
{
    doe_future_storage_requirements req;
    void *storage;
    OK(doe_future_completion_requirements(DOE_FUTURES_ABI_VERSION, &req));
    storage = malloc(req.size);
    assert(storage != NULL && (uintptr_t)storage % req.alignment == 0);
    probe->queue = queue;
    OK(doe_future_completion_init(DOE_FUTURES_ABI_VERSION, storage, req.size,
                                  mode, dispatch, probe, &probe->completion));
    if (queue != NULL) {
        OK(doe_future_new_id(queue, &probe->id));
        OK(doe_future_register(queue, probe->completion, probe->id));
    }
}

static void delete_probe(struct probe *probe)
{
    if (probe->completion == NULL) return;
    OK(doe_future_finish(probe->completion));
    OK(doe_future_completion_deinit(probe->completion));
    free(probe->completion);
    probe->completion = NULL;
}

static void deliver(doe_future_completion *completion, void *userdata)
{
    struct probe *probe = userdata;
    probe->count++;
    if (probe->log != NULL) probe->log[(*probe->log_count)++] = probe->tag;
    if (probe->queue != NULL) {
        doe_future_wait_info self = {probe->id, 0};
        OK(doe_future_wait_any(probe->queue, &self, 1, UINT64_MAX));
        assert(self.completed == 1);
    }
    if (probe->follower != NULL) {
        init_probe(probe->follower, probe->queue,
                   DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, deliver);
        OK(doe_future_mark_ready(probe->follower->completion));
    }
    if (probe->make_ready != NULL)
        OK(doe_future_mark_ready(probe->make_ready->completion));
    if (probe->recursive) OK(doe_future_process_events(probe->queue));
    OK(doe_future_finish(completion));
    if (probe->free_on_dispatch) {
        OK(doe_future_completion_deinit(completion));
        free(completion);
        probe->completion = NULL;
    }
    if (probe->queue != NULL)
        assert(doe_future_queue_deinit(probe->queue) == DOE_FUTURE_BUSY);
}

static void test_storage(void)
{
    doe_future_storage_requirements req = {19, 23};
    doe_future_queue *queue = (void *)1;
    void *memory;
    assert(doe_future_queue_requirements(0, &req) == DOE_FUTURE_UNSUPPORTED_VERSION);
    assert(req.size == 19 && req.alignment == 23);
    OK(doe_future_queue_requirements(1, &req));
    memory = malloc(req.size + req.alignment);
    assert(memory != NULL);
    assert(doe_future_queue_init(0, memory, req.size, &queue) ==
           DOE_FUTURE_UNSUPPORTED_VERSION && queue == NULL);
    assert(doe_future_queue_init(1, memory, req.size - 1, &queue) ==
           DOE_FUTURE_INSUFFICIENT_STORAGE && queue == NULL);
    assert(doe_future_queue_init(1, (unsigned char *)memory + 1, req.size, &queue) ==
           DOE_FUTURE_MISALIGNED_STORAGE && queue == NULL);
    OK(doe_future_queue_init(1, memory, req.size, &queue));
    OK(doe_future_queue_deinit(queue));
    OK(doe_future_queue_init(1, memory, req.size, &queue));
    delete_queue(queue);
}

static void test_id_and_retirement(void)
{
    doe_future_queue *queue = new_queue(), *other = new_queue();
    struct probe first = {0}, second = {0}, duplicate = {0};
    uint64_t issued, legacy;
    init_probe(&first, queue, DOE_FUTURE_MODE_WAIT_ANY_ONLY, NULL);
    init_probe(&second, queue, DOE_FUTURE_MODE_WAIT_ANY_ONLY, NULL);
    init_probe(&duplicate, NULL, DOE_FUTURE_MODE_WAIT_ANY_ONLY, NULL);
    assert(doe_future_register(queue, duplicate.completion, first.id) ==
           DOE_FUTURE_DUPLICATE);
    assert(doe_future_register(queue, first.completion, second.id) ==
           DOE_FUTURE_DUPLICATE);
    assert(doe_future_register(other, duplicate.completion, first.id) ==
           DOE_FUTURE_UNKNOWN_ID);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_BUSY);
    assert(doe_future_completion_deinit(first.completion) == DOE_FUTURE_BUSY);
    doe_future_wait_info info[2] = {{first.id, 9}, {second.id, 9}};
    assert(doe_future_wait_any(queue, info, 2, 0) == DOE_FUTURE_TIMED_OUT);
    assert(info[0].completed == 0 && info[1].completed == 0);
    OK(doe_future_finish(first.completion));
    OK(doe_future_wait_any(queue, info, 2, 0));
    assert(info[0].completed == 1 && info[1].completed == 0);
    assert(doe_future_wait_any(other, info, 1, 0) == DOE_FUTURE_UNKNOWN_ID);
    OK(doe_future_new_id(queue, &issued));
    info[0] = (doe_future_wait_info){issued, 0};
    OK(doe_future_wait_any(queue, info, 1, 0));
    assert(info[0].completed == 1);
    OK(doe_future_legacy_id(&legacy));
    assert(legacy >> 63 == 1 && first.id >> 63 == 0);
    info[0].id = legacy;
    assert(doe_future_wait_any(queue, info, 1, 0) == DOE_FUTURE_UNKNOWN_ID);
    OK(doe_future_wait_any(NULL, NULL, 0, UINT64_MAX));
    delete_probe(&first); delete_probe(&second); delete_probe(&duplicate);
    delete_queue(other); delete_queue(queue);
    queue = new_queue();
    OK(doe_future_new_id(queue, &issued));
    assert(issued != first.id && issued >> 32 != first.id >> 32);
    delete_queue(queue);
}

static void test_modes_and_order(void)
{
    doe_future_queue *queue = new_queue();
    struct probe probes[4] = {{0}};
    unsigned log[4] = {0}, count = 0;
    for (unsigned i = 0; i < 4; ++i) {
        probes[i].tag = i;
        probes[i].log = log; probes[i].log_count = &count;
        probes[i].free_on_dispatch = true;
        init_probe(&probes[i], queue, (doe_future_mode)i, deliver);
    }
    for (unsigned i = 4; i > 0; --i) OK(doe_future_mark_ready(probes[i-1].completion));
    assert(count == 2 && log[0] == 3 && log[1] == 0);
    OK(doe_future_process_events(queue));
    assert(count == 3 && log[2] == 2);
    doe_future_wait_info duplicates[3] = {
        {probes[1].id, 0}, {probes[1].id, 0}, {probes[1].id, 0}};
    OK(doe_future_wait_any(queue, duplicates, 3, 0));
    assert(count == 4 && log[3] == 1);
    for (unsigned i = 0; i < 3; ++i) assert(duplicates[i].completed == 1);
    OK(doe_future_process_events(queue));
    OK(doe_future_wait_any(queue, duplicates, 3, 0));
    for (unsigned i = 0; i < 4; ++i) assert(probes[i].count == 1);
    delete_queue(queue);

    queue = new_queue(); count = 0;
    for (unsigned i = 0; i < 4; ++i) {
        probes[i] = (struct probe){.tag=i, .log=log, .log_count=&count};
        init_probe(&probes[i], queue, DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, deliver);
    }
    for (unsigned i = 4; i > 0; --i) OK(doe_future_mark_ready(probes[i-1].completion));
    OK(doe_future_process_events(queue));
    for (unsigned i = 0; i < 4; ++i) { assert(log[i] == i); delete_probe(&probes[i]); }
    delete_queue(queue);
}

static void test_boundaries(void)
{
    for (unsigned recursive = 0; recursive < 2; ++recursive) {
        doe_future_queue *queue = new_queue();
        struct probe follower = {0}, leader = {.follower=&follower, .recursive=recursive};
        init_probe(&leader, queue, DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, deliver);
        OK(doe_future_mark_ready(leader.completion));
        OK(doe_future_process_events(queue));
        assert(leader.count == 1 && follower.count == recursive);
        OK(doe_future_process_events(queue));
        assert(follower.count == 1);
        delete_probe(&leader); delete_probe(&follower); delete_queue(queue);
    }
    doe_future_queue *queue = new_queue();
    struct probe early = {0}, later = {.make_ready=&early};
    init_probe(&early, queue, DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, deliver);
    init_probe(&later, queue, DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, deliver);
    OK(doe_future_mark_ready(later.completion));
    OK(doe_future_process_events(queue));
    assert(early.count == 1 && later.count == 1);
    delete_probe(&early); delete_probe(&later); delete_queue(queue);
}

struct wait_task { doe_future_queue *queue; uint64_t id; uint64_t timeout;
                   doe_future_status result; };
static void *wait_worker(void *userdata)
{
    struct wait_task *task = userdata;
    doe_future_wait_info info = {task->id, 0};
    task->result = doe_future_wait_any(task->queue, &info, 1, task->timeout);
    return NULL;
}

static void test_manual_and_infinite(void)
{
    doe_future_queue *queue = new_queue();
    struct probe probe = {0};
    struct wait_task task;
    pthread_t thread;
    init_probe(&probe, queue, DOE_FUTURE_MODE_WAIT_ANY_ONLY, NULL);
    OK(doe_future_begin_delivery(probe.completion));
    doe_future_wait_info info = {probe.id, 0};
    OK(doe_future_wait_any(queue, &info, 1, UINT64_MAX));
    task = (struct wait_task){queue, probe.id, 0, DOE_FUTURE_OK};
    assert(pthread_create(&thread, NULL, wait_worker, &task) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(task.result == DOE_FUTURE_TIMED_OUT);
    task.timeout = UINT64_MAX;
    assert(pthread_create(&thread, NULL, wait_worker, &task) == 0);
    OK(doe_future_finish(probe.completion));
    assert(pthread_join(thread, NULL) == 0);
    assert(task.result == DOE_FUTURE_OK);
    delete_probe(&probe); delete_queue(queue);
}

struct gate { pthread_mutex_t mutex; pthread_cond_t changed; bool entered;
              bool release; struct probe *probe; };
static void gated_deliver(doe_future_completion *completion, void *userdata)
{
    struct probe *probe = userdata;
    struct gate *gate = probe->gate;
    OK(doe_future_finish(completion));
    OK(doe_future_completion_deinit(completion));
    free(completion);
    assert(doe_future_queue_deinit(probe->queue) == DOE_FUTURE_BUSY);
    assert(pthread_mutex_lock(&gate->mutex) == 0);
    gate->entered = true;
    assert(pthread_cond_broadcast(&gate->changed) == 0);
    while (!gate->release) assert(pthread_cond_wait(&gate->changed, &gate->mutex) == 0);
    assert(pthread_mutex_unlock(&gate->mutex) == 0);
}
static void *publish_worker(void *userdata)
{
    struct probe *probe = userdata;
    OK(doe_future_mark_ready(probe->completion));
    return NULL;
}
static void test_callback_lifetime(void)
{
    doe_future_queue *queue = new_queue();
    struct probe probe = {0};
    struct gate gate = {0};
    pthread_t thread;
    assert(pthread_mutex_init(&gate.mutex, NULL) == 0);
    assert(pthread_cond_init(&gate.changed, NULL) == 0);
    probe.gate = &gate;
    init_probe(&probe, queue, DOE_FUTURE_MODE_ALLOW_SPONTANEOUS, gated_deliver);
    assert(pthread_create(&thread, NULL, publish_worker, &probe) == 0);
    assert(pthread_mutex_lock(&gate.mutex) == 0);
    while (!gate.entered) assert(pthread_cond_wait(&gate.changed, &gate.mutex) == 0);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_BUSY);
    gate.release = true;
    assert(pthread_cond_broadcast(&gate.changed) == 0);
    assert(pthread_mutex_unlock(&gate.mutex) == 0);
    assert(pthread_join(thread, NULL) == 0);
    delete_queue(queue);
    assert(pthread_cond_destroy(&gate.changed) == 0);
    assert(pthread_mutex_destroy(&gate.mutex) == 0);
}

int main(void)
{
    test_storage(); test_id_and_retirement(); test_modes_and_order();
    test_boundaries(); test_manual_and_infinite(); test_callback_lifetime();
    puts("Developer futures API tests passed (storage, IDs, modes, ordering, reentry, lifetime).");
    return 0;
}
