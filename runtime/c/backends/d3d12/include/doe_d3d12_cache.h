/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_CACHE_H
#define DOE_D3D12_CACHE_H
#include "doe_d3d12.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct doe_dx_pipeline_cache_stats {
    uint64_t load_attempts, load_successes, additions, addition_failures;
    uint64_t serializations, serialization_failures;
    bool enabled, dirty;
} doe_dx_pipeline_cache_stats;
/* Explicit directory, serialized with pipeline creation. Native library bytes
 * are driver/adapter-specific, not shader semantics or evidence of cache hits.
 * NULL/0 disables without writing. Failure leaves the current cache unchanged. */
doe_dx_status doe_dx_context_pipeline_cache(doe_dx_context *, const char *, size_t, doe_dx_error *);
doe_dx_status doe_dx_context_pipeline_cache_flush(doe_dx_context *, doe_dx_error *);
doe_dx_status doe_dx_context_pipeline_cache_stats(const doe_dx_context *, doe_dx_pipeline_cache_stats *);
#ifdef __cplusplus
}
#endif
#endif
