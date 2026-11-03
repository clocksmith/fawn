/* SPDX-License-Identifier: MIT */
#ifndef DOE_METAL_QUERY_H
#define DOE_METAL_QUERY_H
#include "doe_metal_texture.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Native timestamp storage, not the public query-set/availability authority.
 * The same command completion owner leases this object. A sample is written at
 * most once by strict operations per recording; the explicitly ordered boundary
 * below supports overwrite after all preceding work. Settled recordings may reuse it. */
typedef struct doe_mtl_timestamps doe_mtl_timestamps;
typedef struct doe_mtl_pass_timestamps {
    doe_mtl_timestamps *set;
    uint32_t begin, end; /* UINT32_MAX omits that endpoint; at least one is present. */
} doe_mtl_pass_timestamps;
enum { DOE_MTL_TIMESTAMP_CAPACITY = 4096 };
doe_mtl_status doe_mtl_timestamps_create(doe_mtl_context *context, uint32_t count,
    doe_mtl_timestamps **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_timestamps_retain(doe_mtl_timestamps *timestamps);
void doe_mtl_timestamps_release(doe_mtl_timestamps *timestamps);
/* Caller serializes this recording. Validate and retain through the existing
 * query-use owner before any pass encoder mutation. Unsupported stage sampling,
 * prior resolve/ordered boundaries or an already-written endpoint return OK with
 * attach=false, so the caller can retain explicit ordered boundaries. Success
 * never marks a sample available. Outputs remain untouched on error. */
doe_mtl_status doe_mtl_commands_prepare_pass_timestamps(doe_mtl_commands *commands,
    const doe_mtl_pass_timestamps *timestamps, bool *attach, doe_mtl_error *error);
/* Requires actual blit-boundary sampling support, not just stage sampling. */
doe_mtl_status doe_mtl_commands_timestamp(doe_mtl_commands *commands,
    doe_mtl_timestamps *timestamps, uint32_t index, doe_mtl_error *error);
/* Ordered standalone boundary using actual stage or blit sampling. The native
 * event precedes sampling, so same-recording index reuse preserves earlier
 * resolves. On stage devices a leased four-byte blit makes the encoder execute;
 * the recorded value is a native counter, never that scratch value. */
doe_mtl_status doe_mtl_commands_ordered_timestamp(doe_mtl_commands *commands,
    doe_mtl_timestamps *timestamps, uint32_t index, doe_mtl_error *error);
/* Availability belongs to the same speculative command snapshot. This query
 * does not publish or invent values; upstream public query policy can zero
 * unavailable samples explicitly while strict raw resolve remains strict. */
doe_mtl_status doe_mtl_timestamps_available(doe_mtl_commands *commands,
    doe_mtl_timestamps *timestamps, uint32_t index, bool *out);
/* Native CounterResultTimestamp records, uint64_t each, with no fabricated
 * values or assumed Mach-time conversion. UINT64_MAX is Metal CounterErrorValue.
 * Successful GPU completion does not prove every counter value is valid.
 * Resolve never silently substitutes zero for unavailable samples. */
doe_mtl_status doe_mtl_commands_resolve_timestamps(doe_mtl_commands *commands,
    doe_mtl_timestamps *timestamps, uint32_t first, uint32_t count,
    doe_mtl_buffer *destination, uint64_t offset, doe_mtl_error *error);

typedef struct doe_mtl_compute_dispatch {
    doe_mtl_pipeline *pipeline;
    const doe_mtl_buffer_binding *buffers;
    size_t buffer_count;
    const doe_mtl_texture_binding *textures;
    size_t texture_count;
    const doe_mtl_sampler_binding *samplers;
    size_t sampler_count;
    doe_mtl_dimensions groups;
    uint32_t repeat;
    /* Optional stage-boundary writes surround this one native compute encoder,
     * including all repetitions. UINT32_MAX omits one endpoint. Defined indices
     * must be distinct and in range; at least one endpoint is required.
     * Empty dispatch still emits the encoder if timestamps are requested. */
    doe_mtl_timestamps *timestamps;
    uint32_t timestamp_begin, timestamp_end;
    doe_mtl_immediate_binding immediates;
} doe_mtl_compute_dispatch;
doe_mtl_status doe_mtl_commands_compute(doe_mtl_commands *commands,
    const doe_mtl_compute_dispatch *dispatch, doe_mtl_error *error);
#ifdef __cplusplus
}
#endif
#endif
