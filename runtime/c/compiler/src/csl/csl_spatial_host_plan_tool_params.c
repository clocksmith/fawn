// SPDX-License-Identifier: MIT
// Port of the compile parameter computation in
// runtime/zig/src/spatial/csl/csl_host_plan_tool.zig (UNTESTED).
// Split from csl_spatial_host_plan_tool.c to keep each file under 999
// lines, per runtime/c/STYLE.md. Parity NOT verified.
#include "csl_spatial_host_plan_tool.h"

#include <string.h>

#include "csl_spatial_dense_gemv.h"

#define PREFILL_IN_DIM_PER_PE 512u
#define PREFILL_OUT_DIM_PER_PE 112u
#define PREFILL_OUTPUT_PE_ROWS 1u
#define Q4K_BLOCK_ELEMENTS 256u
#define SSM_SHARD_PES 2u

typedef struct ParamList {
    CslSpatialCompileParam *params;
    size_t capacity;
    size_t count;
} ParamList;

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static CslErr ceil_div_u32(uint32_t lhs, uint32_t rhs, uint32_t *out) {
    uint64_t v;
    if (!out || rhs == 0) {
        return CSL_ERR_INVALID;
    }
    v = ((uint64_t)lhs + (uint64_t)rhs - 1u) / (uint64_t)rhs;
    if (v > 0xFFFFFFFFu) {
        return CSL_ERR_TOO_LARGE;
    }
    *out = (uint32_t)v;
    return CSL_OK;
}

static CslErr ceil_div_u64(uint64_t lhs, uint64_t rhs, uint32_t *out) {
    uint64_t v;
    if (!out || rhs == 0) {
        return CSL_ERR_INVALID;
    }
    v = (lhs + rhs - 1u) / rhs;
    if (v > 0xFFFFFFFFu) {
        return CSL_ERR_TOO_LARGE;
    }
    *out = (uint32_t)v;
    return CSL_OK;
}

static CslErr append_param(ParamList *list, const char *name,
                           uint32_t value) {
    if (!list || !name) {
        return CSL_ERR_INVALID;
    }
    if (list->count >= list->capacity) {
        return CSL_ERR_TOO_LARGE;
    }
    if (value == 0) {
        value = 1;
    }
    list->params[list->count].name = name;
    list->params[list->count].value = value;
    list->count++;
    return CSL_OK;
}

/* Shared head for the three-parameter row targets: width is the */
/* decode/phase row width, height is always 1, then one named value. */
/* Used by rms_norm/reduction (hidden_size), the residual/gelu/gated */
/* family (chunk_size), and l2_normalize (hidden_size). The Zig source */
/* spells these as separate branches with identical shapes; merging */
/* them changes no parameter name, order, or value. */
static CslErr append_row_params(ParamList *list, uint32_t row_width,
                                const char *third_name,
                                uint32_t third_value) {
    CslErr err;
    err = append_param(list, "width", row_width);
    if (err) {
        return err;
    }
    err = append_param(list, "height", 1u);
    if (err) {
        return err;
    }
    return append_param(list, third_name, third_value);
}

static int eq(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static int starts_with(const char *s, const char *prefix) {
    size_t plen;
    if (!s || !prefix) {
        return 0;
    }
    plen = strlen(prefix);
    return strncmp(s, prefix, plen) == 0;
}

static uint32_t opt_or(uint32_t fallback, int has, uint32_t value) {
    return has ? value : fallback;
}

CslErr csl_spatial_tool_dense_params(const CslSpatialModelConfig *config,
                                       uint32_t width,
                                       uint32_t max_rect_dim,
                                       CslSpatialCompileParam *out,
                                       size_t capacity,
                                       size_t *out_count) {
    ParamList local;
    ParamList *list = &local;
    local.params = out;
    local.capacity = capacity;
    local.count = 0;
    if (!out_count) {
        return CSL_ERR_INVALID;
    }
    *out_count = 0;
    uint32_t height;
    uint32_t out_dim_per_pe;
    uint32_t in_dim_per_pe;
    CslErr err;
    err = csl_spatial_dense_gemv_height(config->vocab_size, max_rect_dim,
                                        &height);
    if (err) {
        return err;
    }
    if (height == 0) {
        return CSL_ERR_INVALID;
    }
    err = ceil_div_u32(config->vocab_size, height, &out_dim_per_pe);
    if (err) {
        return err;
    }
    err = csl_spatial_dense_gemv_in_dim_per_pe(config->hidden_dim,
                                               max_rect_dim,
                                               &in_dim_per_pe);
    if (err) {
        return err;
    }
    err = append_param(list, "width", width);
    if (err) {
        return err;
    }
    err = append_param(list, "height", height);
    if (err) {
        return err;
    }
    err = append_param(list, "out_dim", config->vocab_size);
    if (err) {
        return err;
    }
    err = append_param(list, "out_dim_per_pe", out_dim_per_pe);
    if (err) {
        return err;
    }
    {
        CslErr tail_err = append_param(list, "in_dim_per_pe", in_dim_per_pe);
        if (tail_err) {
            return tail_err;
        }
        *out_count = list->count;
        return CSL_OK;
    }
}

CslErr csl_spatial_tool_compile_params(
    const CslSpatialModelConfig *config, const CslSpatialHostPlan *plan,
    const char *target_name, const char *pattern, const char *phase,
    uint32_t max_rect_dim, CslSpatialCompileParam *out, size_t capacity,
    size_t *out_count) {
    ParamList list;
    CslErr err;
    int is_decode;
    uint32_t row_width;
    if (!plan || !target_name || !pattern || !out || !out_count) {
        return CSL_ERR_INVALID;
    }
    list.params = out;
    list.capacity = capacity;
    list.count = 0;
    *out_count = 0;
    if (!config) {
        return CSL_OK;
    }
    is_decode = phase && eq(phase, "decode");
    row_width = is_decode ? 1u : plan->pe_grid_width;
    if (eq(pattern, "gather")) {
        uint32_t vocab;
        uint32_t hidden;
        uint64_t pe_count64;
        uint32_t hidden_per_pe;
        uint32_t rows_per_pe;
        int is_ple = starts_with(target_name, "ple_");
        vocab = is_ple ? opt_or(config->vocab_size,
                                config->has_ple_vocab_size,
                                config->ple_vocab_size)
                       : config->vocab_size;
        hidden = is_ple ? opt_or(config->hidden_dim, config->has_ple_width,
                                 config->ple_width)
                        : config->hidden_dim;
        pe_count64 = (uint64_t)plan->pe_grid_width *
                     (uint64_t)plan->pe_grid_height;
        if (pe_count64 == 0) {
            pe_count64 = 1;
        }
        if (plan->pe_grid_height == 0) {
            return CSL_ERR_INVALID;
        }
        err = ceil_div_u32(hidden, plan->pe_grid_height, &hidden_per_pe);
        if (err) {
            return err;
        }
        err = ceil_div_u64(vocab, pe_count64, &rows_per_pe);
        if (err) {
            return err;
        }
        err = append_param(&list, "width", plan->pe_grid_width);
        if (err) {
            return err;
        }
        err = append_param(&list, "height", plan->pe_grid_height);
        if (err) {
            return err;
        }
        err = append_param(&list, "hidden_size", hidden);
        if (err) {
            return err;
        }
        err = append_param(&list, "hidden_per_pe", hidden_per_pe);
        if (err) {
            return err;
        }
        err = append_param(&list, "rows_per_pe", rows_per_pe);
        if (err) {
            return err;
        }
        err = append_param(&list, "num_tokens",
                           min_u32(config->max_seq_len, 16u));
        if (err) {
            return err;
        }
        err = append_param(&list, "tokens_per_chunk",
                           min_u32(config->max_seq_len, 16u));
        if (err) {
            return err;
        }
    } else if (eq(pattern, "prefill_q4k_gemv")) {
        /* Confirmed from the local clone at d25e075: */
        /* emit_csl_exec_v1.zig:41 gives PREFILL_Q4K_GEMV_PATTERN */
        /* "prefill_q4k_gemv", the literal used here and by the */
        /* bindings module for the same kernel. */
        uint32_t max_feature;
        uint32_t width;
        uint32_t num_blocks;
        err = csl_spatial_tool_max_feature_dim(config, &max_feature);
        if (err) {
            return err;
        }
        err = ceil_div_u32(max_feature, PREFILL_IN_DIM_PER_PE, &width);
        if (err) {
            return err;
        }
        err = ceil_div_u32(PREFILL_IN_DIM_PER_PE, Q4K_BLOCK_ELEMENTS,
                           &num_blocks);
        if (err) {
            return err;
        }
        err = append_param(&list, "width", width);
        if (err) {
            return err;
        }
        err = append_param(&list, "height", PREFILL_OUTPUT_PE_ROWS);
        if (err) {
            return err;
        }
        err = append_param(&list, "out_dim",
                           PREFILL_OUTPUT_PE_ROWS * PREFILL_OUT_DIM_PER_PE);
        if (err) {
            return err;
        }
        err = append_param(&list, "out_dim_per_pe", PREFILL_OUT_DIM_PER_PE);
        if (err) {
            return err;
        }
        err = append_param(&list, "in_dim_per_pe", PREFILL_IN_DIM_PER_PE);
        if (err) {
            return err;
        }
        err = append_param(&list, "num_blocks_per_row", num_blocks);
        if (err) {
            return err;
        }
        err = append_param(&list, "output_pe_rows", PREFILL_OUTPUT_PE_ROWS);
        if (err) {
            return err;
        }
    } else if (eq(pattern, "tiled_matmul")) {
        if (eq(target_name, "tiled_31b")) {
            uint32_t max_feature;
            uint32_t mt;
            uint32_t kt;
            err = csl_spatial_tool_max_feature_dim(config, &max_feature);
            if (err) {
                return err;
            }
            err = ceil_div_u32(config->max_seq_len, 512u, &mt);
            if (err) {
                return err;
            }
            err = ceil_div_u32(max_feature, 512u, &kt);
            if (err) {
                return err;
            }
            err = append_param(&list, "width", 512u);
            if (err) {
                return err;
            }
            err = append_param(&list, "height", 512u);
            if (err) {
                return err;
            }
            err = append_param(&list, "P", 512u);
            if (err) {
                return err;
            }
            err = append_param(&list, "Mt", mt);
            if (err) {
                return err;
            }
            err = append_param(&list, "Kt", kt);
            if (err) {
                return err;
            }
            err = append_param(&list, "Nt", kt);
            if (err) {
                return err;
            }
        } else {
            int is_ple = starts_with(target_name, "ple_");
            uint32_t tile;
            uint32_t block_m;
            if (is_ple) {
                tile = 16u;
                block_m = 16u;
            } else {
                err = ceil_div_u32(config->hidden_dim, 32u, &tile);
                if (err) {
                    return err;
                }
                block_m = 32u;
            }
            err = append_param(&list, "width", tile);
            if (err) {
                return err;
            }
            err = append_param(&list, "height", tile);
            if (err) {
                return err;
            }
            err = append_param(&list, "P", tile);
            if (err) {
                return err;
            }
            err = append_param(&list, "Mt", block_m);
            if (err) {
                return err;
            }
            err = append_param(&list, "Kt", block_m);
            if (err) {
                return err;
            }
            err = append_param(&list, "Nt", block_m);
            if (err) {
                return err;
            }
        }
    } else if (eq(pattern, "dense_gemv")) {
        uint32_t width;
        err = csl_spatial_dense_gemv_width(config->hidden_dim, max_rect_dim,
                                           &width);
        if (err) {
            return err;
        }
        {
            size_t dense_count = 0;
            err = csl_spatial_tool_dense_params(config, width, max_rect_dim,
                                                list.params, list.capacity,
                                                &dense_count);
            if (err) {
                return err;
            }
            list.count = dense_count;
        }
        if (err) {
            return err;
        }
    } else if (eq(pattern, "rms_norm") || eq(pattern, "reduction")) {
        err = append_row_params(&list, row_width, "hidden_size",
                                config->hidden_dim);
        if (err) {
            return err;
        }
    } else if (eq(pattern, "residual") || eq(pattern, "gelu") ||
               eq(pattern, "element_wise") || eq(pattern, "gelu_gated") ||
               eq(pattern, "silu_gated") || eq(pattern, "sigmoid_gated")) {
        /* The Zig source has two separate branches here (one for */
        /* residual/gelu/element_wise, one for the gated set) with */
        /* byte-identical bodies: width, height 1, chunk_size. */
        /* Merging them preserves every name, order, and value. */
        err = append_row_params(&list, row_width, "chunk_size",
                                config->hidden_dim);
        if (err) {
            return err;
        }
    } else if (eq(pattern, "l2_normalize")) {
        uint32_t hidden = opt_or(min_u32(config->head_dim, 128u),
                                 config->has_linear_key_head_dim,
                                 config->linear_key_head_dim);
        err = append_row_params(&list, row_width, "hidden_size", hidden);
        if (err) {
            return err;
        }
    } else if (eq(pattern, "conv1d_depthwise")) {
        uint32_t channels = opt_or(min_u32(config->head_dim, 128u),
                                   config->has_linear_key_head_dim,
                                   config->linear_key_head_dim);
        uint32_t kernel_size = opt_or(4u, config->has_linear_conv_kernel_dim,
                                      config->linear_conv_kernel_dim);
        err = append_param(&list, "width", row_width);
        if (err) {
            return err;
        }
        err = append_param(&list, "height", 1u);
        if (err) {
            return err;
        }
        err = append_param(&list, "num_tokens",
                           min_u32(config->max_seq_len, 4u));
        if (err) {
            return err;
        }
        err = append_param(&list, "channels", channels);
        if (err) {
            return err;
        }
        err = append_param(&list, "kernel_size", kernel_size);
        if (err) {
            return err;
        }
    } else if (eq(pattern, "linear_attention")) {
        uint32_t value_dim = opt_or(min_u32(config->head_dim, 128u),
                                    config->has_linear_value_head_dim,
                                    config->linear_value_head_dim);
        uint32_t value_dim_per_pe;
        uint32_t key_dim = opt_or(min_u32(config->head_dim, 128u),
                                  config->has_linear_key_head_dim,
                                  config->linear_key_head_dim);
        err = ceil_div_u32(value_dim, SSM_SHARD_PES, &value_dim_per_pe);
        if (err) {
            return err;
        }
        err = append_param(&list, "width", 1u);
        if (err) {
            return err;
        }
        err = append_param(&list, "height", SSM_SHARD_PES);
        if (err) {
            return err;
        }
        err = append_param(&list, "key_dim", key_dim);
        if (err) {
            return err;
        }
        err = append_param(&list, "value_dim", value_dim);
        if (err) {
            return err;
        }
        err = append_param(&list, "value_dim_per_pe", value_dim_per_pe);
        if (err) {
            return err;
        }
    } else if (eq(pattern, "fused_gemv_dequant")) {
        uint32_t out_dim;
        uint32_t out_dim_per_pe;
        if (csl_spatial_tool_is_lm_head_target(target_name)) {
            if (plan->pe_grid_width == 0) {
                return CSL_ERR_INVALID;
            }
            err = ceil_div_u32(config->vocab_size, plan->pe_grid_width,
                               &out_dim);
            if (err) {
                return err;
            }
        } else {
            out_dim = config->hidden_dim;
        }
        if (plan->pe_grid_height == 0) {
            return CSL_ERR_INVALID;
        }
        err = ceil_div_u32(out_dim, plan->pe_grid_height, &out_dim_per_pe);
        if (err) {
            return err;
        }
        err = append_param(&list, "width", plan->pe_grid_width);
        if (err) {
            return err;
        }
        err = append_param(&list, "height", plan->pe_grid_height);
        if (err) {
            return err;
        }
        err = append_param(&list, "out_dim", out_dim);
        if (err) {
            return err;
        }
        err = append_param(&list, "out_dim_per_pe", out_dim_per_pe);
        if (err) {
            return err;
        }
        err = append_param(&list, "in_dim_per_pe",
                           min_u32(config->hidden_dim, 512u));
        if (err) {
            return err;
        }
        err = append_param(&list, "num_blocks_per_row", 2u);
        if (err) {
            return err;
        }
    } else if (eq(pattern, "kv_write")) {
        uint32_t slots;
        if (plan->pe_grid_height == 0) {
            return CSL_ERR_INVALID;
        }
        err = ceil_div_u32(config->max_seq_len, plan->pe_grid_height,
                           &slots);
        if (err) {
            return err;
        }
        err = append_param(&list, "width", config->num_heads);
        if (err) {
            return err;
        }
        err = append_param(&list, "height", plan->pe_grid_height);
        if (err) {
            return err;
        }
        err = append_param(&list, "head_dim", config->head_dim);
        if (err) {
            return err;
        }
        err = append_param(&list, "max_seq_len", config->max_seq_len);
        if (err) {
            return err;
        }
        err = append_param(&list, "slots_per_pe", slots);
        if (err) {
            return err;
        }
    } else if (eq(pattern, "attention_tiled")) {
        uint32_t q_len_per_pe;
        if (plan->pe_grid_width == 0) {
            return CSL_ERR_INVALID;
        }
        err = ceil_div_u32(config->max_seq_len, plan->pe_grid_width,
                           &q_len_per_pe);
        if (err) {
            return err;
        }
        err = append_param(&list, "width", plan->pe_grid_width);
        if (err) {
            return err;
        }
        err = append_param(&list, "height", 1u);
        if (err) {
            return err;
        }
        err = append_param(&list, "head_dim", config->head_dim);
        if (err) {
            return err;
        }
        err = append_param(&list, "q_len", config->max_seq_len);
        if (err) {
            return err;
        }
        err = append_param(&list, "q_len_per_pe", q_len_per_pe);
        if (err) {
            return err;
        }
        err = append_param(&list, "block_size",
                           min_u32(config->max_seq_len, 16u));
        if (err) {
            return err;
        }
    } else if (eq(pattern, "attention_prefill_kv_axis_sharded")) {
        uint32_t slots;
        err = ceil_div_u32(config->max_seq_len, 512u, &slots);
        if (err) {
            return err;
        }
        err = append_param(&list, "width", 512u);
        if (err) {
            return err;
        }
        err = append_param(&list, "height", 512u);
        if (err) {
            return err;
        }
        err = append_param(&list, "kv_len", config->max_seq_len);
        if (err) {
            return err;
        }
        err = append_param(&list, "slots_per_pe", slots);
        if (err) {
            return err;
        }
    } else if (eq(pattern, "attention_decode")) {
        uint32_t kv_chunk;
        if (plan->pe_grid_width == 0) {
            return CSL_ERR_INVALID;
        }
        err = ceil_div_u32(config->max_seq_len, plan->pe_grid_width,
                           &kv_chunk);
        if (err) {
            return err;
        }
        err = append_param(&list, "width", plan->pe_grid_width);
        if (err) {
            return err;
        }
        err = append_param(&list, "height", 1u);
        if (err) {
            return err;
        }
        err = append_param(&list, "head_dim", config->head_dim);
        if (err) {
            return err;
        }
        err = append_param(&list, "kv_chunk", kv_chunk);
        if (err) {
            return err;
        }
    } else if (eq(pattern, "sample")) {
        uint32_t width;
        uint32_t chunk;
        err = ceil_div_u32(config->vocab_size, 1024u, &width);
        if (err) {
            return err;
        }
        if (width == 0) {
            return CSL_ERR_INVALID;
        }
        err = ceil_div_u32(config->vocab_size, width, &chunk);
        if (err) {
            return err;
        }
        err = append_param(&list, "width", width);
        if (err) {
            return err;
        }
        err = append_param(&list, "height", 1u);
        if (err) {
            return err;
        }
        err = append_param(&list, "chunk_size", chunk);
        if (err) {
            return err;
        }
    } else if (eq(pattern, "rope")) {
        double pairs_f = (double)config->head_dim *
                         (double)config->partial_rotary_factor / 2.0;
        uint32_t num_pairs;
        if (pairs_f < 0.0 || pairs_f > 4294967295.0) {
            return CSL_ERR_INVALID;
        }
        num_pairs = (uint32_t)(pairs_f + 0.5);
        err = append_param(&list, "width", plan->pe_grid_width);
        if (err) {
            return err;
        }
        err = append_param(&list, "head_dim", config->head_dim);
        if (err) {
            return err;
        }
        err = append_param(&list, "num_pairs", num_pairs);
        if (err) {
            return err;
        }
        if (config->has_mrope_section) {
            uint64_t sum = (uint64_t)config->mrope_section[0] +
                           (uint64_t)config->mrope_section[1] +
                           (uint64_t)config->mrope_section[2];
            if (sum != (uint64_t)num_pairs) {
                return CSL_ERR_INVALID;
            }
            err = append_param(&list, "mrope_t_pairs",
                               config->mrope_section[0]);
            if (err) {
                return err;
            }
            err = append_param(&list, "mrope_h_pairs",
                               config->mrope_section[1]);
            if (err) {
                return err;
            }
            err = append_param(&list, "mrope_w_pairs",
                               config->mrope_section[2]);
            if (err) {
                return err;
            }
        }
    } else {
        /* Unknown pattern: Zig falls through with an empty list. */
    }
    *out_count = list.count;
    return CSL_OK;
}

