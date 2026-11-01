/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_PIPELINE_BLOB_PUBLISHER_H
#define DOE_NATIVE_PIPELINE_BLOB_PUBLISHER_H
#include "doe_platform.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One per Vulkan device with an embedder pipeline blob store. A pipeline
 * creation that missed hands its populated driver cache here instead of
 * serializing it, so the creating call pays neither vkGetPipelineCacheData,
 * the envelope digest nor the embedder's store. One worker thread serializes
 * the caches in creation order and hands each envelope to the store, holding
 * no Doe lock. Device destruction serializes and stores what remains, then
 * joins the worker: no store runs after it returns. The worker uses only the
 * deferred caches, read-only context state and the device mutex for counters. */
typedef struct native_blob_job native_blob_job;
typedef struct native_blob_publisher {
    doe_mutex mutex;
    doe_condition changed;
    doe_thread thread;
    bool initialized, started, stopping;
    native_blob_job *queued, *last; /* Oldest first. */
    size_t outstanding;             /* Queued or being serialized. */
} native_blob_publisher;

/* Jobs the publisher holds before creation serializes inline again. */
#define NATIVE_BLOB_PUBLISHER_LIMIT 64u

struct native_device;
/* Caller holds no Doe lock. False when the publisher cannot take the cache
 * (start failure, destroyed device, bound reached): the caller then keeps the
 * inline serialization and store, unchanged. */
bool native_blob_publisher_defer(struct native_device *device, uint64_t cache, const uint8_t key[32]);
/* The same serialization, count and store on the calling thread (no Doe lock
 * held): the path a creation keeps when the publisher refuses its cache. */
void native_blob_publish_now(struct native_device *device, uint64_t cache, const uint8_t key[32]);
/* Waits until every deferred cache has been serialized and stored; caller
 * holds no Doe lock. doeNativeDeviceFlushPipelineCache uses it. */
void native_blob_publisher_drain(struct native_device *device);
/* Device destruction and final release; caller holds no Doe lock. Idempotent. */
void native_blob_publisher_finish(struct native_device *device);
/* Final release, after finish. */
void native_blob_publisher_destroy(struct native_device *device);
#endif
