// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_host_plan_bindings.zig (UNTESTED).
// Status: done for the binding tables and pattern dispatch, including the
// f16 lane rerouting rule. The Zig BindingMetadata type itself lives in
// the out-of-scope emit_csl_host_plan module, so this header defines a
// local mirror struct with exactly the fields these tables use. Field
// order and defaults follow the Zig literals. Parity is NOT verified.
#ifndef DOE_CSL_SPATIAL_BINDINGS_H
#define DOE_CSL_SPATIAL_BINDINGS_H

#include <stddef.h>

#include "csl_error.h"

/* Mirror of wgsl.ir.ScalarType as used by these tables. */
typedef enum CslSpatialScalar {
    CSL_SPATIAL_SCALAR_F32 = 0,
    CSL_SPATIAL_SCALAR_F16 = 1,
    CSL_SPATIAL_SCALAR_OTHER = 2
} CslSpatialScalar;

/*
 * One binding. Nullable Zig optionals are plain NULL pointers here.
 * All string pointers are borrowed static literals except when the
 * caller copies a table and owns the array itself (the strings stay
 * borrowed either way).
 */
typedef struct CslSpatialBinding {
    const char *symbol;
    const char *access;
    const char *elem_type;
    const char *binding_shape_elements;
    const char *per_pe_shape_elements;
    const char *staging_kind;
    const char *staging_matrix_role;
    const char *detile_kind;
    const char *detile_matrix_role;
    const char *detile_rows_from_input;
    const char *weight_source;
} CslSpatialBinding;

/* Largest table in this module has 6 entries (linear_attention). */
#define CSL_SPATIAL_BINDINGS_MAX 6

/*
 * Resolve a pattern to its binding table, copied into the caller's
 * array (capacity must be at least CSL_SPATIAL_BINDINGS_MAX).
 * Sets *found to 0 and *out_count to 0, returning CSL_OK, for an
 * unknown pattern (the Zig function returns null, which is not an
 * error). Sets *found to 1 on a match.
 * f16 rerouting: for every pattern except prefill_q4k_gemv and
 * dense_gemv, an f16 activation copies the table and rewrites any
 * elem_type of exactly "f32" to "f16"; f32 returns the table as-is;
 * any other scalar returns CSL_ERR_INVALID. prefill_q4k_gemv and
 * dense_gemv are returned as-is for every scalar, matching Zig, which
 * returns the static tables directly for those two patterns.
 */
CslErr csl_spatial_bindings_for_pattern(const char *pattern,
                                        CslSpatialScalar activation_elem,
                                        CslSpatialBinding *out,
                                        size_t capacity, size_t *out_count,
                                        int *found);

#endif
