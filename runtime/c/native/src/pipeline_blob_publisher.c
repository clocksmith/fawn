/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "pipeline_blob_publisher.h"
#include "doe_vk_cache.h"
#include <stdlib.h>
#include <string.h>

struct native_blob_job {
    native_blob_job *next;
    uint64_t cache;
    uint8_t key[32];
};

static void must(doe_host_result result)
{
    if (result.status != DOE_HOST_OK) abort();
}

void native_blob_publish_now(native_device *device, uint64_t cache, const uint8_t key[32])
{
    doe_vk_context *context = device->backend.value.vulkan.context;
    uint8_t *bytes = NULL;
    size_t size = 0;
    const bool serialized = doe_vk_pipeline_blob_serialize(context, cache, &bytes, &size) == DOE_VK_OK;
    native_lock(device);
    doe_vk_pipeline_blob_counted(context, serialized);
    DoeNativePipelineBlobStore store = device->pipeline_blobs;
    native_unlock(device);
    if (serialized && store.store) store.store(store.userdata, key, 32, bytes, size);
    free(bytes);
}

static void work(void *argument)
{
    native_device *device = argument;
    native_blob_publisher *publisher = &device->blob_publisher;
    must(doe_mutex_lock(&publisher->mutex));
    for (;;) {
        while (!publisher->queued && !publisher->stopping)
            must(doe_condition_wait(&publisher->changed, &publisher->mutex));
        native_blob_job *job = publisher->queued;
        if (!job) break; /* Stopping with nothing left. */
        publisher->queued = job->next;
        if (!publisher->queued) publisher->last = NULL;
        must(doe_mutex_unlock(&publisher->mutex));
        native_blob_publish_now(device, job->cache, job->key);
        free(job);
        must(doe_mutex_lock(&publisher->mutex));
        --publisher->outstanding;
        must(doe_condition_broadcast(&publisher->changed)); /* Drainers wait for zero. */
    }
    must(doe_mutex_unlock(&publisher->mutex));
}

bool native_blob_publisher_defer(native_device *device, uint64_t cache, const uint8_t key[32])
{
    native_blob_publisher *publisher = &device->blob_publisher;
    native_blob_job *job = calloc(1, sizeof(*job));
    if (!job) return false;
    job->cache = cache;
    memcpy(job->key, key, sizeof(job->key));
    /* The device mutex orders this with destruction: a cache is either queued
     * before finish stops the worker or not taken at all. */
    native_lock(device);
    bool taken = device->backend.kind == WGPUBackendType_Vulkan && !device->destroyed &&
        device->pipeline_blobs.store != NULL;
    if (taken && !publisher->initialized) {
        if (doe_mutex_init(&publisher->mutex, false).status != DOE_HOST_OK) taken = false;
        else if (doe_condition_init(&publisher->changed).status != DOE_HOST_OK) {
            must(doe_mutex_destroy(&publisher->mutex));
            taken = false;
        } else publisher->initialized = true;
    }
    if (taken && !publisher->started) {
        publisher->started = doe_thread_create(&publisher->thread, work, device).status == DOE_HOST_OK;
        taken = publisher->started;
    }
    if (taken) {
        must(doe_mutex_lock(&publisher->mutex));
        taken = !publisher->stopping && publisher->outstanding < NATIVE_BLOB_PUBLISHER_LIMIT;
        if (taken) {
            if (publisher->last) publisher->last->next = job;
            else publisher->queued = job;
            publisher->last = job;
            ++publisher->outstanding;
            must(doe_condition_broadcast(&publisher->changed));
        }
        must(doe_mutex_unlock(&publisher->mutex));
    }
    native_unlock(device);
    if (!taken) free(job);
    return taken;
}

void native_blob_publisher_drain(native_device *device)
{
    native_blob_publisher *publisher = &device->blob_publisher;
    native_lock(device);
    const bool initialized = publisher->initialized;
    native_unlock(device);
    if (!initialized) return; /* Initialized once, under the device mutex, before any job. */
    must(doe_mutex_lock(&publisher->mutex));
    while (publisher->outstanding) must(doe_condition_wait(&publisher->changed, &publisher->mutex));
    must(doe_mutex_unlock(&publisher->mutex));
}

void native_blob_publisher_finish(native_device *device)
{
    native_blob_publisher *publisher = &device->blob_publisher;
    native_lock(device);
    const bool started = publisher->started;
    publisher->started = false;
    if (started) {
        must(doe_mutex_lock(&publisher->mutex));
        publisher->stopping = true;
        must(doe_condition_broadcast(&publisher->changed));
        must(doe_mutex_unlock(&publisher->mutex));
    }
    native_unlock(device);
    /* The worker drains its queue, storing each envelope, before it exits. */
    if (started) must(doe_thread_join(&publisher->thread));
}

void native_blob_publisher_destroy(native_device *device)
{
    native_blob_publisher *publisher = &device->blob_publisher;
    if (!publisher->initialized) return;
    if (publisher->started || publisher->queued) abort();
    must(doe_condition_destroy(&publisher->changed));
    must(doe_mutex_destroy(&publisher->mutex));
    publisher->initialized = false;
}
