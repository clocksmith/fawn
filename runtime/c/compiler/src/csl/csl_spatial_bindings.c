// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_host_plan_bindings.zig (UNTESTED).
// Tables and dispatch are literal ports. Parity NOT verified.
#include "csl_spatial_bindings.h"

#include <string.h>

#include "csl_spatial_dense_gemv.h"

#define BIND(sym, acc, elem, shape, per, sk, sr, dk, dr, drows, ws) \
    { sym, acc, elem, shape, per, sk, sr, dk, dr, drows, ws }

static const CslSpatialBinding kRmsnorm[] = {
    BIND("input", "read", "f32", "hidden_size", "hidden_size", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("weight", "read", "f32", "hidden_size", "hidden_size", NULL, NULL, NULL, NULL, NULL, "runtime_weight_mapping"),
    BIND("output", "read_write", "f32", "hidden_size", "hidden_size", NULL, NULL, NULL, NULL, NULL, NULL),
};

static const CslSpatialBinding kResidual[] = {
    BIND("input", "read", "f32", "chunk_size", "chunk_size", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("residual", "read", "f32", "chunk_size", "chunk_size", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("output", "read_write", "f32", "chunk_size", "chunk_size", NULL, NULL, NULL, NULL, NULL, NULL),
};

static const CslSpatialBinding kGelu[] = {
    BIND("input", "read", "f32", "chunk_size", "chunk_size", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("output", "read_write", "f32", "chunk_size", "chunk_size", NULL, NULL, NULL, NULL, NULL, NULL),
};

static const CslSpatialBinding kGated[] = {
    BIND("gate", "read", "f32", "chunk_size", "chunk_size", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("input", "read", "f32", "chunk_size", "chunk_size", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("output", "read_write", "f32", "chunk_size", "chunk_size", NULL, NULL, NULL, NULL, NULL, NULL),
};

static const CslSpatialBinding kL2Normalize[] = {
    BIND("input", "read", "f32", "hidden_size", "hidden_size", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("output", "read_write", "f32", "hidden_size", "hidden_size", NULL, NULL, NULL, NULL, NULL, NULL),
};

static const CslSpatialBinding kConv1dDepthwise[] = {
    BIND("input", "read", "f32", "num_tokens * channels", "num_tokens * channels", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("weight", "read", "f32", "channels * kernel_size", "channels * kernel_size", NULL, NULL, NULL, NULL, NULL, "runtime_weight_mapping"),
    BIND("bias", "read", "f32", "channels", "channels", NULL, NULL, NULL, NULL, NULL, "runtime_weight_mapping"),
    BIND("output", "read_write", "f32", "num_tokens * channels", "num_tokens * channels", NULL, NULL, NULL, NULL, NULL, NULL),
};

static const CslSpatialBinding kLinearAttention[] = {
    BIND("query", "read", "f32", "value_dim", "value_dim_per_pe", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("key", "read", "f32", "key_dim", "key_dim", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("value", "read", "f32", "key_dim", "key_dim", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("gate", "read", "f32", "value_dim", "value_dim_per_pe", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("linear_state", "read_write", "f32", "value_dim * key_dim", "value_dim_per_pe * key_dim", NULL, NULL, NULL, NULL, NULL, NULL),
    BIND("output", "read_write", "f32", "value_dim", "value_dim_per_pe", NULL, NULL, NULL, NULL, NULL, NULL),
};

static const CslSpatialBinding kTiled[] = {
    BIND("a", "read", "f32", "Mt * Kt", "Mt * Kt", "logical_matrix_to_summa_tiles", "a", NULL, NULL, NULL, NULL),
    BIND("b", "read", "f32", "Kt * Nt", "Kt * Nt", "weight_matrix_to_summa_tiles", "b", NULL, NULL, NULL, "runtime_weight_mapping"),
    BIND("c", "read_write", "f32", "Mt * Nt", "Mt * Nt", NULL, NULL, "summa_tiles_to_logical_matrix", "c", "a", NULL),
};

static const CslSpatialBinding kPrefillQ4kGemv[] = {
    BIND("activation", "read", "f16", "in_dim_per_pe", "in_dim_per_pe", "logical_matrix_to_prefill_q4k_gemv_activation_shards", NULL, NULL, NULL, NULL, NULL),
    BIND("weight", "read", "u8", "out_dim_per_pe * num_blocks_per_row * 144", "out_dim_per_pe * num_blocks_per_row * 144", "q4km_rowwise_to_prefill_q4k_gemv_weight_tiles", NULL, NULL, NULL, NULL, "runtime_weight_mapping"),
    BIND("output", "read_write", "f16", "out_dim_per_pe", "out_dim_per_pe", NULL, NULL, "prefill_q4k_gemv_row_tiles_to_logical_matrix", NULL, NULL, NULL),
};

static int eq(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static CslErr copy_table(const CslSpatialBinding *table, size_t count,
                         CslSpatialBinding *out, size_t capacity,
                         size_t *out_count) {
    size_t i;
    if (!out || !out_count || capacity < count) {
        return CSL_ERR_INVALID;
    }
    for (i = 0; i < count; i++) {
        out[i] = table[i];
    }
    *out_count = count;
    return CSL_OK;
}

typedef struct PatternEntry {
    const char *pattern;
    const CslSpatialBinding *table;
    size_t count;
    int direct; /* 1: returned as-is for every scalar, no f16 reroute */
} PatternEntry;

static const PatternEntry kPatterns[] = {
    { "rms_norm", kRmsnorm, 3, 0 },
    { "reduction", kRmsnorm, 3, 0 },
    { "residual", kResidual, 3, 0 },
    { "gelu", kGelu, 2, 0 },
    { "gelu_gated", kGated, 3, 0 },
    { "silu_gated", kGated, 3, 0 },
    { "sigmoid_gated", kGated, 3, 0 },
    { "l2_normalize", kL2Normalize, 2, 0 },
    { "conv1d_depthwise", kConv1dDepthwise, 4, 0 },
    { "linear_attention", kLinearAttention, 6, 0 },
    { "tiled_matmul", kTiled, 3, 0 },
    { "prefill_q4k_gemv", kPrefillQ4kGemv, 3, 1 },
};

CslErr csl_spatial_bindings_for_pattern(const char *pattern,
                                        CslSpatialScalar activation_elem,
                                        CslSpatialBinding *out,
                                        size_t capacity, size_t *out_count,
                                        int *found) {
    const CslSpatialBinding *table = NULL;
    size_t count = 0;
    int direct = 0;
    size_t i;
    CslErr err;
    if (!pattern || !out || !out_count || !found) {
        return CSL_ERR_INVALID;
    }
    *out_count = 0;
    *found = 0;
    /* Dense GEMV keeps its own module and is returned as-is, as in */
    /* Zig, which returns the static dense table directly. */
    if (eq(pattern, "dense_gemv")) {
        err = csl_spatial_dense_gemv_bindings(out, capacity, out_count);
        if (err) {
            return err;
        }
        *found = 1;
        return CSL_OK;
    }
    /* Table-driven dispatch replaces the Zig if/else chain. The */
    /* mapping is data, not logic: each pattern maps to exactly the */
    /* table the Zig chain selected, and first-match order is */
    /* unchanged because no pattern appears twice with different */
    /* tables (aliases share one table). */
    for (i = 0; i < sizeof(kPatterns) / sizeof(kPatterns[0]); i++) {
        if (eq(pattern, kPatterns[i].pattern)) {
            table = kPatterns[i].table;
            count = kPatterns[i].count;
            direct = kPatterns[i].direct;
            break;
        }
    }
    if (!table) {
        return CSL_OK;
    }
    if (!direct && activation_elem == CSL_SPATIAL_SCALAR_OTHER) {
        return CSL_ERR_INVALID;
    }
    err = copy_table(table, count, out, capacity, out_count);
    if (err) {
        return err;
    }
    *found = 1;
    if (!direct && activation_elem == CSL_SPATIAL_SCALAR_F16) {
        for (i = 0; i < count; i++) {
            if (out[i].elem_type && strcmp(out[i].elem_type, "f32") == 0) {
                out[i].elem_type = "f16";
            }
        }
    }
    return CSL_OK;
}
