// SPDX-License-Identifier: MIT
// Layout shapes follow emit_csl_layout.zig (element-wise 2-D grid,
// reduction width x 1 with reduce_color, dense GEMV c2d params); other
// patterns share the rectangle + memcpy + tile-code + export skeleton
// with pattern-specific comments/params, because their Zig bodies differ
// mainly in grid/param/export details that are descriptor inputs here.
// Per-pattern tile parameter detail derived from IR in Zig is a
// descriptor input here (see csl_kernel.h). NOT byte-identical to Zig
// for matmul/attention/kv patterns: partial.
#include "csl_layout.h"
#include "csl_emit_common.h"
#include "csl_spec.h"


static CslErr exports(CslBuf *out, const CslKernelDesc *d) {
    size_t i;
    if (!d) return CSL_ERR_INVALID;
    for (i = 0; i < d->global_count; i++) {
        const CslGlobal *g = &d->globals[i];
        if (!g->has_binding) continue;
        if (g->addr_space != CSL_ADDR_STORAGE && g->addr_space != CSL_ADDR_UNIFORM) continue;
        PUT("    @export_name(\""); PUT(g->name ? g->name : ""); PUT("\", [*]");
        PUT(csl_emit_scalar(d)); PUT(", true);\n");
    }
    PUT("    @export_name(\"compute\", fn()void);\n");
    return CSL_OK;
}
static CslErr rect_layout(CslBuf *out, const CslKernelDesc *d, const char *comment,
    const char *extra_params) {
    CslErr e;
    if (!out || !d) return CSL_ERR_INVALID;
    PUT("// Layout: "); PUT(comment); PUT("\n\n");
    PUT("param width: u16;\nparam height: u16;\n");
    if (extra_params) PUT(extra_params);
    PUT("\nconst memcpy = @import_module(\"<memcpy/get_params>\", .{\n    .width = width,\n    .height = height,\n});\n\nlayout {\n    @set_rectangle(width, height);\n\n");
    PUT("    for (@range(u16, height)) |pe_y| {\n        for (@range(u16, width)) |pe_x| {\n            @set_tile_code(pe_x, pe_y, \"pe_program.csl\", .{\n                .memcpy_params = memcpy.get_params(pe_x),\n                .width = width,\n                .height = height,\n            });\n        }\n    }\n\n");
    e = exports(out, d); if (e) return e;
    PUT("}\n");
    return CSL_OK;
}
CslErr csl_layout_element_wise(CslBuf *out, const CslKernelDesc *d) {
    /* Verbatim shape from Zig: 2-D grid comment, u16 params, tile code. */
    if (!out || !d) return CSL_ERR_INVALID;
    PUT("// Layout: element-wise kernel distributed across a width x height PE grid.\n");
    PUT("// Each PE processes a contiguous chunk of the input array.\n\n");
    PUT("// Grid width and height are set at compile time via\n");
    PUT("// --params=width:<W>,height:<H>. height defaults to 1 for 1-D layouts.\n");
    PUT("param width: u16;\nparam height: u16;\n\n");
    PUT("const memcpy = @import_module(\"<memcpy/get_params>\", .{\n    .width = width,\n    .height = height,\n});\n\nlayout {\n    @set_rectangle(width, height);\n\n");
    PUT("    for (@range(u16, height)) |pe_y| {\n        for (@range(u16, width)) |pe_x| {\n            @set_tile_code(pe_x, pe_y, \"pe_program.csl\", .{\n                .memcpy_params = memcpy.get_params(pe_x),\n                .width = width,\n                .height = height,\n            });\n        }\n    }\n\n");
    { CslErr e = exports(out, d); if (e) return e; }
    PUT("}\n");
    return CSL_OK;
}
CslErr csl_layout_reduction(CslBuf *out, const CslKernelDesc *d) {
    if (!out || !d) return CSL_ERR_INVALID;
    PUT("// Layout: single-PE reduction kernel (width x 1, no cross-PE fabric).\n\n");
    PUT("param width: i16;\nparam hidden_size: i16 = 1024;\n\n");
    PUT("const memcpy = @import_module(\"<memcpy/get_params>\", .{\n    .width = width,\n    .height = 1,\n});\n\n");
    PUT("const reduce_color: color = @get_color(4);\n\n");
    PUT("layout {\n    @set_rectangle(width, 1);\n\n");
    PUT("    for (@range(i16, width)) |pe_x| {\n        @set_tile_code(pe_x, 0, \"pe_program.csl\", .{\n            .memcpy_params = memcpy.get_params(pe_x),\n            .pe_id = pe_x,\n            .num_pes = width,\n            .reduce_color = reduce_color,\n        });\n    }\n\n");
    { CslErr e = exports(out, d); if (e) return e; }
    PUT("}\n");
    return CSL_OK;
}
CslErr csl_layout_dense_gemv(CslBuf *out, const CslKernelDesc *d) {
    /* Exact text from emit_csl_dense_gemv.zig emitLayout. */
    if (!out || !d) return CSL_ERR_INVALID;
    PUT("// Layout: dense GEMV with hidden x-shards and vocab y-shards.\n");
    PUT("// Each PE row reduces its hidden partials into the row sink.\n\n");
    PUT("param width: u16;\nparam height: u16;\nparam out_dim: i16;\nparam out_dim_per_pe: i16;\nparam in_dim_per_pe: i16;\n\n");
    PUT("const memcpy = @import_module(\"<memcpy/get_params>\", .{\n    .width = width,\n    .height = height,\n});\n\nconst c2d = @import_module(\"<collectives_2d/params>\");\n\nlayout {\n    @set_rectangle(width, height);\n\n");
    PUT("    for (@range(u16, height)) |pe_y| {\n        for (@range(u16, width)) |pe_x| {\n");
    PUT("            const c2d_tile_params = c2d.get_params(pe_x, pe_y, .{\n");
    PUT("                .x_colors      = .{ @get_color(4),         @get_color(5)         },\n");
    PUT("                .x_entrypoints = .{ @get_local_task_id(8), @get_local_task_id(9) },\n");
    PUT("                .y_colors      = .{ @get_color(6),         @get_color(7)         },\n");
    PUT("                .y_entrypoints = .{ @get_local_task_id(10), @get_local_task_id(11) },\n");
    PUT("            });\n");
    PUT("            @set_tile_code(pe_x, pe_y, \"pe_program.csl\", .{\n");
    PUT("                .memcpy_params = memcpy.get_params(pe_x),\n                .c2d_params = c2d_tile_params,\n                .pe_id = @as(i16, pe_x),\n                .pe_y = @as(i16, pe_y),\n                .num_pes = @as(i16, width),\n                .out_dim_per_pe = out_dim_per_pe,\n                .in_dim_per_pe = in_dim_per_pe,\n            });\n        }\n    }\n\n");
    PUT("    @export_name(\"activation\", [*]f16, true);\n    @export_name(\"weight\", [*]f16, true);\n    @export_name(\"output\", [*]f32, true);\n    @export_name(\"compute\", fn()void);\n}\n");
    return CSL_OK;
}
CslErr csl_layout_matmul(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "tiled matmul (SUMMA, P x P grid, row/column broadcast)", "param tile_m: u16;\nparam tile_n: u16;\nparam tile_k: u16;\n");
}
CslErr csl_layout_matmul_q4k(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "tiled matmul with Q4K-quantized B (SUMMA, dequant on PE)", "param tile_m: u16;\nparam tile_n: u16;\nparam tile_k: u16;\n");
}
CslErr csl_layout_gather(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "embedding gather: width x height shards the table by row", "param hidden_per_pe: i16;\nparam tokens_per_chunk: i16;\n");
}
CslErr csl_layout_rope(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "rotary position embeddings (cos/sin paired dims)", "param head_dim: i16;\n");
}
CslErr csl_layout_dequant(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "Q4K dequantization, PE-local super-blocks", NULL);
}
CslErr csl_layout_sample(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "token sampling: per-PE local argmax over logit chunks", NULL);
}
CslErr csl_layout_fused_gemv(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "fused GEMV + Q4K dequant, row-sink host readback", "param out_dim_per_pe: i16;\nparam in_dim_per_pe: i16;\n");
}
CslErr csl_layout_streaming_attention(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "streaming attention: one (query_pos, head) pair per PE, no fabric", NULL);
}
CslErr csl_layout_decode_attention(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "decode attention: chunked KV, fabric allreduce for online softmax", NULL);
}
CslErr csl_layout_tiled_attention(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "tiled (flash) attention: K/V tile broadcasts via collectives_2d", NULL);
}
CslErr csl_layout_linear_attention(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "linear attention: no softmax normalization, no fabric", NULL);
}
CslErr csl_layout_kv_write(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "KV cache write: append projected K/V at decode position", "param max_seq_len: i16 = 4096;\nparam slots_per_pe: i16;\n");
}
CslErr csl_layout_kv_read(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "KV cache read: slice of cached K/V per PE chunk", "param max_seq_len: i16 = 4096;\nparam slots_per_pe: i16;\n");
}
CslErr csl_layout_fused_ffn(CslBuf *out, const CslKernelDesc *d) {
    return rect_layout(out, d, "fused FFN: silu(gate) * up, collectives_2d row reduction", NULL);
}
CslErr csl_layout_emit(CslBuf *out, const CslKernelDesc *d) {
    if (!out || !d) return CSL_ERR_INVALID;
    switch (d->pattern) {
        case CSL_PATTERN_ELEMENT_WISE: return csl_layout_element_wise(out, d);
        case CSL_PATTERN_REDUCTION: return csl_layout_reduction(out, d);
        case CSL_PATTERN_TILED_MATMUL: return csl_layout_matmul(out, d);
        case CSL_PATTERN_TILED_MATMUL_Q4K: return csl_layout_matmul_q4k(out, d);
        case CSL_PATTERN_GATHER: return csl_layout_gather(out, d);
        case CSL_PATTERN_ROPE: return csl_layout_rope(out, d);
        case CSL_PATTERN_DEQUANT: return csl_layout_dequant(out, d);
        case CSL_PATTERN_SAMPLE: return csl_layout_sample(out, d);
        case CSL_PATTERN_FUSED_GEMV_DEQUANT: return csl_layout_fused_gemv(out, d);
        case CSL_PATTERN_ATTENTION_STREAMING: return csl_layout_streaming_attention(out, d);
        case CSL_PATTERN_ATTENTION_DECODE: return csl_layout_decode_attention(out, d);
        case CSL_PATTERN_ATTENTION_TILED: return csl_layout_tiled_attention(out, d);
        case CSL_PATTERN_ATTENTION_LINEAR: return csl_layout_linear_attention(out, d);
        case CSL_PATTERN_KV_WRITE: return csl_layout_kv_write(out, d);
        case CSL_PATTERN_KV_READ: return csl_layout_kv_read(out, d);
        case CSL_PATTERN_FUSED_FFN: return csl_layout_fused_ffn(out, d);
        case CSL_PATTERN_DENSE_GEMV: return csl_layout_dense_gemv(out, d);
        case CSL_PATTERN_SEMANTIC:
        case CSL_PATTERN_UNSUPPORTED: return CSL_ERR_UNSUPPORTED;
        default: return CSL_ERR_UNSUPPORTED;
    }
}
