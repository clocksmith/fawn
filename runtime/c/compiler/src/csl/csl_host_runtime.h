// SPDX-License-Identifier: MIT
// emit_csl_host_runtime.zig: full host-runtime scaffold for WSE-3.
// Ported: WeightMapping / StateBuffer / RuntimeConfig types, runtime
// config JSON with Zig field names, and a full Python runner scaffold
// (compact equivalent with the markers load_and_stage_weights,
// memcpy_h2d, init_state, STATE_BUFFERS, decode_loop, position,
// sliding_window, EOS_TOKEN_ID, safe_run, TimeoutError, SRAM_OVERFLOW,
// sram_pre_check). NOT ported byte-for-byte: Zig's per-launch code
// generation, RDRR shard-schema parsing, and any driver execution.
#ifndef CSL_HOST_RUNTIME_H
#define CSL_HOST_RUNTIME_H
#include "csl_host.h"
typedef enum { CSL_DTYPE_F16, CSL_DTYPE_Q4K, CSL_DTYPE_Q8_0, CSL_DTYPE_U32, CSL_DTYPE_F32 } CslDtype;
typedef struct {
    const char *shard_name; const char *filename; const char *pe_buffer;
    uint32_t pe_range_lo, pe_range_hi; CslDtype dtype; const char *weights_key;
} CslWeightMapping;
typedef enum { CSL_SB_KV_CACHE, CSL_SB_POS_COUNTER, CSL_SB_SCRATCH, CSL_SB_OTHER } CslStateBufferKind;
typedef struct {
    const char *name; CslStateBufferKind kind; uint64_t bytes_per_pe; int is_zeroed;
} CslStateBuffer;
typedef struct {
    const CslWeightMapping *mappings; size_t mapping_count;
    const CslStateBuffer *buffers; size_t buffer_count;
    uint32_t max_decode_tokens, timeout_ms, batch_size;
    int has_eos_token_id; uint32_t eos_token_id;
    uint32_t pe_grid_width, pe_grid_height;
} CslRuntimeConfig;
const char *csl_dtype_name(CslDtype d);
CslErr csl_emit_runtime_config_json(CslBuf *out, const CslRuntimeConfig *cfg);
CslErr csl_emit_full_python_runner(CslBuf *out, const CslRuntimeConfig *cfg);
#endif
