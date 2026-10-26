// SPDX-License-Identifier: MIT
// Op table from Zig's op inventory (kernel names as in Zig tests).
#include "csl_exec_v1.h"
#include <string.h>
static const CslOpSpec OPS[] = {
    {"embed", "gather", 1, 1}, {"gather", "gather", 1, 1},
    {"rmsnorm", "reduction", 1, 1}, {"rms_norm", "reduction", 1, 1},
    {"layernorm", "reduction", 1, 1}, {"softmax", "reduction", 1, 1},
    {"gelu", "element_wise", 1, 1}, {"silu", "element_wise", 1, 1},
    {"relu", "element_wise", 1, 1}, {"scale", "element_wise", 1, 1},
    {"bias_add", "element_wise", 1, 1}, {"residual", "element_wise", 1, 1},
    {"cast", "element_wise", 1, 1},
    {"silu_gated", "fused_ffn", 1, 1}, {"gelu_gated", "fused_ffn", 1, 1},
    {"sigmoid_gated", "fused_ffn", 1, 1}, {"ffn", "fused_ffn", 1, 1},
    {"matmul", "tiled_matmul", 1, 1}, {"matmul_q4k", "tiled_matmul_q4k_dequant_b", 1, 1},
    {"dequant", "dequant", 1, 1}, {"fused_gemv_dequant", "fused_gemv_dequant", 1, 1},
    {"prefill_q4k_gemv", "prefill_q4k_gemv", 1, 0},
    {"rope", "rope", 1, 1}, {"attention", "attention_decode", 0, 1},
    {"attention_decode", "attention_decode", 0, 1},
    {"attention_prefill", "attention_tiled", 1, 0},
    {"attention_tiled", "attention_tiled", 1, 0},
    {"attention_prefill_kv_axis_sharded", "attention_prefill_kv_axis_sharded", 1, 0},
    {"attention_linear", "attention_linear", 1, 1},
    {"linear_attention", "linear_attention", 1, 1},
    {"conv1d_depthwise", "conv1d_depthwise", 1, 1},
    {"l2_normalize", "l2_normalize", 1, 1},
    {"kv_write", "kv_write", 1, 1}, {"kv_read", "kv_read", 1, 1},
    {"kv_write_shared", "kv_write", 1, 1},
    {"sample", "sample", 0, 1}, {"sampler", "sample", 0, 1},
    {"ple_gather", "gather", 1, 1}, {"ple_project", "element_wise", 1, 1},
    {"ple_norm", "reduction", 1, 1}, {"ple_modulate", "element_wise", 1, 1},
    {"attention_sliding", "attention_decode", 0, 1},
    {"lm_head", "dense_gemv", 1, 1},
};
const CslOpSpec *csl_op_to_spec(const char *op) {
    size_t i; if (!op) return NULL;
    for (i = 0; i < sizeof(OPS) / sizeof(OPS[0]); i++)
        if (strcmp(op, OPS[i].op) == 0) return &OPS[i];
    return NULL;
}
const char *csl_op_to_pattern(const char *op) {
    const CslOpSpec *s = csl_op_to_spec(op);
    return s ? s->pattern : NULL;
}
int csl_is_paired_gate_op(const char *op) {
    /* Exact Zig set: a paired-gate step must carry two inputs (see
       Zig's MalformedStep check on inputs_from length). */
    return op && (strcmp(op, "silu_gated") == 0 || strcmp(op, "sigmoid_gated") == 0 ||
        strcmp(op, "gelu_gated") == 0 || strcmp(op, "o_gate") == 0);
}
int csl_exec_phase_from_name(const char *name, CslExecPhase *out) {
    if (!name || !out) return 0;
    if (strcmp(name, "prefill") == 0) { *out = CSL_PHASE_PREFILL; return 1; }
    if (strcmp(name, "decode") == 0) { *out = CSL_PHASE_DECODE; return 1; }
    return 0;
}
CslErr csl_lower_steps_to_host_plan(const CslExecStep *steps, size_t count, uint32_t grid_w, uint32_t grid_h, uint32_t eos_token_id, int has_eos, CslHostPlan *out_plan, CslKernelSpec *kernels, size_t kernel_cap, CslLaunchSpec *prefill, size_t prefill_cap, CslLaunchSpec *decode, size_t decode_cap) {
    size_t i, kc = 0, pc = 0, dc = 0;
    if (!steps || !out_plan || !kernels || !prefill || !decode) return CSL_ERR_INVALID;
    for (i = 0; i < count; i++) {
        const CslExecStep *s = &steps[i];
        const CslOpSpec *spec = csl_op_to_spec(s->op);
        CslExecPhase phase;
        if (!spec) return CSL_ERR_UNSUPPORTED;
        if (!csl_exec_phase_from_name(s->phase, &phase)) return CSL_ERR_INVALID;
        if (phase == CSL_PHASE_PREFILL && !spec->prefill_ok) return CSL_ERR_UNSUPPORTED;
        if (phase == CSL_PHASE_DECODE && !spec->decode_ok) return CSL_ERR_UNSUPPORTED;
        if (kc >= kernel_cap) return CSL_ERR_TOO_LARGE;
        kernels[kc].name = s->kernel_key ? s->kernel_key : s->op;
        kernels[kc].pattern = spec->pattern; kernels[kc].count = 1; kc += 1;
        if (phase == CSL_PHASE_PREFILL) {
            if (pc >= prefill_cap) return CSL_ERR_TOO_LARGE;
            prefill[pc].kernel_name = s->kernel_key ? s->kernel_key : s->op;
            prefill[pc].repeat = s->repeat ? s->repeat : 1;
            prefill[pc].has_attention_type = s->attention_type != NULL;
            prefill[pc].attention_type = (s->attention_type && strcmp(s->attention_type, "sliding") == 0) ? CSL_ATTN_SLIDING : CSL_ATTN_GLOBAL;
            prefill[pc].has_sliding_window_size = s->has_sliding_window_size;
            prefill[pc].sliding_window_size = s->sliding_window_size;
            prefill[pc].has_current_pos_source = 0; prefill[pc].kv_cache_alias = s->kv_cache_alias;
            pc += 1;
        } else {
            if (dc >= decode_cap) return CSL_ERR_TOO_LARGE;
            decode[dc].kernel_name = s->kernel_key ? s->kernel_key : s->op;
            decode[dc].repeat = s->repeat ? s->repeat : 1;
            decode[dc].has_attention_type = s->attention_type != NULL;
            decode[dc].attention_type = (s->attention_type && strcmp(s->attention_type, "sliding") == 0) ? CSL_ATTN_SLIDING : CSL_ATTN_GLOBAL;
            decode[dc].has_sliding_window_size = s->has_sliding_window_size;
            decode[dc].sliding_window_size = s->sliding_window_size;
            decode[dc].has_current_pos_source = 0; decode[dc].kv_cache_alias = s->kv_cache_alias;
            dc += 1;
        }
    }
    out_plan->pe_grid_width = grid_w; out_plan->pe_grid_height = grid_h;
    out_plan->kernels = kernels; out_plan->kernel_count = kc;
    out_plan->prefill_launches = prefill; out_plan->prefill_count = pc;
    out_plan->decode_launches = decode; out_plan->decode_count = dc;
    out_plan->has_eos_token_id = has_eos; out_plan->eos_token_id = eos_token_id;
    return CSL_OK;
}
CslErr csl_lower_json_to_host_plan(const char *json) {
    (void)json; return CSL_ERR_UNSUPPORTED;
}
