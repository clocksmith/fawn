/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _POSIX_C_SOURCE 200809L
#include "doe_futures.h"
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct signal_state {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    bool reached;
    bool release;
};
static struct signal_state *watch_wait;
int __real_pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int __wrap_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    /* A real condition handshake proves the library waiter holds its mutex and
     * is entering wait. Publisher cannot take that mutex until atomic release. */
    if (watch_wait != NULL && cond != &watch_wait->changed) {
        assert(pthread_mutex_lock(&watch_wait->mutex) == 0);
        watch_wait->reached = true;
        assert(pthread_cond_broadcast(&watch_wait->changed) == 0);
        assert(pthread_mutex_unlock(&watch_wait->mutex) == 0);
    }
    return __real_pthread_cond_wait(cond, mutex);
}
static void init_signal(struct signal_state *signal)
{
    *signal = (struct signal_state){0};
    assert(pthread_mutex_init(&signal->mutex, NULL) == 0);
    assert(pthread_cond_init(&signal->changed, NULL) == 0);
}
static void dispose_signal(struct signal_state *signal)
{
    assert(pthread_cond_destroy(&signal->changed) == 0);
    assert(pthread_mutex_destroy(&signal->mutex) == 0);
}
static void wait_reached(struct signal_state *signal)
{
    assert(pthread_mutex_lock(&signal->mutex) == 0);
    while (!signal->reached)
        assert(pthread_cond_wait(&signal->changed, &signal->mutex) == 0);
    assert(pthread_mutex_unlock(&signal->mutex) == 0);
}
static doe_future_queue *new_queue(void)
{
    doe_future_storage_requirements req;
    doe_future_queue *queue;
    assert(doe_future_queue_requirements(1, &req) == DOE_FUTURE_OK);
    void *storage = malloc(req.size);
    assert(storage != NULL);
    assert(doe_future_queue_init(1, storage, req.size, &queue) == DOE_FUTURE_OK);
    return queue;
}
struct node {
    doe_future_queue *queue;
    doe_future_completion *completion;
    struct signal_state *hold;
    uint64_t id;
    unsigned count;
};
static void deliver(doe_future_completion *completion, void *userdata)
{
    struct node *node = userdata;
    doe_future_wait_info self = {node->id, 0};
    assert(doe_future_wait_any(node->queue, &self, 1, UINT64_MAX) == DOE_FUTURE_OK);
    assert(self.completed == 1);
    if (node->hold != NULL) {
        struct signal_state *signal = node->hold;
        assert(pthread_mutex_lock(&signal->mutex) == 0);
        signal->reached = true;
        assert(pthread_cond_broadcast(&signal->changed) == 0);
        while (!signal->release)
            assert(pthread_cond_wait(&signal->changed, &signal->mutex) == 0);
        assert(pthread_mutex_unlock(&signal->mutex) == 0);
    }
    node->count++;
    assert(doe_future_finish(completion) == DOE_FUTURE_OK);
    assert(doe_future_completion_deinit(completion) == DOE_FUTURE_OK);
    free(completion);
}
static void init_node(struct node *node, doe_future_queue *queue,
                      struct signal_state *hold)
{
    doe_future_storage_requirements req;
    *node = (struct node){.queue=queue, .hold=hold};
    assert(doe_future_completion_requirements(1, &req) == DOE_FUTURE_OK);
    void *storage = malloc(req.size);
    assert(storage != NULL);
    assert(doe_future_completion_init(1, storage, req.size,
        DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS, deliver, node, &node->completion) == DOE_FUTURE_OK);
    assert(doe_future_new_id(queue, &node->id) == DOE_FUTURE_OK);
    assert(doe_future_register(queue, node->completion, node->id) == DOE_FUTURE_OK);
}
static void *wait_thread(void *userdata)
{
    struct node *node = userdata;
    doe_future_wait_info info = {node->id, 0};
    assert(doe_future_wait_any(node->queue, &info, 1, UINT64_MAX) == DOE_FUTURE_OK);
    assert(info.completed == 1);
    return NULL;
}
static void *pump_thread(void *userdata)
{
    assert(doe_future_process_events(userdata) == DOE_FUTURE_OK);
    return NULL;
}
static void infinite_wait(void)
{
    struct signal_state signal;
    struct node node;
    pthread_t worker;
    doe_future_queue *queue = new_queue();
    init_signal(&signal);
    init_node(&node, queue, NULL);
    watch_wait = &signal;
    assert(pthread_create(&worker, NULL, wait_thread, &node) == 0);
    wait_reached(&signal);
    assert(doe_future_mark_ready(node.completion) == DOE_FUTURE_OK);
    assert(pthread_join(worker, NULL) == 0);
    watch_wait = NULL;
    assert(node.count == 1);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    free(queue);
    dispose_signal(&signal);
}
static void concurrent_pumps(void)
{
    enum { NODE_COUNT = 17 };
    struct signal_state signal;
    struct node nodes[NODE_COUNT];
    pthread_t first, second;
    doe_future_queue *queue = new_queue();
    init_signal(&signal);
    for (unsigned i = 0; i < NODE_COUNT; ++i) {
        init_node(&nodes[i], queue, i == 0 ? &signal : NULL);
        assert(doe_future_mark_ready(nodes[i].completion) == DOE_FUTURE_OK);
    }
    assert(pthread_create(&first, NULL, pump_thread, queue) == 0);
    wait_reached(&signal);
    doe_future_wait_info info = {nodes[0].id, 0};
    assert(doe_future_wait_any(queue, &info, 1, 0) == DOE_FUTURE_TIMED_OUT);
    assert(info.completed == 0);
    assert(pthread_create(&second, NULL, pump_thread, queue) == 0);
    assert(pthread_join(second, NULL) == 0);
    for (unsigned i = 1; i < NODE_COUNT; ++i) assert(nodes[i].count == 1);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_BUSY);
    assert(pthread_mutex_lock(&signal.mutex) == 0);
    signal.release = true;
    assert(pthread_cond_broadcast(&signal.changed) == 0);
    assert(pthread_mutex_unlock(&signal.mutex) == 0);
    assert(pthread_join(first, NULL) == 0);
    for (unsigned i = 0; i < NODE_COUNT; ++i) assert(nodes[i].count == 1);
    assert(doe_future_wait_any(queue, &info, 1, 0) == DOE_FUTURE_OK);
    assert(info.completed == 1);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    free(queue);
    dispose_signal(&signal);
}
int main(void)
{
    infinite_wait();
    for (unsigned repeat = 0; repeat < 20; ++repeat) concurrent_pumps();
    puts("Developer real-condition infinite wait and concurrent pumps passed.");
    return 0;
}
