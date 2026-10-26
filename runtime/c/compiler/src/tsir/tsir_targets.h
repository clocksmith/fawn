/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_TARGETS_H
#define DOE_TSIR_TARGETS_H

#include "tsir_planner.h"

/* Non-CSL bootstrap target registry. All returned descriptors/strings are
 * borrowed immutable process-lifetime data; callers do not free them. */
typedef enum TsirBootstrapTarget {
    TSIR_TARGET_WEBGPU_GENERIC = 0,
    TSIR_TARGET_MSL = 1,
    TSIR_TARGET_SPIR_V = 2
} TsirBootstrapTarget;

typedef struct TsirTargetIdentity {
    const char *name;
    TsirTargetDescriptor planner;
    uint64_t pe_persistent_pool_bytes;
    uint32_t sub_tile_lane_width;
    const char *const *fused_intrinsics;
    size_t fused_intrinsic_count;
    const char *streaming_gemm;
    /* Search hint only: deliberately excluded from correctness identity. */
    uint32_t fabric_per_hop_latency_ns;
} TsirTargetIdentity;

const TsirTargetIdentity *tsir_target_descriptor(TsirBootstrapTarget target);

/* Hash exact targets/mod.zig correctness field ordering, little-endian
 * numeric fields and enum tag spellings. On failure out is untouched. */
TsirStatus tsir_target_descriptor_hash(const TsirTargetIdentity *descriptor,
    uint8_t out[32]);

/* The retained bootstrap manifest format binds the REFERENCE emitter source
 * identity, not the C implementation's source identity. Read the same exact
 * source-byte concatenation as Zig emitterCodeDigest, rooted at repository_root.
 * No Zig executable is invoked. Each regular source input is bounded to 1 MiB.
 * Missing, unreadable, replaced, or oversized files fail visibly; out is
 * untouched on failure. Caller keeps root alive for the synchronous call. */
TsirStatus tsir_target_emitter_digest(TsirBootstrapTarget target,
    const char *repository_root, uint8_t out[32]);

#endif
