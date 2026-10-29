// SPDX-License-Identifier: MIT
// CslKernelDesc implementation. See csl_kernel.h for the seam contract.
#include "csl_kernel.h"
#include <string.h>
void csl_kernel_desc_init(CslKernelDesc *d) {
    if (!d) return;
    memset(d, 0, sizeof(*d));
    d->pattern = CSL_PATTERN_UNSUPPORTED;
    d->width = 1; d->height = 1; d->chunk_size = 1024; d->hidden_size = 1024;
}
static const char *const NAMES[] = {
    "element_wise", "reduction", "tiled_matmul", "tiled_matmul_q4k_dequant_b",
    "gather", "rope", "attention_streaming", "attention_decode",
    "attention_tiled", "dequant", "sample", "fused_gemv_dequant",
    "attention_linear", "kv_write", "kv_read", "fused_ffn", "dense_gemv",
    "semantic", "unsupported"
};
const char *csl_pattern_name(CslPattern p) {
    if ((int)p < 0 || (int)p > (int)CSL_PATTERN_UNSUPPORTED) return "unsupported";
    return NAMES[(int)p];
}
int csl_pattern_from_name(const char *name, CslPattern *out) {
    size_t i;
    if (!name || !out) return 0;
    for (i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
        if (strcmp(name, NAMES[i]) == 0) { *out = (CslPattern)i; return 1; }
    }
    return 0;
}
const char *csl_desc_pattern_name(const CslKernelDesc *d) {
    if (!d) return "unsupported";
    if (d->pattern_name) return d->pattern_name;
    return csl_pattern_name(d->pattern);
}
int csl_pattern_contract_valid(const CslKernelDesc *d) {
    if (!d) return 0;
    switch (d->pattern) {
        case CSL_PATTERN_ELEMENT_WISE:
            return d->input_count > 0 && d->output_count > 0 && d->has_size_guard;
        case CSL_PATTERN_REDUCTION:
            return d->input_count > 0 && d->output_count > 0 && d->has_apply_phase;
        case CSL_PATTERN_TILED_MATMUL: case CSL_PATTERN_TILED_MATMUL_Q4K:
            return d->tile_m > 0 && d->tile_n > 0 && d->tile_k > 0;
        case CSL_PATTERN_GATHER:
            return d->indices_global != d->table_global &&
                d->output_global != d->indices_global &&
                d->output_global != d->table_global;
        case CSL_PATTERN_ROPE:
            return d->input_global != d->cos_global &&
                d->input_global != d->sin_global && d->cos_global != d->sin_global;
        case CSL_PATTERN_ATTENTION_STREAMING: case CSL_PATTERN_ATTENTION_LINEAR:
        case CSL_PATTERN_DEQUANT: case CSL_PATTERN_SAMPLE:
        case CSL_PATTERN_FUSED_GEMV_DEQUANT: case CSL_PATTERN_FUSED_FFN:
            return d->input_count > 0 && d->output_count > 0;
        case CSL_PATTERN_ATTENTION_DECODE:
            return d->shared_count > 0 && d->input_count > 0 && d->output_count > 0;
        case CSL_PATTERN_ATTENTION_TILED:
            return d->shared_k_global != d->shared_v_global &&
                d->input_count > 0 && d->output_count > 0;
        case CSL_PATTERN_KV_WRITE:
            return d->key_proj_global != d->key_cache_global &&
                d->val_proj_global != d->val_cache_global &&
                d->key_cache_global != d->val_cache_global;
        case CSL_PATTERN_KV_READ:
            return d->key_cache_global != d->key_out_global &&
                d->val_cache_global != d->val_out_global &&
                d->key_out_global != d->val_out_global;
        case CSL_PATTERN_DENSE_GEMV:
            return d->in_dim_per_pe > 0 && d->out_dim_per_pe > 0;
        case CSL_PATTERN_SEMANTIC:
            return d->input_count > 0 && d->output_count > 0;
        case CSL_PATTERN_UNSUPPORTED: return 0;
        default: return 0;
    }
}
