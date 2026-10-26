/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_TARGETS_H
#define DOE_TSIR_TARGETS_H

#include "tsir_planner.h"
#include "tsir_emit.h"

/* Non-CSL bootstrap target registry. All returned descriptors/strings are
 * borrowed immutable process-lifetime data; callers do not free them. */
typedef enum TsirBootstrapTarget {
    TSIR_TARGET_WEBGPU_GENERIC = 0,
    TSIR_TARGET_MSL = 1,
    TSIR_TARGET_SPIR_V = 2
} TsirBootstrapTarget;

/* Identity and planning use one descriptor, not detached copies of its fields. */
typedef TsirTargetDescriptor TsirTargetIdentity;

const TsirTargetIdentity *tsir_target_descriptor(TsirBootstrapTarget target);

/* Hash exact targets/mod.zig correctness field ordering, little-endian
 * numeric fields and enum tag spellings. On failure out is untouched. */
TsirStatus tsir_target_descriptor_hash(const TsirTargetIdentity *descriptor,
    uint8_t out[32]);

/* The retained bootstrap manifest format binds the REFERENCE emitter source
 * identity, not the C implementation's source identity. Read the same exact
 * source-byte concatenation as Zig emitterCodeDigest, rooted at repository_root.
 * No Zig executable is invoked. Each regular source input is bounded to 1 MiB.
 * Missing, unreadable, nonregular or oversized files and detected size/time
 * changes fail visibly; out is untouched on failure. Callers keep UTF-8 root
 * alive and reference inputs stable for the synchronous call. */
TsirStatus tsir_target_emitter_digest(TsirBootstrapTarget target,
    const char *repository_root, uint8_t out[32]);

/* All retained non-CSL emitter identities, including DXIL (which has no
 * dedicated retained bootstrap descriptor). CSL fails as unsupported.
 * The same source-input and untouched-output contract applies as above. */
TsirStatus tsir_emitter_code_digest(TsirKernelBackend backend,
    const char *repository_root, uint8_t out[32]);

#endif
