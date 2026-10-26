// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_dense_gemv_host_plan.zig (UNTESTED).
// Parity NOT verified. max_rect_dim is caller supplied (see header).
#include "csl_spatial_dense_gemv.h"

static uint32_t ceil_div_u32(uint32_t lhs, uint32_t rhs) {
    /* rhs is always at least 1 at every call site in this file. */
    uint64_t v = (uint64_t)lhs + (uint64_t)rhs - 1u;
    return (uint32_t)(v / (uint64_t)rhs);
}

static uint32_t even_ceil_div_u32(uint32_t lhs, uint32_t rhs) {
    uint32_t v = ceil_div_u32(lhs, rhs);
    return (v % 2u == 0u) ? v : v + 1u;
}

CslErr csl_spatial_dense_gemv_width(uint32_t hidden_dim,
                                    uint32_t max_rect_dim,
                                    uint32_t *out_width) {
    uint32_t w;
    if (!out_width || max_rect_dim == 0u) {
        return CSL_ERR_INVALID;
    }
    w = ceil_div_u32(hidden_dim, CSL_SPATIAL_DENSE_GEMV_TARGET_IN_PER_PE);
    if (w < 1u) {
        w = 1u;
    }
    if (w > max_rect_dim) {
        w = max_rect_dim;
    }
    *out_width = w;
    return CSL_OK;
}

CslErr csl_spatial_dense_gemv_height(uint32_t vocab_size,
                                     uint32_t out_shard_pes,
                                     uint32_t *out_height) {
    if (!out_height) {
        return CSL_ERR_INVALID;
    }
    *out_height = (out_shard_pes < vocab_size) ? out_shard_pes : vocab_size;
    return CSL_OK;
}

CslErr csl_spatial_dense_gemv_in_dim_per_pe(uint32_t hidden_dim,
                                            uint32_t max_rect_dim,
                                            uint32_t *out_value) {
    uint32_t w;
    CslErr err;
    if (!out_value) {
        return CSL_ERR_INVALID;
    }
    err = csl_spatial_dense_gemv_width(hidden_dim, max_rect_dim, &w);
    if (err) {
        return err;
    }
    *out_value = even_ceil_div_u32(hidden_dim, w);
    return CSL_OK;
}

CslErr csl_spatial_dense_gemv_bindings(CslSpatialBinding *out,
                                       size_t capacity, size_t *out_count) {
    static const CslSpatialBinding table[3] = {
        {
            "activation", "read", "f16",
            "in_dim_per_pe", "in_dim_per_pe",
            "logical_vector_to_dense_gemv_activation_shards", NULL,
            NULL, NULL, NULL, NULL
        },
        {
            "weight", "read", "f16",
            "out_dim_per_pe * in_dim_per_pe", "out_dim_per_pe * in_dim_per_pe",
            "tied_f16_embedding_to_dense_gemv_shards", NULL,
            NULL, NULL, NULL, "runtime_weight_mapping"
        },
        {
            "output", "read_write", "f32",
            "out_dim_per_pe", "out_dim_per_pe",
            NULL, NULL,
            "dense_gemv_row_shards_to_logits", NULL, NULL, NULL
        },
    };
    size_t i;
    if (!out || !out_count || capacity < 3u) {
        return CSL_ERR_INVALID;
    }
    for (i = 0; i < 3u; i++) {
        out[i] = table[i];
    }
    *out_count = 3u;
    return CSL_OK;
}
