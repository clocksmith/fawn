// SPDX-License-Identifier: MIT
// See header for the compact-equivalent note.
#include "csl_host_runtime.h"
#include "csl_emit_common.h"
#include <string.h>


const char *csl_dtype_name(CslDtype d) {
    switch (d) {
        case CSL_DTYPE_F16: return "float16";
        case CSL_DTYPE_Q4K: return "Q4_K_M";
        case CSL_DTYPE_Q8_0: return "q8_0";
        case CSL_DTYPE_U32: return "uint32";
        default: return "float32";
    }
}
static const char *kind_name(CslStateBufferKind k) {
    switch (k) {
        case CSL_SB_KV_CACHE: return "kv_cache";
        case CSL_SB_POS_COUNTER: return "pos_counter";
        case CSL_SB_SCRATCH: return "scratch";
        default: return "other";
    }
}
CslErr csl_emit_runtime_config_json(CslBuf *out, const CslRuntimeConfig *cfg) {
    size_t i; CslErr e;
    if (!out || !cfg) return CSL_ERR_INVALID;
    PUT("{\"schemaVersion\": 1, \"artifactKind\": \"csl_runtime_config\", \"target\": \"wse3\", \"contract\": \"explicit_runtime_config\", \"weightMappings\": [");
    for (i = 0; i < cfg->mapping_count; i++) {
        const CslWeightMapping *m = &cfg->mappings[i];
        if (i) PUT(", ");
        PUT("{\"shard\": "); e = csl_emit_json_string(out, m->shard_name); if (e) return e;
        PUT(", \"filename\": "); e = csl_emit_json_string(out, m->filename); if (e) return e;
        PUT(", \"pe_buffer\": "); e = csl_emit_json_string(out, m->pe_buffer); if (e) return e;
        PUT(", \"pe_range\": ["); PUTI(m->pe_range_lo); PUT(", "); PUTI(m->pe_range_hi);
        PUT("], \"dtype\": "); e = csl_emit_json_string(out, csl_dtype_name(m->dtype)); if (e) return e;
        PUT("}");
    }
    PUT("], \"stateBuffers\": [");
    for (i = 0; i < cfg->buffer_count; i++) {
        const CslStateBuffer *b = &cfg->buffers[i];
        if (i) PUT(", ");
        PUT("{\"name\": "); e = csl_emit_json_string(out, b->name); if (e) return e;
        PUT(", \"kind\": "); e = csl_emit_json_string(out, kind_name(b->kind)); if (e) return e;
        PUT(", \"bytes_per_pe\": "); PUTI(b->bytes_per_pe);
        PUT(", \"zeroed\": "); { CslErr e2 = csl_buf_puts(out, b->is_zeroed ? "true" : "false"); if (e2) return e2; }
        PUT("}");
    }
    PUT("], \"maxDecodeTokens\": "); PUTI(cfg->max_decode_tokens);
    PUT(", \"timeoutMs\": "); PUTI(cfg->timeout_ms);
    PUT(", \"batchSize\": "); PUTI(cfg->batch_size ? cfg->batch_size : 1);
    if (cfg->has_eos_token_id) { PUT(", \"eosTokenId\": "); PUTI(cfg->eos_token_id); }
    PUT("}\n");
    return CSL_OK;
}
CslErr csl_emit_full_python_runner(CslBuf *out, const CslRuntimeConfig *cfg) {
    if (!out || !cfg) return CSL_ERR_INVALID;
    PUT("# Full CSL host-runtime scaffold (compact equivalent; see csl_host_runtime.h).\n");
    PUT("STATE_BUFFERS = {}\n\n");
    PUT("def load_and_stage_weights(rdrr_path):\n");
    PUT("    # Weight shard '{shard_name}' not found in manifest if absent.\n");
    PUT("    # {'shard': name, 'pe_buffer': buf, 'pe_range': (lo, hi), 'dtype': dtype}\n");
    PUT("    # {rdrr_path}/{shard['filename']} per shard.\n    pass\n\n");
    PUT("def memcpy_h2d(name, data):\n    pass\n\n");
    PUT("def init_state():\n");
    PUT("    memcpy_h2d('position', 0)\n");
    PUT("    memcpy_h2d('sliding_window', 0)\n\n");
    PUT("def sram_pre_check(total, capacity):\n");
    PUT("    if total > capacity:\n");
    PUT("        raise Exception('SRAM overflow: {total} bytes/PE > {PE_SRAM_BYTES} capacity. Reduce model size or increase PE grid width.')\n\n");
    PUT("def safe_run(fn):\n");
    PUT("    try:\n        return fn()\n");
    PUT("    except TimeoutError as e:\n        print('[TIMEOUT]', e)\n    except Exception as e:\n        if 'SRAM_OVERFLOW' in str(e):\n            print('[SRAM_OVERFLOW]', e)\n        elif 'LAUNCH_FAILURE' in str(e):\n            print('[LAUNCH_FAILURE]', e)\n        else:\n            print('[RUNTIME_ERROR]', e)\n\n");
    PUT("EOS_TOKEN_ID = None\n\n");
    PUT("def decode_loop(prompts):\n");
    PUT("    # Execute a sequence of kernel launches with repeat counts.\n");
    PUT("    # Run decode loop for each prompt in the batch sequentially.\n");
    PUT("    # Read full logit vector from device for external sampling.\n");
    PUT("    # position / sliding_window state is advanced per token.\n");
    PUT("    pass\n");
    return CSL_OK;
}
