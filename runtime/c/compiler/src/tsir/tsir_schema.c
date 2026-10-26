/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_schema.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *tsir_status_name(TsirStatus status) {
    switch (status) {
        case TSIR_OK: return "ok";
        case TSIR_ERR_NOMEM: return "out_of_memory";
        case TSIR_ERR_INVALID: return "invalid";
        case TSIR_ERR_UNSUPPORTED: return "unsupported";
        case TSIR_ERR_OVERFLOW: return "overflow";
        case TSIR_ERR_RANGE: return "out_of_range";
        case TSIR_ERR_REJECTED: return "rejected";
        default: return NULL;
    }
}

static const char *lookup(const char *const *table, size_t count, size_t value) {
    if (value >= count) {
        return NULL;
    }
    return table[value];
}

const char *tsir_exactness_class_name(TsirExactnessClass value) {
    static const char *const table[] = {
        "bit_exact_solo", "algorithm_exact", "tolerance_bounded"
    };
    return lookup(table, 3, (size_t)value);
}

const char *tsir_invariant_name(TsirAlgorithmExactInvariant value) {
    static const char *const table[] = {
        "reduction_order", "tree_shape", "accum_dtype", "associativity_grouping"
    };
    return lookup(table, 4, (size_t)value);
}

const char *tsir_rejection_reason_name(TsirRejectionReason value) {
    static const char *const table[] = {
        "tsir_binding_type_unlowerable", "tsir_subgroup_unlowerable",
        "tsir_pe_budget_exhausted", "tsir_collective_not_representable",
        "tsir_dependence_unanalyzable", "tsir_source_not_affine",
        "tsir_target_unfit"
    };
    return lookup(table, 7, (size_t)value);
}

const char *tsir_family_hint_name(TsirKernelFamilyHint value) {
    static const char *const table[] = {
        "unknown", "elementwise", "reduction", "gather", "rms_norm",
        "fused_gemv", "prefill_q4k_gemv", "tiled_matmul", "attention_decode",
        "attention_tiled", "rope", "dequant", "sample", "embed"
    };
    return lookup(table, 14, (size_t)value);
}

const char *tsir_scalar_name(TsirScalarKind value) {
    static const char *const table[] = { "f32", "f16", "bf16", "i32", "u32" };
    return lookup(table, 5, (size_t)value);
}

const char *tsir_associativity_name(TsirReductionAssociativity value) {
    static const char *const table[] = { "strict_ordered", "associative_allowed" };
    return lookup(table, 2, (size_t)value);
}

const char *tsir_nan_inf_name(TsirNanInfPolicy value) {
    static const char *const table[] = { "propagate", "quiet_mask" };
    return lookup(table, 2, (size_t)value);
}

const char *tsir_reduction_op_name(TsirReductionOp value) {
    static const char *const table[] = {
        "sum", "product", "min", "max", "softmax_stable"
    };
    return lookup(table, 5, (size_t)value);
}

const char *tsir_collective_kind_name(TsirCollectiveKind value) {
    static const char *const table[] = {
        "subgroup_add", "subgroup_min", "subgroup_max", "subgroup_mul",
        "subgroup_broadcast", "subgroup_ballot", "subgroup_shuffle",
        "subgroup_exclusive_scan", "subgroup_inclusive_scan",
        "workgroup_barrier", "fabric_reduce", "fabric_broadcast",
        "fabric_allreduce"
    };
    return lookup(table, 13, (size_t)value);
}

const char *tsir_tree_shape_name(TsirReductionTreeShape value) {
    static const char *const table[] = { "binomial", "ring", "linear" };
    return lookup(table, 3, (size_t)value);
}

const char *tsir_residency_class_name(TsirResidencyClass value) {
    static const char *const table[] = {
        "pe_replicated", "pe_sliced", "fabric_streamed", "host_copied",
        "recomputed"
    };
    return lookup(table, 5, (size_t)value);
}

const char *tsir_body_op_name(TsirSemanticBodyOp value) {
    static const char *const table[] = {
        "unknown", "fused_gemv", "rms_norm", "gather", "residual_add",
        "gelu_gated", "silu_gated", "sigmoid_gated", "kv_write", "kv_read",
        "attention_scores", "conv1d_depthwise", "l2_normalize",
        "linear_attention"
    };
    return lookup(table, 14, (size_t)value);
}

const char *tsir_binding_role_name(TsirSemanticBindingRole value) {
    static const char *const table[] = {
        "matrix", "vector", "input", "scale", "indices", "table", "output",
        "query", "key", "value", "kv_len_buffer", "page_table",
        "summand_a", "summand_b", "gate", "key_projection",
        "value_projection", "key_cache", "value_cache", "decode_position",
        "key_output", "value_output", "weight", "bias", "linear_state"
    };
    return lookup(table, 25, (size_t)value);
}

const char *tsir_axis_role_name(TsirSemanticAxisRole value) {
    static const char *const table[] = {
        "output", "reduction", "hidden", "token", "head", "query_sequence",
        "key_sequence"
    };
    return lookup(table, 7, (size_t)value);
}

const char *tsir_rms_norm_formula_name(TsirRmsNormFormula value) {
    static const char *const table[] = {
        "sum_squares_mean_epsilon_rsqrt_scale"
    };
    return lookup(table, 1, (size_t)value);
}

const char *tsir_rms_norm_target_name(TsirRmsNormReductionTarget value) {
    static const char *const table[] = { "intermediate_scalar" };
    return lookup(table, 1, (size_t)value);
}

const char *tsir_epsilon_source_name(TsirRmsNormEpsilonSource value) {
    static const char *const table[] = { "uniform_field", "literal_f32" };
    return lookup(table, 2, (size_t)value);
}

const char *tsir_softmax_mode_name(TsirSoftmaxMode value) {
    static const char *const table[] = { "two_pass_stable", "streaming_online" };
    return lookup(table, 2, (size_t)value);
}

const char *tsir_causal_mode_name(TsirCausalMode value) {
    static const char *const table[] = { "none", "causal", "sliding_window" };
    return lookup(table, 3, (size_t)value);
}

const char *tsir_scale_source_name(TsirAttentionScaleSource value) {
    static const char *const table[] = { "uniform_field", "literal_f32" };
    return lookup(table, 2, (size_t)value);
}

const char *tsir_linear_norm_mode_name(TsirLinearAttentionNormMode value) {
    static const char *const table[] = { "shared", "per_head" };
    return lookup(table, 2, (size_t)value);
}

uint8_t tsir_scalar_byte_size(TsirScalarKind kind) {
    switch (kind) {
        case TSIR_SCALAR_F32:
        case TSIR_SCALAR_I32:
        case TSIR_SCALAR_U32:
            return 4;
        case TSIR_SCALAR_F16:
        case TSIR_SCALAR_BF16:
            return 2;
        default:
            return 0;
    }
}

bool tsir_exactness_is_well_formed(const TsirExactness *exactness) {
    const char *name = NULL;
    size_t i = 0;
    if (exactness == NULL) {
        return false;
    }
    if (tsir_exactness_class_name(exactness->class) == NULL) {
        return false;
    }
    if (exactness->invariant_count > 0 && exactness->invariants == NULL) {
        return false;
    }
    for (i = 0; i < exactness->invariant_count; i++) {
        if (tsir_invariant_name(exactness->invariants[i]) == NULL) {
            return false;
        }
    }
    if (exactness->tolerance_metric == NULL) {
        return false;
    }
    name = exactness->tolerance_metric;
    switch (exactness->class) {
        case TSIR_EXACTNESS_BIT_EXACT_SOLO:
            return exactness->invariant_count == 0 && tsir_text_length(name, exactness->tolerance_metric_length) == 0 &&
                exactness->tolerance_epsilon == 0.0;
        case TSIR_EXACTNESS_ALGORITHM_EXACT:
            return exactness->invariant_count > 0 && tsir_text_length(name, exactness->tolerance_metric_length) == 0 &&
                exactness->tolerance_epsilon == 0.0;
        case TSIR_EXACTNESS_TOLERANCE_BOUNDED:
            return exactness->invariant_count == 0 && tsir_text_length(name, exactness->tolerance_metric_length) != 0 &&
                isfinite(exactness->tolerance_epsilon);
        default:
            return false;
    }
}

void tsir_buf_init(TsirBuffer *buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->err = TSIR_OK;
}

void tsir_buf_free(TsirBuffer *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->err = TSIR_OK;
}

static void buf_reserve(TsirBuffer *buf, size_t extra) {
    size_t needed = 0;
    size_t next = 0;
    char *grown = NULL;
    if (buf->err != TSIR_OK) {
        return;
    }
    if (extra > SIZE_MAX - buf->len - 1) {
        buf->err = TSIR_ERR_OVERFLOW;
        return;
    }
    needed = buf->len + extra + 1;
    if (needed <= buf->cap) {
        return;
    }
    next = buf->cap == 0 ? 256 : buf->cap;
    while (next < needed) {
        if (next > SIZE_MAX / 2) {
            next = needed;
            break;
        }
        next *= 2;
    }
    grown = (char *)realloc(buf->data, next);
    if (grown == NULL) {
        buf->err = TSIR_ERR_NOMEM;
        return;
    }
    buf->data = grown;
    buf->cap = next;
}

void tsir_buf_putc(TsirBuffer *buf, char c) {
    buf_reserve(buf, 1);
    if (buf->err != TSIR_OK) {
        return;
    }
    buf->data[buf->len] = c;
    buf->len += 1;
    buf->data[buf->len] = '\0';
}

void tsir_buf_putn(TsirBuffer *buf, const char *text, size_t len) {
    if (text == NULL && len != 0) {
        if (buf->err == TSIR_OK) {
            buf->err = TSIR_ERR_INVALID;
        }
        return;
    }
    buf_reserve(buf, len);
    if (buf->err != TSIR_OK) {
        return;
    }
    if (len > 0) {
        memcpy(buf->data + buf->len, text, len);
    }
    buf->len += len;
    buf->data[buf->len] = '\0';
}

void tsir_buf_puts(TsirBuffer *buf, const char *text) {
    if (text == NULL) {
        if (buf->err == TSIR_OK) {
            buf->err = TSIR_ERR_INVALID;
        }
        return;
    }
    tsir_buf_putn(buf, text, strlen(text));
}
