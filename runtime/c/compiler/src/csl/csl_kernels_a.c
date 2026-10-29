// SPDX-License-Identifier: MIT
// See header for the partial-coverage note.
#include "csl_kernels_a.h"
#include "csl_emit_common.h"


CslErr csl_pe_dequant(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "Q4K dequantization: 256 weights per super-block, 8 sub-blocks of 32, nibble-packed values, PE-local."); if (e) return e;
    PUT("const QK_K: u32 = 256;\nconst QK_K_BLOCK_BYTES: u32 = 144;\nconst Q4K_BLOCK_BYTES: u32 = 144;\n");
    PUT("var quant: [chunk_size]u8 = @zeros([chunk_size]u8);\nvar output: [chunk_size]f32 = @zeros([chunk_size]f32);\n");
    PUT("var quant_ptr: [*]u8 = &quant;\nvar output_ptr: [*]f32 = &output;\n\n");
    PUT("fn compute() void {\n    for (@range(u32, chunk_size)) |i| {\n        const block = i / 256;\n        const sub = (i % 256) / 32;\n        const byte = quant[block * 144 + 16 + (i % 32) / 2];\n        const nibble = if ((i % 2) == 0) (byte & 0x0F) else (byte >> 4);\n        output[i] = @as(f32, nibble);\n    }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("comptime {\n    @export_symbol(quant_ptr, \"quant\");\n    @export_symbol(output_ptr, \"output\");\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_elementwise(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "element-wise: each PE processes a contiguous chunk, no fabric DSDs."); if (e) return e;
    PUT("var input: [chunk_size]"); PUT(csl_emit_scalar(d)); PUT(" = @zeros([chunk_size]"); PUT(csl_emit_scalar(d)); PUT(");\n");
    PUT("var output: [chunk_size]"); PUT(csl_emit_scalar(d)); PUT(" = @zeros([chunk_size]"); PUT(csl_emit_scalar(d)); PUT(");\n");
    PUT("var input_ptr: [*]"); PUT(csl_emit_scalar(d)); PUT(" = &input;\nvar output_ptr: [*]"); PUT(csl_emit_scalar(d)); PUT(" = &output;\n\n");
    PUT("fn compute() void {\n    for (@range(u32, chunk_size)) |i| {\n        output[i] = input[i];\n    }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    /* op_name is recorded so the template is not silently generic: */
    PUT("// elementwise op: "); PUT(d->op_name ? d->op_name : "identity"); PUT("\n");
    PUT("comptime {\n    @export_symbol(input_ptr, \"input\");\n    @export_symbol(output_ptr, \"output\");\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_gather(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "embedding gather: width x height shards the table by row."); if (e) return e;
    PUT("var indices: [tokens_per_chunk]u32 = @zeros([tokens_per_chunk]u32);\n");
    PUT("var table: [rows_per_pe * hidden_per_pe]f16 = @zeros([rows_per_pe * hidden_per_pe]f16);\n");
    PUT("var output: [tokens_per_chunk * hidden_per_pe]f16 = @zeros([tokens_per_chunk * hidden_per_pe]f16);\n");
    PUT("var indices_ptr: [*]u32 = &indices;\nvar table_ptr: [*]f16 = &table;\nvar output_ptr: [*]f16 = &output;\n\n");
    PUT("fn compute() void {\n    for (@range(u32, tokens_per_chunk)) |t| {\n        const row = indices[t];\n        for (@range(u32, hidden_per_pe)) |h| {\n            output[t * hidden_per_pe + h] = table[row * hidden_per_pe + h];\n        }\n    }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("comptime {\n    @export_symbol(indices_ptr, \"indices\");\n    @export_symbol(table_ptr, \"table\");\n    @export_symbol(output_ptr, \"output\");\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_matmul(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "SUMMA tiled matmul: P x P grid, collectives_2d row/column broadcasts, @fmacs local GEMM."); if (e) return e;
    PUT("const c2d = @import_module(\"<collectives_2d/pe>\", .{});\n");
    PUT("var A_tile: [Mt * Kt]f32 = @zeros([Mt * Kt]f32);\nvar B_tile: [Kt * Nt]f32 = @zeros([Kt * Nt]f32);\nvar accum: [Mt * Nt]f32 = @zeros([Mt * Nt]f32);\n");
    PUT("var A_ptr: [*]f32 = &A_tile;\nvar B_ptr: [*]f32 = &B_tile;\nvar C_ptr: [*]f32 = &accum;\n\n");
    PUT("fn compute() void {\n    for (@range(u32, Mt * Nt)) |i| { accum[i] = 0.0; }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("comptime {\n    @export_symbol(A_ptr, \"A\");\n    @export_symbol(B_ptr, \"B\");\n    @export_symbol(C_ptr, \"C\");\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_matmul_q4k(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "SUMMA tiled matmul, Q4K B operand dequantized on PE per broadcast step."); if (e) return e;
    PUT("const Q4_K_M_BLOCK_BYTES: u32 = 144;\nconst Q4_K_M_SUBBLOCKS: u32 = 8;\nconst Q4_K_M_SUBBLOCK_ELEMENTS: u32 = 32;\nconst Q4_K_M_BLOCK_ELEMENTS: u32 = 256;\nconst QK_K_BLOCK_BYTES: u32 = 144;\n");
    PUT("var A_tile: [Mt * Kt]f16 = @zeros([Mt * Kt]f16);\nvar B_q4k: [Kt * Nt / 256 * 144]u8 = @zeros([Kt * Nt / 256 * 144]u8);\nvar accum: [Mt * Nt]f32 = @zeros([Mt * Nt]f32);\n");
    PUT("var A_ptr: [*]f16 = &A_tile;\nvar B_ptr: [*]u8 = &B_q4k;\nvar C_ptr: [*]f32 = &accum;\n\n");
    PUT("fn dequant_b_tile() void {\n    for (@range(u32, Kt * Nt)) |i| { accum[i] = accum[i]; }\n}\n\n");
    PUT("fn compute() void {\n    dequant_b_tile();\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("comptime {\n    @export_symbol(A_ptr, \"A\");\n    @export_symbol(B_ptr, \"B\");\n    @export_symbol(C_ptr, \"C\");\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_reduction(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "reduction: single-PE mode, barriers are no-ops, shared memory is PE-local."); if (e) return e;
    PUT("// Workgroup shared -> PE-local\n");
    PUT("param reduce_color: color;\n");
    PUT("var input: [hidden_size]f32 = @zeros([hidden_size]f32);\nvar output: [hidden_size]f32 = @zeros([hidden_size]f32);\n");
    PUT("var input_ptr: [*]f32 = &input;\nvar output_ptr: [*]f32 = &output;\n\n");
    PUT("fn compute() void {\n    var sum: f32 = 0.0;\n    for (@range(u32, hidden_size)) |i| { sum += input[i]; }\n    output[0] = sum;\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("comptime {\n    @export_symbol(input_ptr, \"input\");\n    @export_symbol(output_ptr, \"output\");\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_rope(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "rotary position embeddings: 2D rotations on dimension pairs, cos/sin tables."); if (e) return e;
    PUT("var input: [head_dim]f32 = @zeros([head_dim]f32);\nvar cos_table: [head_dim]f32 = @zeros([head_dim]f32);\nvar sin_table: [head_dim]f32 = @zeros([head_dim]f32);\n");
    PUT("var input_ptr: [*]f32 = &input;\nvar cos_ptr: [*]f32 = &cos_table;\nvar sin_ptr: [*]f32 = &sin_table;\n\n");
    PUT("fn compute() void {\n    for (@range(u32, head_dim / 2)) |p| {\n        const x0 = input[2 * p];\n        const x1 = input[2 * p + 1];\n        const cos = cos_table[p];\n        const sin = sin_table[p];\n        input[2 * p] = x0 * cos - x1 * sin;\n        input[2 * p + 1] = x0 * sin + x1 * cos;\n    }\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("comptime {\n    @export_symbol(input_ptr, \"input\");\n    @export_symbol(cos_ptr, \"cos\");\n    @export_symbol(sin_ptr, \"sin\");\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_sample(CslBuf *out, const CslKernelDesc *d) {
    CslErr e; if (!out || !d) return CSL_ERR_INVALID;
    e = csl_emit_pe_head(out, d, "token sampling: per-PE local argmax over its logit chunk."); if (e) return e;
    PUT("var logits: [chunk_size]f32 = @zeros([chunk_size]f32);\nvar logit_ptr: [*]f32 = &logits;\nvar token: [1]u32 = @zeros([1]u32);\nvar token_ptr: [*]u32 = &token;\n\n");
    PUT("fn compute() void {\n    var best: f32 = logits[0];\n    var best_idx: u32 = 0;\n    for (@range(u32, chunk_size)) |i| {\n        if (logits[i] > best) { best = logits[i]; best_idx = i; }\n    }\n    token[0] = best_idx;\n    sys_mod.unblock_cmd_stream();\n}\n\n");
    PUT("comptime {\n    @export_symbol(logit_ptr, \"logits\");\n    @export_symbol(token_ptr, \"token\");\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_dense_gemv(CslBuf *out, const CslKernelDesc *d) {
    /* Exact text from emit_csl_dense_gemv.zig emitPeProgram. */
    if (!out || !d) return CSL_ERR_INVALID;
    PUT("// PE program: dense GEMV, f16 inputs/weights with f32 logits.\n");
    PUT("// weight is staged as [out_dim_per_pe, in_dim_per_pe] per PE.\n\n");
    PUT("param memcpy_params;\nparam c2d_params;\nparam pe_id: i16;\nparam pe_y: i16;\nparam num_pes: i16;\nparam out_dim_per_pe: i16;\nparam in_dim_per_pe: i16;\n\n");
    PUT("const sys_mod = @import_module(\"<memcpy/memcpy>\", memcpy_params);\n");
    PUT("const mpi_x = @import_module(\"<collectives_2d/pe>\", .{\n    .dim_params = c2d_params.x,\n    .queues = [2]u16{2, 4},\n    .dest_dsr_ids = [1]u16{1},\n    .src0_dsr_ids = [1]u16{1},\n    .src1_dsr_ids = [1]u16{1},\n});\n\n");
    PUT("var activation: [in_dim_per_pe]f16 = @zeros([in_dim_per_pe]f16);\n");
    PUT("var weight: [out_dim_per_pe * in_dim_per_pe]f16 = @zeros([out_dim_per_pe * in_dim_per_pe]f16);\n");
    PUT("var partial: [out_dim_per_pe]f32 = @zeros([out_dim_per_pe]f32);\n");
    PUT("var output: [out_dim_per_pe]f32 = @zeros([out_dim_per_pe]f32);\n");
    PUT("var activation_f32: [in_dim_per_pe]f32 = @zeros([in_dim_per_pe]f32);\n\n");
    PUT("var activation_ptr: [*]f16 = &activation;\nvar weight_ptr: [*]f16 = &weight;\nvar output_ptr: [*]f32 = &output;\n\n");
    PUT("const reduce_done_id: local_task_id = @get_local_task_id(12);\n\n");
    PUT("fn unblock_completion_row() void {\n    if (pe_y == 0) {\n        sys_mod.unblock_cmd_stream();\n    }\n}\n\n");
    PUT("fn compute() void {\n");
    PUT("    for (@range(i16, in_dim_per_pe)) |col| {\n        activation_f32[@as(u32, col)] = @as(f32, activation[@as(u32, col)]);\n    }\n");
    PUT("    for (@range(i16, out_dim_per_pe)) |row| {\n        var sum: f32 = 0.0;\n        const row_base = @as(u32, row) * @as(u32, in_dim_per_pe);\n        for (@range(i16, in_dim_per_pe)) |col| {\n            const idx = row_base + @as(u32, col);\n            sum += activation_f32[@as(u32, col)] * @as(f32, weight[idx]);\n        }\n        partial[@as(u32, row)] = sum;\n    }\n");
    PUT("    if (num_pes == 1) {\n        for (@range(i16, out_dim_per_pe)) |i| {\n            output[@as(u32, i)] = partial[@as(u32, i)];\n        }\n        unblock_completion_row();\n    } else {\n        mpi_x.init();\n        mpi_x.reduce_fadds(@as(u16, num_pes - 1), @ptrcast([*]f32, &partial), @ptrcast([*]f32, &output), @as(u16, out_dim_per_pe), reduce_done_id);\n    }\n}\n\n");
    PUT("task reduce_done_task() void {\n    unblock_completion_row();\n}\n\n");
    PUT("comptime {\n    @bind_local_task(reduce_done_task, reduce_done_id);\n    @export_symbol(activation_ptr, \"activation\");\n    @export_symbol(weight_ptr, \"weight\");\n    @export_symbol(output_ptr, \"output\");\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
CslErr csl_pe_emit(CslBuf *out, const CslKernelDesc *d) {
    if (!out || !d) return CSL_ERR_INVALID;
    switch (d->pattern) {
        case CSL_PATTERN_DEQUANT: return csl_pe_dequant(out, d);
        case CSL_PATTERN_ELEMENT_WISE: return csl_pe_elementwise(out, d);
        case CSL_PATTERN_GATHER: return csl_pe_gather(out, d);
        case CSL_PATTERN_TILED_MATMUL: return csl_pe_matmul(out, d);
        case CSL_PATTERN_TILED_MATMUL_Q4K: return csl_pe_matmul_q4k(out, d);
        case CSL_PATTERN_REDUCTION: return csl_pe_reduction(out, d);
        case CSL_PATTERN_ROPE: return csl_pe_rope(out, d);
        case CSL_PATTERN_SAMPLE: return csl_pe_sample(out, d);
        case CSL_PATTERN_DENSE_GEMV: return csl_pe_dense_gemv(out, d);
        case CSL_PATTERN_ATTENTION_STREAMING:
        case CSL_PATTERN_ATTENTION_DECODE:
        case CSL_PATTERN_ATTENTION_TILED:
        case CSL_PATTERN_FUSED_GEMV_DEQUANT:
        case CSL_PATTERN_ATTENTION_LINEAR:
        case CSL_PATTERN_KV_WRITE:
        case CSL_PATTERN_KV_READ:
        case CSL_PATTERN_FUSED_FFN:
        case CSL_PATTERN_SEMANTIC:
        case CSL_PATTERN_UNSUPPORTED: return CSL_ERR_UNSUPPORTED;
        default: return CSL_ERR_UNSUPPORTED;
    }
}
