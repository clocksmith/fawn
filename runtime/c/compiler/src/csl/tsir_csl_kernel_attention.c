// SPDX-License-Identifier: MIT
#include "tsir_csl_kernel_body.h"

#include <stdio.h>

/* Port of emit_kernel_body_attention.zig (443 lines).
 *
 * Scope, exactly as in Zig: softmax_mode must be two_pass_stable,
 * has_softcap false, scale_source literal_f32, causal_mode none /
 * causal / sliding_window. Streaming softmax, softcap, page-table
 * KV, uniform scale, a missing scale literal and a zero sliding
 * window or query_seq_len all return CSL_ERR_INVALID (Zig
 * InvalidBodyContract, mapped per tsir_csl_common.h); see the
 * validation order in tsir_csl_emit_attention_scores.
 * Single-Q convention: query_pos = kv_len - 1, so .causal is a
 * structural conditional that never fires, and .sliding_window
 * masks k < kv_len - sliding_window_size. Multi-Q (query_seq_len
 * > 1) is implemented only on the kv_axis_sharded path, as in Zig;
 * the full_per_pe path ignores query_seq_len beyond the
 * query_seq_len != 0 validation, exactly as Zig does.
 *
 * Layout-supplied CSL params (not descriptor fields): kv_len
 * (i16, also declared by emit_csl layout), slots_per_pe (i16, or
 * the config default), pe_id / num_pes from the emit_csl skeleton
 * (tile_param) or layout coordinates (layout_coordinates).
 * Host stitching for the sharded partials (log-sum-exp reduction
 * over [head_dim + 2] partials) is a host-plan job, not emitted
 * here, exactly as in Zig. */

static const char *mask_sentinel(TsirCslElem elem) {
    if (elem == TSIR_CSL_F16) {
        return "-65504.0";
    }
    return "-1.0e30";
}

/* Single-Q mask conditional, always at 8-space indent, exactly as
 * Zig emitCausalMask writes it. That means on the sharded single-Q
 * path the mask lines sit one indent deeper than the surrounding
 * 4-space loop body; this is inherited from Zig, not introduced
 * here, and indentation is not CSL semantics. */
static CslErr emit_causal_mask(CslBuf *out, TsirCslCausalMode mode,
    uint32_t window, const char *k_expr, TsirCslElem elem) {
    CslErr err = CSL_OK;
    const char *sent = mask_sentinel(elem);
    if (mode == TSIR_CSL_CAUSAL_NONE) {
        return CSL_OK;
    }
    if (mode == TSIR_CSL_CAUSAL_CAUSAL) {
        if ((err = csl_buf_puts(out,
            "        // Single-Q canary convention: query_pos = kv_len - 1.\n")) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_puts(out,
            "        // The conditional is structural; for single-Q it never fires.\n")) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_printf(out,
            "        if (%s > @as(u32, kv_len) - 1) {\n",
            k_expr)) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_printf(out, "            sc = %s;\n",
            sent)) != CSL_OK) {
            return err;
        }
        return csl_buf_puts(out, "        }\n");
    }
    if (mode == TSIR_CSL_CAUSAL_SLIDING_WINDOW) {
        if ((err = csl_buf_printf(out,
            "        const window_size: u32 = %u;\n", window)) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_printf(out,
            "        if (@as(u32, kv_len) > window_size and %s < @as(u32, kv_len) - window_size) {\n",
            k_expr)) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_printf(out, "            sc = %s;\n",
            sent)) != CSL_OK) {
            return err;
        }
        return csl_buf_puts(out, "        }\n");
    }
    return CSL_ERR_UNSUPPORTED;
}

typedef struct AttnCtx {
    const TsirCslBinding *query;
    const TsirCslBinding *key;
    const TsirCslBinding *value;
    const TsirCslBinding *output;
    TsirCslElem elem;
    const char *prefix;
    uint32_t head_dim;
    double scale;
    TsirCslCausalMode causal_mode;
    uint32_t sliding_window_size;
    uint32_t query_seq_len;
} AttnCtx;

static CslErr emit_full_per_pe(CslBuf *out, const AttnCtx *ctx) {
    CslErr err = CSL_OK;
    const char *p = ctx->prefix;
    const char *ty = tsir_csl_elem_name(ctx->elem);
    const char *sent = mask_sentinel(ctx->elem);
    char scale_text[64];
    if (ty == NULL) {
        return CSL_ERR_INVALID;
    }
    err = tsir_csl_format_scientific(scale_text, sizeof(scale_text),
        ctx->scale);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param memcpy_params;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "const head_dim: i16 = %u;\n",
        ctx->head_dim)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param kv_len: i16;\n")) != CSL_OK) {
        return err;
    }
    err = tsir_csl_write_sys_mod(out);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_write_math_import(out);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "const attn_scale: %s = %s;\n",
        ty, scale_text)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, ctx->query->name,
        "head_dim", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, ctx->key->name,
        "kv_len * head_dim", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, ctx->value->name,
        "kv_len * head_dim", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, ctx->output->name,
        "head_dim", ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "var attn_scores: [kv_len]%s = @zeros([kv_len]%s);\n",
        ty, ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, ctx->query->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, ctx->key->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, ctx->value->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, ctx->output->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "fn compute() void {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "    var max_score: %s = %s;\n",
        ty, sent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    for (@range(i16, kv_len)) |k| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "        var dot: %s = 0.0;\n",
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        for (@range(i16, head_dim)) |d| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            dot += %s%s[@as(u32, d)] * %s%s[@as(u32, k) * @as(u32, head_dim) + @as(u32, d)];\n",
        p, ctx->query->name, p, ctx->key->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "        var sc: %s = dot * attn_scale;\n",
        ty)) != CSL_OK) {
        return err;
    }
    err = emit_causal_mask(out, ctx->causal_mode,
        ctx->sliding_window_size, "@as(u32, k)", ctx->elem);
    if (err != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        attn_scores[@as(u32, k)] = sc;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        if (sc > max_score) {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "            max_score = sc;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "    var sum_exp: %s = 0.0;\n",
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    for (@range(i16, kv_len)) |k| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        const e = math.exp(attn_scores[@as(u32, k)] - max_score);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        attn_scores[@as(u32, k)] = e;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        sum_exp += e;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "    for (@range(i16, head_dim)) |d| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "        var acc: %s = 0.0;\n",
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "        for (@range(i16, kv_len)) |k| {\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "            acc += %s%s[@as(u32, k) * @as(u32, head_dim) + @as(u32, d)] * (attn_scores[@as(u32, k)] / sum_exp);\n",
        p, ctx->value->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "        }\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "        %s%s[@as(u32, d)] = acc;\n",
        p, ctx->output->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
        return err;
    }
    return tsir_csl_write_compute_tail(out, ctx->prefix,
        (const char *[]){ctx->query->name, ctx->key->name,
            ctx->value->name, ctx->output->name}, 4);
}

static CslErr emit_kv_axis_sharded(CslBuf *out, const AttnCtx *ctx,
    const TsirCslConfig *config) {
    CslErr err = CSL_OK;
    const char *p = ctx->prefix;
    const char *ty = tsir_csl_elem_name(ctx->elem);
    const char *sent = mask_sentinel(ctx->elem);
    const char *indent = NULL;
    const char *q_index = NULL;
    const char *out_d = NULL;
    const char *out_max = NULL;
    const char *out_sum = NULL;
    int multi_q = 0;
    int layout_coords = 0;
    char scale_text[64];
    if (ty == NULL) {
        return CSL_ERR_INVALID;
    }
    multi_q = ctx->query_seq_len > 1;
    layout_coords = config->attention_pe_id_source ==
        TSIR_CSL_ATTN_ID_LAYOUT_COORDINATES;
    indent = multi_q ? "        " : "    ";
    q_index = multi_q ? "q_row_offset + @as(u32, d)" : "@as(u32, d)";
    out_d = multi_q ? "out_row_offset + @as(u32, d)" : "@as(u32, d)";
    out_max = multi_q ? "out_row_offset + @as(u32, head_dim)"
        : "@as(u32, head_dim)";
    out_sum = multi_q ? "out_row_offset + @as(u32, head_dim) + 1"
        : "@as(u32, head_dim) + 1";
    err = tsir_csl_format_scientific(scale_text, sizeof(scale_text),
        ctx->scale);
    if (err != CSL_OK) {
        return err;
    }
    if (layout_coords) {
        if ((err = csl_buf_puts(out, "param width: u16;\n")) != CSL_OK) {
            return err;
        }
    }
    if ((err = csl_buf_puts(out, "param memcpy_params;\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "const head_dim: i16 = %u;\n",
        ctx->head_dim)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "param kv_len: i16;\n")) != CSL_OK) {
        return err;
    }
    if (multi_q) {
        if ((err = csl_buf_printf(out,
            "const query_seq_len: u32 = %u;\n",
            ctx->query_seq_len)) != CSL_OK) {
            return err;
        }
    }
    if (config->has_attention_slots_per_pe_default) {
        if ((err = csl_buf_printf(out,
            "param slots_per_pe: i16 = %u;\n",
            config->attention_slots_per_pe_default)) != CSL_OK) {
            return err;
        }
    } else {
        if ((err = csl_buf_puts(out,
            "param slots_per_pe: i16;\n")) != CSL_OK) {
            return err;
        }
    }
    err = tsir_csl_write_sys_mod(out);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_write_math_import(out);
    if (err != CSL_OK) {
        return err;
    }
    if (layout_coords) {
        if ((err = csl_buf_puts(out,
            "const layout_mod = @import_module(\"<layout>\");\n")) != CSL_OK) {
            return err;
        }
    }
    if ((err = csl_buf_printf(out, "const attn_scale: %s = %s;\n",
        ty, scale_text)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "const local_kv_len: u32 = @as(u32, slots_per_pe) * @as(u32, head_dim);\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out,
        "const partials_len: u32 = @as(u32, head_dim) + 2;\n")) != CSL_OK) {
        return err;
    }
    if (multi_q) {
        if ((err = tsir_csl_write_buffer_array(out, p, ctx->query->name,
            "query_seq_len * @as(u32, head_dim)", ty)) != CSL_OK) {
            return err;
        }
    } else {
        if ((err = tsir_csl_write_buffer_array(out, p, ctx->query->name,
            "head_dim", ty)) != CSL_OK) {
            return err;
        }
    }
    if ((err = tsir_csl_write_buffer_array(out, p, ctx->key->name,
        "local_kv_len", ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_array(out, p, ctx->value->name,
        "local_kv_len", ty)) != CSL_OK) {
        return err;
    }
    if (multi_q) {
        if ((err = tsir_csl_write_buffer_array(out, p, ctx->output->name,
            "query_seq_len * partials_len", ty)) != CSL_OK) {
            return err;
        }
    } else {
        if ((err = tsir_csl_write_buffer_array(out, p, ctx->output->name,
            "partials_len", ty)) != CSL_OK) {
            return err;
        }
    }
    if ((err = csl_buf_printf(out,
        "var attn_scores: [slots_per_pe]%s = @zeros([slots_per_pe]%s);\n",
        ty, ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, ctx->query->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, ctx->key->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, ctx->value->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = tsir_csl_write_buffer_pointer(out, p, ctx->output->name,
        ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "\n")) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_puts(out, "fn compute() void {\n")) != CSL_OK) {
        return err;
    }
    if (layout_coords) {
        if ((err = csl_buf_puts(out,
            "    const pe_x = layout_mod.get_x_coord();\n")) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_puts(out,
            "    const pe_y = layout_mod.get_y_coord();\n")) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_puts(out,
            "    const pe_id: u32 = @as(u32, pe_y) * @as(u32, width) + @as(u32, pe_x);\n")) != CSL_OK) {
            return err;
        }
    }
    if ((err = csl_buf_puts(out,
        "    const slot_base: u32 = @as(u32, pe_id) * @as(u32, slots_per_pe);\n")) != CSL_OK) {
        return err;
    }
    if (multi_q) {
        if ((err = csl_buf_puts(out,
            "    for (@range(u32, query_seq_len)) |q| {\n")) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_puts(out,
            "        const q_row_offset: u32 = q * @as(u32, head_dim);\n")) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_puts(out,
            "        const out_row_offset: u32 = q * partials_len;\n")) != CSL_OK) {
            return err;
        }
        if (ctx->causal_mode == TSIR_CSL_CAUSAL_CAUSAL) {
            if ((err = csl_buf_puts(out,
                "        const q_pos: u32 = @as(u32, kv_len) - query_seq_len + q;\n")) != CSL_OK) {
                return err;
            }
        }
    }
    if ((err = csl_buf_printf(out, "%svar local_max: %s = %s;\n",
        indent, ty, sent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%sfor (@range(i16, slots_per_pe)) |k| {\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%s    const gk: u32 = slot_base + @as(u32, k);\n",
        indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%s    if (gk >= @as(u32, kv_len)) {\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%s        attn_scores[@as(u32, k)] = %s;\n", indent, sent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s        continue;\n",
        indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s    }\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s    var dot: %s = 0.0;\n",
        indent, ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%s    for (@range(i16, head_dim)) |d| {\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%s        dot += %s%s[%s] * %s%s[@as(u32, k) * @as(u32, head_dim) + @as(u32, d)];\n",
        indent, p, ctx->query->name, q_index, p, ctx->key->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s    }\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s    var sc: %s = dot * attn_scale;\n",
        indent, ty)) != CSL_OK) {
        return err;
    }
    if (multi_q && ctx->causal_mode == TSIR_CSL_CAUSAL_CAUSAL) {
        if ((err = csl_buf_printf(out, "%s    if (gk > q_pos) {\n",
            indent)) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_printf(out, "%s        sc = %s;\n",
            indent, sent)) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_printf(out, "%s    }\n", indent)) != CSL_OK) {
            return err;
        }
    } else if (multi_q &&
        ctx->causal_mode == TSIR_CSL_CAUSAL_SLIDING_WINDOW) {
        if ((err = csl_buf_printf(out,
            "%s    const window_size: u32 = %u;\n", indent,
            ctx->sliding_window_size)) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_printf(out,
            "%s    if (@as(u32, kv_len) > window_size and gk < @as(u32, kv_len) - window_size) {\n",
            indent)) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_printf(out, "%s        sc = %s;\n",
            indent, sent)) != CSL_OK) {
            return err;
        }
        if ((err = csl_buf_printf(out, "%s    }\n", indent)) != CSL_OK) {
            return err;
        }
    } else if (!multi_q) {
        /* Inherited 8-space mask helper; see emit_causal_mask note. */
        err = emit_causal_mask(out, ctx->causal_mode,
            ctx->sliding_window_size, "gk", ctx->elem);
        if (err != CSL_OK) {
            return err;
        }
    }
    if ((err = csl_buf_printf(out,
        "%s    attn_scores[@as(u32, k)] = sc;\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%s    if (sc > local_max) {\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%s        local_max = sc;\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s    }\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s}\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%svar local_sum_exp: %s = 0.0;\n", indent, ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%sfor (@range(i16, slots_per_pe)) |k| {\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%s    const e = math.exp(attn_scores[@as(u32, k)] - local_max);\n",
        indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%s    attn_scores[@as(u32, k)] = e;\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s    local_sum_exp += e;\n",
        indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s}\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%sfor (@range(i16, head_dim)) |d| {\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s    var acc: %s = 0.0;\n",
        indent, ty)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%s    for (@range(i16, slots_per_pe)) |k| {\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out,
        "%s        acc += %s%s[@as(u32, k) * @as(u32, head_dim) + @as(u32, d)] * attn_scores[@as(u32, k)];\n",
        indent, p, ctx->value->name)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s    }\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s    %s%s[%s] = acc;\n",
        indent, p, ctx->output->name, out_d)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s}\n", indent)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s%s%s[%s] = local_max;\n",
        indent, p, ctx->output->name, out_max)) != CSL_OK) {
        return err;
    }
    if ((err = csl_buf_printf(out, "%s%s%s[%s] = local_sum_exp;\n",
        indent, p, ctx->output->name, out_sum)) != CSL_OK) {
        return err;
    }
    if (multi_q) {
        if ((err = csl_buf_puts(out, "    }\n")) != CSL_OK) {
            return err;
        }
    }
    return tsir_csl_write_compute_tail(out, ctx->prefix,
        (const char *[]){ctx->query->name, ctx->key->name,
            ctx->value->name, ctx->output->name}, 4);
}

CslErr tsir_csl_emit_attention_scores(const TsirCslKernelDesc *desc,
    CslBuf *out) {
    TsirCslConfig defaults;
    const TsirCslConfig *config = NULL;
    CslErr err = CSL_OK;
    AttnCtx ctx;
    if (desc == NULL || out == NULL) {
        return CSL_ERR_INVALID;
    }
    err = tsir_csl_resolve_config(desc, &defaults, &config);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_QUERY,
        &ctx.query);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_KEY, &ctx.key);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_VALUE,
        &ctx.value);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_binding_for_role(desc, TSIR_CSL_ROLE_OUTPUT,
        &ctx.output);
    if (err != CSL_OK) {
        return err;
    }
    ctx.elem = ctx.output->elem;
    err = tsir_csl_require_supported_compute_elem(ctx.elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(ctx.query, ctx.elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(ctx.key, ctx.elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(ctx.value, ctx.elem);
    if (err != CSL_OK) {
        return err;
    }
    err = tsir_csl_require_elem(ctx.output, ctx.elem);
    if (err != CSL_OK) {
        return err;
    }
    if (desc->body.attn_softmax_mode !=
        TSIR_CSL_SOFTMAX_TWO_PASS_STABLE) {
        return CSL_ERR_INVALID;
    }
    if (desc->body.attn_has_softcap) {
        return CSL_ERR_INVALID;
    }
    if (desc->body.attn_scale_source != TSIR_CSL_SCALE_LITERAL_F32) {
        return CSL_ERR_INVALID;
    }
    if (!desc->body.attn_has_scale_literal) {
        return CSL_ERR_INVALID;
    }
    if (desc->body.attn_causal_mode ==
        TSIR_CSL_CAUSAL_SLIDING_WINDOW) {
        if (!desc->body.attn_has_sliding_window_size ||
            desc->body.attn_sliding_window_size == 0) {
            return CSL_ERR_INVALID;
        }
    }
    ctx.prefix = config->var_prefix;
    ctx.head_dim = desc->body.attn_head_dim;
    ctx.scale = desc->body.attn_scale_literal;
    ctx.causal_mode = desc->body.attn_causal_mode;
    ctx.sliding_window_size = desc->body.attn_has_sliding_window_size
        ? desc->body.attn_sliding_window_size : 0;
    ctx.query_seq_len = desc->body.attn_has_query_seq_len
        ? desc->body.attn_query_seq_len : 1;
    if (ctx.query_seq_len == 0) {
        return CSL_ERR_INVALID;
    }
    if (config->attention_pe_strategy == TSIR_CSL_ATTN_FULL_PER_PE) {
        return emit_full_per_pe(out, &ctx);
    }
    if (config->attention_pe_strategy ==
        TSIR_CSL_ATTN_KV_AXIS_SHARDED) {
        return emit_kv_axis_sharded(out, &ctx, config);
    }
    return CSL_ERR_UNSUPPORTED;
}
