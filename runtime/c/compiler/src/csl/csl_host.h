// SPDX-License-Identifier: MIT
// emit_csl_host.zig: WSE-3 constants, ModelConfig, SRAM estimates,
// compilation manifest JSON, Python runner scaffold (compact equivalent
// with the section markers the Zig tests check; the full per-launch
// code generation of Zig is NOT reproduced byte-for-byte).
#ifndef CSL_HOST_H
#define CSL_HOST_H
#include "csl_kernel.h"
#define CSL_WSE3_PE_COUNT ((uint32_t)900000)
#define CSL_WSE3_PE_SRAM_BYTES ((uint32_t)49152)
#define CSL_WSE3_TOTAL_SRAM_BYTES ((uint64_t)CSL_WSE3_PE_COUNT * CSL_WSE3_PE_SRAM_BYTES)
typedef enum { CSL_QUANT_F16, CSL_QUANT_Q4K, CSL_QUANT_Q8_0 } CslQuantFormat;
typedef struct {
    uint32_t hidden_dim, num_heads, head_dim, num_layers, vocab_size, max_seq_len;
    CslQuantFormat quant_format;
    uint32_t ffn_expansion_factor, ffn_matrix_count;
    int has_linear_key_head_dim, has_linear_value_head_dim, has_linear_conv_kernel_dim;
    uint32_t linear_key_head_dim, linear_value_head_dim, linear_conv_kernel_dim;
    int has_global_head_dim, has_num_key_value_heads;
    uint32_t global_head_dim, num_key_value_heads;
    int has_ple_width, has_ple_vocab_size;
    uint32_t ple_width, ple_vocab_size;
    float partial_rotary_factor; /* 1.0 full rotary; Qwen 3.x uses 0.25 */
    int has_mrope_section; uint32_t mrope_section[3];
} CslModelConfig;
typedef struct { const char *name; const char *pattern; uint32_t count; } CslKernelSpec;
typedef enum { CSL_ATTN_GLOBAL, CSL_ATTN_SLIDING } CslLaunchAttentionType;
typedef struct {
    const char *kernel_name; uint32_t repeat;
    int has_attention_type; CslLaunchAttentionType attention_type;
    int has_sliding_window_size; uint32_t sliding_window_size;
    int has_current_pos_source; /* decode_position only, as in Zig */
    const char *kv_cache_alias; /* NULL means no alias */
} CslLaunchSpec;
typedef struct {
    uint32_t pe_grid_width, pe_grid_height;
    const CslKernelSpec *kernels; size_t kernel_count;
    const CslLaunchSpec *prefill_launches; size_t prefill_count;
    const CslLaunchSpec *decode_launches; size_t decode_count;
    int has_eos_token_id; uint32_t eos_token_id;
} CslHostPlan;
typedef struct {
    uint64_t per_layer_bytes, embedding_bytes, total_bytes, capacity_bytes;
    int fits; const char *estimate_kind;
} CslSramEstimate;
void csl_model_config_init(CslModelConfig *c);
uint64_t csl_estimate_layer_sram_bytes(const CslModelConfig *c);
CslSramEstimate csl_estimate_model_sram(const CslModelConfig *c);
CslSramEstimate csl_estimate_model_sram_for_plan(const CslModelConfig *c, const CslHostPlan *plan);
uint32_t csl_effective_kv_cache_layer_count(const CslModelConfig *c, const CslHostPlan *plan);
uint32_t csl_rope_num_pairs(const CslModelConfig *c);
CslErr csl_emit_compilation_manifest(CslBuf *out, const CslModelConfig *c, const CslHostPlan *plan);
CslErr csl_emit_python_runner(CslBuf *out, const CslModelConfig *c, const CslHostPlan *plan);
#endif
