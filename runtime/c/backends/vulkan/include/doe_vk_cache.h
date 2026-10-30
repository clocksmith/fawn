/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_CACHE_H
#define DOE_VK_CACHE_H
#include "doe_vk.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct doe_vk_pipeline_cache_stats {
    uint64_t load_attempts, load_successes, additions, addition_failures;
    uint64_t serializations, serialization_failures;
    bool enabled, dirty;
} doe_vk_pipeline_cache_stats;
/* Explicit directory only; NULL/0 disables without writing. Serialized by the
 * same caller as pipeline creation. Driver cache compatibility is not a hit or
 * shader-source semantic cache. Existing pipeline sharing remains separate. */
doe_vk_status doe_vk_context_pipeline_cache(doe_vk_context *, const char *, size_t, doe_vk_error *);
doe_vk_status doe_vk_context_pipeline_cache_flush(doe_vk_context *, doe_vk_error *);
doe_vk_status doe_vk_context_pipeline_cache_stats(const doe_vk_context *, doe_vk_pipeline_cache_stats *);
/* One-way: later compute pipelines each use a temporary driver cache seeded from
 * caller-persisted bytes, never a file. Exclusive with the directory cache. */
doe_vk_status doe_vk_context_pipeline_blobs(doe_vk_context *, doe_vk_error *);
bool doe_vk_context_pipeline_blobs_enabled(const doe_vk_context *);
/* Locates a blob; it never selects a pipeline. The driver validates its own
 * cache contents (including layout and immediates, which the key omits), and
 * the envelope binds device, driver and payload digest. */
bool doe_vk_pipeline_blob_key(const doe_vk_context *, const uint8_t source[32], const uint8_t build[32],
    const uint32_t *spirv, size_t word_count, const char *entrypoint, uint32_t required_subgroup_size,
    uint8_t key[32]);
enum { DOE_VK_CACHE_BLOB_LIMIT = 32 * 1024 * 1024 }; /* Equals the cache file bound. */
typedef struct doe_vk_pipeline_blob {
    const uint8_t *loaded; size_t loaded_size; /* Borrowed for the call; NULL on a miss. */
    uint8_t *produced; size_t produced_size;   /* Envelope to persist; the caller frees it. */
} doe_vk_pipeline_blob;
#ifdef __cplusplus
}
#endif
#endif
