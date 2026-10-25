/* SPDX-License-Identifier: MIT */
#ifndef DOE_METAL_CACHE_H
#define DOE_METAL_CACHE_H
#include "doe_metal.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Optional backend binary archives. A null/zero directory disables caching.
 * Source libraries still compile normally; native descriptor matching alone
 * selects compatible device binaries. No kernel-name or source-text shortcut.
 * Configure and flush require the same caller serialization as pipeline creation.
 * Cache failures do not change existing pipeline objects or command leases. */
typedef struct doe_mtl_pipeline_cache_stats {
    uint64_t load_attempts, load_successes, additions, addition_failures;
    uint64_t serializations, serialization_failures;
    bool enabled, dirty;
} doe_mtl_pipeline_cache_stats;
doe_mtl_status doe_mtl_context_pipeline_cache(doe_mtl_context *,
    const char *directory, size_t length, doe_mtl_error *);
doe_mtl_status doe_mtl_context_pipeline_cache_flush(doe_mtl_context *, doe_mtl_error *);
doe_mtl_status doe_mtl_context_pipeline_cache_stats(const doe_mtl_context *, doe_mtl_pipeline_cache_stats *);
#ifdef __cplusplus
}
#endif
#endif
