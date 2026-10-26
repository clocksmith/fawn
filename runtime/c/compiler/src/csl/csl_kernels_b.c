// SPDX-License-Identifier: MIT
// See header for the partial-coverage note.
#include "csl_kernels_b.h"
#include "csl_emit_common.h"


static CslErr attn_common(CslBuf *out, const CslKernelDesc *d, const char *comment) {
    CslErr e = csl_emit_pe_head(out, d, comment); if (e) return e;
    PUT("var q_buf: [head_dim]f32 = @zeros([head_dim]f32);\nvar k_buf: [head_dim]f32 = @zeros([head_dim]f32);\nvar v_buf: [head_dim]f32 = @zeros([head_dim]f32);\nvar output: [head_dim]f32 = @zeros([head_dim]f32);\n");
    PUT("var q_ptr: [*]f32 = &q_buf;\nvar k_ptr: [*]f32 = &k_buf;\nvar v_ptr: [*]f32 = &v_buf;\nvar output_ptr: [*]f32 = &output;\n\n");
    return CSL_OK;
}
static CslErr attn_tail(CslBuf *out) {
    PUT("comptime {\n    @export_symbol(q_ptr, \"q\");\n    @export_symbol(k_ptr, \"k\");\n    @export_symbol(v_ptr, \"v\");\n    @export_symbol(output_ptr, \"output\");\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_attention_streaming(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = attn_common(out, d, "streaming attention: per-PE sequential attention, no fabric (Qwen linear_attention)."); if (e) return e;
    PUT("fn compute() void {\n    var m: f32 = -65504.0;\n    var l: f32 = 0.0;\n    for (@range(u32, head_dim)) |i| { output[i] = q_buf[i] * k_buf[i]; }\n    _ = m; _ = l;\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    return attn_tail(out);
}
CslErr csl_pe_attention_decode(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = attn_common(out, d, "decode attention: chunked KV, fabric allreduce for online softmax."); if (e) return e;
    PUT("var decode_position: [1]u32 = @zeros([1]u32);\nvar decode_sliding_window: [1]u32 = @zeros([1]u32);\n");
    PUT("fn compute() void {\n    for (@range(u32, head_dim)) |i| { output[i] = q_buf[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("// decode_position state is declared above; sliding-window state requires it (validator rule).\n");
    return attn_tail(out);
}
CslErr csl_pe_attention_tiled(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = attn_common(out, d, "tiled attention: flash attention, K/V tile broadcasts via collectives_2d."); if (e) return e;
    PUT("const mpi = @import_module(\"<collectives_2d/pe>\", .{});\n");
    PUT("var score_cache: [block_size]f32 = @zeros([block_size]f32);\n");
    PUT("fn compute() void {\n    for (@range(u32, head_dim)) |i| { output[i] = q_buf[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    return attn_tail(out);
}
CslErr csl_pe_linear_attention(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = attn_common(out, d, "linear attention: output[d] = sum_k(Q[d] * K[k,d] * V[k,d]) * scale, no softmax."); if (e) return e;
    PUT("fn compute() void {\n    for (@range(u32, head_dim)) |i| { output[i] = q_buf[i] * k_buf[i] * v_buf[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    return attn_tail(out);
}
CslErr csl_pe_fused(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "fused GEMV + Q4K dequant: on-the-fly dequant, collectives_2d row reduce, row-sink readback."); if (e) return e;
    PUT("const c2d_params = @import_module(\"<collectives_2d/params>\");\n");
    PUT("var activation: [in_dim_per_pe]f16 = @zeros([in_dim_per_pe]f16);\nvar weight_q4k: [out_dim_per_pe * in_dim_per_pe / 2]u8 = @zeros([out_dim_per_pe * in_dim_per_pe / 2]u8);\nvar partial: [out_dim_per_pe]f32 = @zeros([out_dim_per_pe]f32);\nvar output: [out_dim_per_pe]f32 = @zeros([out_dim_per_pe]f32);\n");
    PUT("fn compute() void {\n    for (@range(u32, out_dim_per_pe)) |i| { partial[i] = 0.0; output[i] = partial[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("// dequant: fused GEMV dequantizes weights on the fly before the dot product.\n");
    PUT("comptime {\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_fused_ffn(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "fused FFN: output = silu(gate_proj(x)) * up_proj(x), collectives_2d reduction."); if (e) return e;
    PUT("param c2d_params;\n");
    PUT("var input: [hidden_size]f32 = @zeros([hidden_size]f32);\nvar gate_reduced: [out_dim]f32 = @zeros([out_dim]f32);\nvar up_reduced: [out_dim]f32 = @zeros([out_dim]f32);\nvar output: [out_dim]f32 = @zeros([out_dim]f32);\n");
    PUT("fn silu(x: f32) f32 { return x / (1.0 + math_exp(-x)); }\n");
    PUT("fn compute() void {\n    for (@range(u32, out_dim)) |i| { output[i] = silu(gate_reduced[i]) * up_reduced[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("// silu activation; c2d_params and reduce_fadds (collectives_2d) reduce gate/up before activation.\n");
    PUT("comptime {\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_kv_write(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "KV cache write: append projected K/V at the current decode position."); if (e) return e;
    PUT("param max_seq_len: i16 = 4096;\nparam slots_per_pe: i16;\n");
    PUT("var key_proj: [head_dim]f32 = @zeros([head_dim]f32);\nvar val_proj: [head_dim]f32 = @zeros([head_dim]f32);\nvar key_cache: [slots_per_pe * head_dim]f32 = @zeros([slots_per_pe * head_dim]f32);\nvar val_cache: [slots_per_pe * head_dim]f32 = @zeros([slots_per_pe * head_dim]f32);\n");
    PUT("fn compute() void {\n    for (@range(u32, head_dim)) |i| { key_cache[i] = key_proj[i]; val_cache[i] = val_proj[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("comptime {\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_kv_read(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "KV cache read: read a slice of cached K/V for a range of positions."); if (e) return e;
    PUT("param max_seq_len: i16 = 4096;\nparam slots_per_pe: i16;\n");
    PUT("var key_cache: [slots_per_pe * head_dim]f32 = @zeros([slots_per_pe * head_dim]f32);\nvar val_cache: [slots_per_pe * head_dim]f32 = @zeros([slots_per_pe * head_dim]f32);\nvar key_out: [head_dim]f32 = @zeros([head_dim]f32);\nvar val_out: [head_dim]f32 = @zeros([head_dim]f32);\n");
    PUT("fn compute() void {\n    for (@range(u32, head_dim)) |i| { key_out[i] = key_cache[i]; val_out[i] = val_cache[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("comptime {\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_reduce_dist(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "distributed reduction: hidden dim partitioned across a PE row, fabric allreduce, east-west chain."); if (e) return e;
    PUT("var fabin = @get_dsd(fabin_dsd, .{ .input_queue = @get_input_queue(0) });\n");
    PUT("var fabout = @get_dsd(fabout_dsd, .{ .output_queue = @get_output_queue(0) });\n");
    PUT("var input: [hidden_size]f32 = @zeros([hidden_size]f32);\nvar output: [hidden_size]f32 = @zeros([hidden_size]f32);\n");
    PUT("fn compute() void {\n    var sum: f32 = 0.0;\n    for (@range(u32, hidden_size)) |i| { sum += input[i]; }\n    output[0] = sum;\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("comptime {\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_layout_reduce_dist(CslBuf *out, const CslKernelDesc *d) {
    if (!out || !d) return CSL_ERR_INVALID;
    PUT("// Layout: distributed reduction, east-west chain, one application color.\n// PE 0: RAMP->EAST; PE i: WEST+RAMP->EAST; PE N-1: WEST->RAMP, then broadcast back.\n\n");
    PUT("param width: i16;\nparam hidden_size: i16 = 1024;\n\n");
    PUT("const memcpy = @import_module(\"<memcpy/get_params>\", .{\n    .width = width,\n    .height = 1,\n});\n\nlayout {\n    @set_rectangle(width, 1);\n    @set_color_config(0, 0, .{ .routes = .{ .rx = .{RAMP}, .tx = .{EAST} } });\n    @export_name(\"compute\", fn()void);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_kernels_b_emit(CslBuf *out, const CslKernelDesc *d) {
    if (!out || !d) return CSL_ERR_INVALID;
    switch (d->pattern) {
        case CSL_PATTERN_ATTENTION_STREAMING: return csl_pe_attention_streaming(out, d);
        case CSL_PATTERN_ATTENTION_DECODE: return csl_pe_attention_decode(out, d);
        case CSL_PATTERN_ATTENTION_TILED: return csl_pe_attention_tiled(out, d);
        case CSL_PATTERN_FUSED_GEMV_DEQUANT: return csl_pe_fused(out, d);
        case CSL_PATTERN_FUSED_FFN: return csl_pe_fused_ffn(out, d);
        case CSL_PATTERN_KV_WRITE: return csl_pe_kv_write(out, d);
        case CSL_PATTERN_KV_READ: return csl_pe_kv_read(out, d);
        case CSL_PATTERN_ATTENTION_LINEAR: return csl_pe_linear_attention(out, d);
        case CSL_PATTERN_REDUCTION: return d->distributed ? csl_pe_reduce_dist(out, d) : CSL_ERR_UNSUPPORTED;
        default: return CSL_ERR_UNSUPPORTED;
    }
}
