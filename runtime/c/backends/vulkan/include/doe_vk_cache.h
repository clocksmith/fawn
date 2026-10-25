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
#ifdef __cplusplus
}
#endif
#endif
