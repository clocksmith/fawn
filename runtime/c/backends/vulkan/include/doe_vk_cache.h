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
/* Render pipelines combine both stage keys; fixed-function state, layout and
 * attachments are validated by the driver cache, not the key. fragment_spirv
 * may be NULL for a depth-only pipeline. */
bool doe_vk_render_pipeline_blob_key(const doe_vk_context *, const uint8_t vertex_source[32],
    const uint8_t fragment_source[32], const uint8_t build[32], const uint32_t *vertex_spirv, size_t vertex_words,
    const char *vertex_entry, const uint32_t *fragment_spirv, size_t fragment_words, const char *fragment_entry,
    uint8_t key[32]);
typedef struct doe_vk_pipeline_blob {
    const uint8_t *loaded; size_t loaded_size; /* Borrowed for the call; NULL on a miss. */
    uint8_t *produced; size_t produced_size;   /* Envelope to persist; the caller frees it. */
    /* Set by the caller: on a miss, keep the populated driver cache in
     * `deferred` instead of serializing it during creation. The caller then
     * owns it and passes it once to doe_vk_pipeline_blob_serialize or _discard. */
    bool defer;
    uint64_t deferred;
} doe_vk_pipeline_blob;
/* Envelope (same bytes as `produced`) of a deferred driver cache, which is
 * destroyed either way. Callable from any thread: it uses only that cache,
 * which nothing else references, and read-only context state; it updates no
 * context counters (record the outcome with _counted under the caller's lock).
 * The device must outlive the call. */
doe_vk_status doe_vk_pipeline_blob_serialize(doe_vk_context *, uint64_t deferred, uint8_t **out, size_t *out_size);
void doe_vk_pipeline_blob_discard(doe_vk_context *, uint64_t deferred);
void doe_vk_pipeline_blob_counted(doe_vk_context *, bool serialized);
#ifdef __cplusplus
}
#endif
#endif
