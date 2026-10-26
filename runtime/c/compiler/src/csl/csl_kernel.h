// SPDX-License-Identifier: MIT
// CslKernelDesc: the documented seam between CSL emitters and WGSL IR.
// The Zig backend classifies an ir.Module; there is no C WGSL IR module
// in scope, so every IR-derived input is an explicit descriptor field.
// A future C IR port can fill descriptors from real IR without changing
// any emitter. Ownership: all strings are borrowed, never freed by
// emitters; the caller owns the descriptor and its globals array.
#ifndef CSL_KERNEL_H
#define CSL_KERNEL_H
#include <stddef.h>
#include <stdint.h>
#include "csl_error.h"
#include "csl_text.h"

typedef enum {
    CSL_ADDR_STORAGE, CSL_ADDR_UNIFORM, CSL_ADDR_WORKGROUP, CSL_ADDR_OTHER
} CslAddrSpace;
typedef struct { const char *name; CslAddrSpace addr_space; int has_binding; } CslGlobal;

typedef enum {
    CSL_PATTERN_ELEMENT_WISE, CSL_PATTERN_REDUCTION, CSL_PATTERN_TILED_MATMUL,
    CSL_PATTERN_TILED_MATMUL_Q4K, CSL_PATTERN_GATHER, CSL_PATTERN_ROPE,
    CSL_PATTERN_ATTENTION_STREAMING, CSL_PATTERN_ATTENTION_DECODE,
    CSL_PATTERN_ATTENTION_TILED, CSL_PATTERN_DEQUANT, CSL_PATTERN_SAMPLE,
    CSL_PATTERN_FUSED_GEMV_DEQUANT, CSL_PATTERN_ATTENTION_LINEAR,
    CSL_PATTERN_KV_WRITE, CSL_PATTERN_KV_READ, CSL_PATTERN_FUSED_FFN,
    CSL_PATTERN_DENSE_GEMV, CSL_PATTERN_SEMANTIC, CSL_PATTERN_UNSUPPORTED
} CslPattern;

typedef struct CslKernelDesc {
    CslPattern pattern;
    const char *pattern_name; /* e.g. "element_wise"; NULL means use pattern enum name */
    const CslGlobal *globals; size_t global_count;
    /* Contract fields, copied from emit_csl_classify.zig info structs. */
    uint32_t input_count, output_count, shared_count;
    int has_size_guard, has_apply_phase, distributed;
    uint32_t tile_m, tile_n, tile_k;
    uint32_t indices_global, table_global, output_global, input_global;
    uint32_t cos_global, sin_global, q_global, k_global, v_global;
    uint32_t shared_k_global, shared_v_global, quant_global, logits_global;
    uint32_t tokens_global, activation_global, weight_global;
    uint32_t gate_weight_global, up_weight_global;
    uint32_t key_proj_global, val_proj_global, key_cache_global, val_cache_global;
    uint32_t key_out_global, val_out_global, shared_global_index;
    /* Geometry and shapes the Zig classifier/layout derive from IR. */
    uint32_t width, height, chunk_size, hidden_size, head_dim, num_heads;
    uint32_t vocab_size, seq_len, in_dim, out_dim, in_dim_per_pe, out_dim_per_pe;
    uint32_t rows_per_pe, tokens_per_chunk, hidden_per_pe, window_size;
    uint32_t num_pes, pe_id, max_seq_len, block_size;
    int has_sliding_window, has_decode_position, is_q4k, is_f16;
    double scale; /* attention scale; 0 means the emitter default */
    const char *op_name; /* elementwise op, e.g. "gelu", "silu", "scale" */
    const char *activation; /* fused_ffn / semantic: "silu", "gelu", ... */
    const char *input_name, *output_name, *table_name, *indices_name;
    const char *scalar; /* "f16" or "f32"; NULL means f32 */
    const char *unsupported_reason;
} CslKernelDesc;

void csl_kernel_desc_init(CslKernelDesc *d);
const char *csl_pattern_name(CslPattern p);
int csl_pattern_from_name(const char *name, CslPattern *out);
const char *csl_desc_pattern_name(const CslKernelDesc *d);
/* Exact patternContractValid rules from emit_csl_classify.zig. */
int csl_pattern_contract_valid(const CslKernelDesc *d);
/* The IR walk itself is not ported: classifying from IR is unsupported. */
#endif
