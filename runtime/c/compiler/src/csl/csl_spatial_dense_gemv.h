// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_dense_gemv_host_plan.zig (UNTESTED).
// Status: done for the arithmetic and the binding table, with one caller
// supplied value (see below). Parity is NOT verified.
#ifndef DOE_CSL_SPATIAL_DENSE_GEMV_H
#define DOE_CSL_SPATIAL_DENSE_GEMV_H

#include <stddef.h>
#include <stdint.h>

#include "csl_error.h"
#include "csl_spatial_bindings.h"

/* TARGET_IN_PER_PE from the Zig source. */
#define CSL_SPATIAL_DENSE_GEMV_TARGET_IN_PER_PE 32u

/*
 * The Zig source takes OUT_SHARD_PES from csl_spec.MAX_RECT_DIM.
 * Confirmed from the local clone at d25e075:
 * runtime/zig/src/compiler/wgsl/emit/csl/csl_spec.zig:45 gives
 * MAX_RECT_DIM 512 (u16), the maximum PE grid dimension for SDK
 * rectangle programs. The constant below is that value. The
 * functions still take max_rect_dim as an explicit argument (rather
 * than hard-coding the constant) so the arithmetic stays testable
 * at other rectangle sizes; production callers pass
 * CSL_SPATIAL_MAX_RECT_DIM.
 */
#define CSL_SPATIAL_MAX_RECT_DIM 512u
CslErr csl_spatial_dense_gemv_width(uint32_t hidden_dim,
                                    uint32_t max_rect_dim,
                                    uint32_t *out_width);
CslErr csl_spatial_dense_gemv_height(uint32_t vocab_size,
                                     uint32_t out_shard_pes,
                                     uint32_t *out_height);
CslErr csl_spatial_dense_gemv_in_dim_per_pe(uint32_t hidden_dim,
                                            uint32_t max_rect_dim,
                                            uint32_t *out_value);

/*
 * The 3 dense GEMV bindings (activation f16, weight f16 with
 * runtime_weight_mapping, output f32), copied into the caller's array.
 * The caller array must hold at least 3 entries. No allocation, no
 * ownership transfer; the strings are static literals.
 */
CslErr csl_spatial_dense_gemv_bindings(CslSpatialBinding *out,
                                       size_t capacity, size_t *out_count);

#endif
