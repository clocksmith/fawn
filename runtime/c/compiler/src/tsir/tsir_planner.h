/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_PLANNER_H
#define DOE_TSIR_PLANNER_H

#include "tsir_schema.h"

/* Realization planner, mirroring planner.zig, planner_support.zig, and
 * collective_synthesis.zig. Only the target-descriptor correctness
 * fields the planner actually reads are mirrored here; this is not a
 * claim about the full Zig TargetDescriptor (see gaps.md for the
 * descriptor-hash gap). Numerical mode names are the Zig
 * NumericalMode tag spellings ("f32", "f16", "bf16"). Native
 * collective entries carry the Zig kind_name and exactness_name
 * strings verbatim.
 *
 * Ownership: the caller owns the input semantic and descriptor for the
 * whole call. On success, tsir_plan_realization fills *out with
 * freshly allocated arrays (per-function tiles, residency, collectives,
 * reductions; per-function emitter_params_json copy; rejection
 * node_path/detail copies) and the caller releases them with
 * tsir_plan_free_realization. On failure, *out is left empty and
 * nothing is leaked by this function. Rejection strings inside *out
 * are owned by *out, not by the input. */

typedef enum TsirRuntimeSizedPolicy {
    TSIR_POLICY_HOST_COPIED = 0,
    TSIR_POLICY_FABRIC_STREAMED_WITH_LOADER = 1,
    TSIR_POLICY_REJECT = 2
} TsirRuntimeSizedPolicy;

typedef struct TsirNativeCollective {
    const char *kind_name;
    const char *exactness_name;
} TsirNativeCollective;

typedef struct TsirTargetDescriptor {
    uint64_t pe_working_memory_bytes;
    uint32_t fabric_color_count;
    uint32_t max_collective_group_size;
    const char *const *native_numerical_modes;
    size_t native_numerical_mode_count;
    const TsirNativeCollective *native_collectives;
    size_t native_collective_count;
    TsirRuntimeSizedPolicy runtime_sized_binding_policy;
} TsirTargetDescriptor;

typedef struct TsirLoaderCapabilities {
    bool fabric_streaming;
    uint64_t max_stream_chunk_bytes;
} TsirLoaderCapabilities;

typedef struct TsirPlannerOptions {
    TsirLoaderCapabilities loader;
    uint8_t emitter_digest[32];
    const char *emitter_params_json;
} TsirPlannerOptions;

#define TSIR_DEFAULT_EMITTER_PARAMS_JSON "{\"planner\":\"tsir.first_fit_v1\"}"

void tsir_planner_default_options(TsirPlannerOptions *options);
uint32_t tsir_planner_choose_tile(const char *upper_bound);
TsirPEGridShape tsir_planner_choose_pe_grid(
    const TsirTargetDescriptor *descriptor);
bool tsir_supports_numerical_mode(const TsirTargetDescriptor *descriptor,
    TsirScalarKind kind);

/* Output realization with owned allocations; see header contract. */
typedef struct TsirPlannedRealization {
    TsirRealizationFunction *functions;
    size_t function_count;
    uint32_t contract_version;
    uint8_t emitter_digest[32];
    TsirRejectionEntry *rejections;
    size_t rejection_count;
} TsirPlannedRealization;

TsirStatus tsir_plan_realization(const TsirSemantic *semantic,
    const TsirTargetDescriptor *descriptor,
    const TsirPlannerOptions *options, TsirPlannedRealization *out);
void tsir_plan_free_realization(TsirPlannedRealization *planned);

#endif
