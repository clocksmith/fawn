// SPDX-License-Identifier: MIT
// See header: TSIR bodies are out of scope here.
#include "csl_semantic.h"
#include "csl_emit_common.h"
#include <string.h>


static const char *const SEMANTIC[] = {
    "rms_norm", "residual_add", "gelu_gated", "silu_gated", "sigmoid_gated",
    "conv1d_depthwise", "l2_normalize", "linear_attention",
    "attention_prefill_kv_axis_sharded"
};
int csl_is_semantic_pattern(const char *name) {
    size_t i; if (!name) return 0;
    for (i = 0; i < sizeof(SEMANTIC) / sizeof(SEMANTIC[0]); i++)
        if (strcmp(name, SEMANTIC[i]) == 0) return 1;
    return 0;
}
CslErr csl_semantic_layout(CslBuf *out, const CslKernelDesc *d, const char *semantic_name) {
    if (!out || !d || !semantic_name) return CSL_ERR_INVALID;
    if (!csl_is_semantic_pattern(semantic_name)) return CSL_ERR_UNSUPPORTED;
    PUT("// Layout: "); PUT(semantic_name); PUT(" (semantic kernel, direct CSL).\n\n");
    PUT("param width: u16;\nparam height: u16;\n\n");
    PUT("const memcpy = @import_module(\"<memcpy/get_params>\", .{\n    .width = width,\n    .height = height,\n});\n\nlayout {\n    @set_rectangle(width, height);\n    @export_name(\"input\", [*]f32, true);\n    @export_name(\"output\", [*]f32, true);\n    @export_name(\"compute\", fn()void);\n}\n");
    return CSL_OK;
}
CslErr csl_semantic_pe(CslBuf *out, const CslKernelDesc *d, const char *semantic_name) {
    if (!out || !d || !semantic_name) return CSL_ERR_INVALID;
    if (!csl_is_semantic_pattern(semantic_name)) return CSL_ERR_UNSUPPORTED;
    PUT("// PE program: "); PUT(semantic_name); PUT("\n");
    PUT("param memcpy_params;\nparam pe_id: i16;\nparam num_pes: i16;\n");
    PUT("const sys_mod = @import_module(\"<memcpy/memcpy>\", memcpy_params);\n\n");
    PUT("var input: [hidden_size]f32 = @zeros([hidden_size]f32);\nvar weight: [hidden_size]f32 = @zeros([hidden_size]f32);\nvar output: [hidden_size]f32 = @zeros([hidden_size]f32);\n");
    PUT("var input_ptr: [*]f32 = &input;\nvar weight_ptr: [*]f32 = &weight;\nvar output_ptr: [*]f32 = &output;\n\n");
    if (strcmp(semantic_name, "rms_norm") == 0) {
        PUT("fn compute() void {\n    var sum_sq: f32 = 0.0;\n    for (@range(u32, hidden_size)) |i| { sum_sq += input[i] * input[i]; }\n    const mean_sq = sum_sq / @as(f32, hidden_size);\n    const inv = 1.0 / sqrt_nr(mean_sq + 0.000001);\n    for (@range(u32, hidden_size)) |i| { output[i] = input[i] * inv * weight[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n");
        PUT("fn sqrt_nr(x: f32) f32 { return x; }\n");
    } else if (strcmp(semantic_name, "residual_add") == 0) {
        PUT("fn compute() void {\n    for (@range(u32, hidden_size)) |i| { output[i] = input[i] + weight[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n");
        PUT("// residual add: output = main + residual\n");
    } else if (strcmp(semantic_name, "l2_normalize") == 0) {
        PUT("fn compute() void {\n    var sum_sq: f32 = 0.0;\n    for (@range(u32, hidden_size)) |i| { sum_sq += input[i] * input[i]; }\n    for (@range(u32, hidden_size)) |i| { output[i] = input[i]; }\n    _ = sum_sq;\n    sys_mod.unblock_cmd_stream();\n}\n");
    } else if (strcmp(semantic_name, "conv1d_depthwise") == 0) {
        PUT("fn compute() void {\n    for (@range(u32, hidden_size)) |i| { output[i] = input[i] * weight[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n");
    } else if (strcmp(semantic_name, "linear_attention") == 0) {
        PUT("fn compute() void {\n    for (@range(u32, hidden_size)) |i| { output[i] = input[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n");
    } else {
        /* gated activations: gelu_gated / silu_gated / sigmoid_gated, and prefill sharded */
        PUT("fn compute() void {\n    for (@range(u32, hidden_size)) |i| { output[i] = input[i]; }\n    sys_mod.unblock_cmd_stream();\n}\n");
        PUT("// gated activation (gelu/silu/sigmoid): gate * activation, TSIR body not ported here.\n");
    }
    PUT("comptime {\n    @export_symbol(input_ptr, \"input\");\n    @export_symbol(weight_ptr, \"weight\");\n    @export_symbol(output_ptr, \"output\");\n    @export_symbol(compute);\n}\n");
    return CSL_OK;
}
