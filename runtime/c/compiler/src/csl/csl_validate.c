// SPDX-License-Identifier: MIT
// Rules copied from emit_csl_validate.zig (see header notes).
#include "csl_validate.h"
#include "csl_spec.h"
#include <string.h>
#include <ctype.h>
static int has(const char *s, const char *n) { return s && n && strstr(s, n) != NULL; }
static const char *ERR;
#define REQ(n, m) do { if (!has(csl, (n))) { ERR = (m); return 0; } } while (0)
#define BAD(n, m) do { if (has(csl, (n))) { ERR = (m); return 0; } } while (0)
static int sdk210(const char *csl) {
    const char *p; const char *start; const char *tail; const char *end;
    BAD("comptime_struct", "CSL SDK 2.10 removed comptime_struct; emit untyped params or named structs");
    BAD("@concat_struct", "CSL SDK 2.10 removed @concat_struct/@concat_structs; emit named struct composition");
    p = csl;
    while ((start = strstr(p, "@get_dsd(fabout_dsd")) != NULL) {
        tail = start; end = strstr(tail, ");");
        if (!end) break;
        { char call[512]; size_t len = (size_t)(end - tail); if (len >= sizeof(call)) len = sizeof(call) - 1; memcpy(call, tail, len); call[len] = '\0';
          if (strstr(call, ".fabric_color")) { ERR = "CSL SDK 2.10 fabric DSDs use queue-color binding, not fabric_color"; return 0; }
          if (!strstr(call, ".output_queue")) { ERR = "CSL SDK 2.10 requires explicit output_queue for fabout_dsd"; return 0; } }
        p = end + 1;
    }
    p = csl;
    while ((start = strstr(p, "@get_dsd(fabin_dsd")) != NULL) {
        tail = start; end = strstr(tail, ");");
        if (!end) break;
        { char call[512]; size_t len = (size_t)(end - tail); if (len >= sizeof(call)) len = sizeof(call) - 1; memcpy(call, tail, len); call[len] = '\0';
          if (strstr(call, ".fabric_color")) { ERR = "CSL SDK 2.10 fabric DSDs use queue-color binding, not fabric_color"; return 0; }
          if (!strstr(call, ".input_queue")) { ERR = "CSL SDK 2.10 requires explicit input_queue for fabin_dsd"; return 0; } }
        p = end + 1;
    }
    return 1;
}
int csl_validate_pattern(const char *csl, CslValidatePattern pattern, const char **first_error) {
    const char *base;
    ERR = NULL;
    if (!csl) { if (first_error) *first_error = "missing layout.csl section"; return 0; }
    base = csl_validate_output(csl);
    if (base) { if (first_error) *first_error = base; return 0; }
    if (!sdk210(csl)) { if (first_error) *first_error = ERR;
                return 0; }
    switch (pattern) {
        case CSL_VAL_ELEMENT_WISE:
            BAD("fabin_dsd", "element_wise pattern should not use fabric input DSDs");
            BAD("fabout_dsd", "element_wise pattern should not use fabric output DSDs");
            REQ("@range", "element_wise missing compute loop (@range)");
            break;
        case CSL_VAL_REDUCTION:
            if (!(has(csl, "fabout_dsd") && has(csl, "fabin_dsd")) && !has(csl, "PE-local")) {
                ERR = "reduction must emit either fabric DSDs (distributed) or a PE-local workgroup marker";
                if (first_error) *first_error = ERR;
                return 0;
            }
            break;
        case CSL_VAL_GATHER: REQ("indices", "gather missing indices reference"); break;
        case CSL_VAL_ATTENTION_DECODE: case CSL_VAL_ATTENTION_TILED:
        case CSL_VAL_ATTENTION_LINEAR: case CSL_VAL_ATTENTION_STREAMING:
            if (!(has(csl, "q_") || has(csl, "query") || has(csl, "key"))) {
                ERR = "attention pattern missing Q/K/V buffer references";
                if (first_error) *first_error = ERR;
                return 0;
            }
            if (has(csl, "decode_sliding_window") && !has(csl, "decode_position")) {
                ERR = "attention with sliding window state must also declare decode position state";
                if (first_error) *first_error = ERR;
                return 0;
            }
            break;
        case CSL_VAL_TILED_MATMUL: REQ("accum", "tiled_matmul missing accumulator"); break;
        case CSL_VAL_TILED_MATMUL_Q4K:
            REQ("QK_K_BLOCK_BYTES", "tiled_matmul_q4k_dequant_b missing Q4K block-byte constant");
            REQ("dequant_b_tile", "tiled_matmul_q4k_dequant_b missing dequant prologue");
            BAD("var B_ptr: [*]f32", "tiled_matmul_q4k_dequant_b must export B_ptr as [*]u8 byte stream, not f32");
            break;
        case CSL_VAL_FUSED_GEMV_DEQUANT: REQ("dequant", "fused_gemv_dequant missing dequantization"); break;
        case CSL_VAL_DENSE_GEMV:
            REQ("dense GEMV", "dense_gemv missing dense GEMV marker");
            REQ("var partial: [out_dim_per_pe]f32", "dense_gemv missing separate f32 partial buffer");
            REQ("param pe_y: i16;", "dense_gemv missing row coordinate parameter");
            REQ("<collectives_2d/pe>", "dense_gemv missing collectives_2d PE import");
            REQ("c2d_params", "dense_gemv missing collectives_2d params");
            REQ("unblock_completion_row", "dense_gemv missing single-row command-stream completion guard");
            REQ("reduce_fadds", "dense_gemv missing collectives_2d row reduction");
            REQ("out_dim_per_pe", "dense_gemv missing output shard parameter");
            REQ("[*]f32", "dense_gemv must export f32 logits");
            BAD("@mov32(reduce_out, scratch_out_dsd", "dense_gemv must use collectives_2d reduce_fadds, not the hand-rolled row-chain send");
            break;
        case CSL_VAL_DEQUANT:
            if (!(has(csl, "QK_K") || has(csl, "Q4K"))) {
                ERR = "dequant missing Q4K format constants";
                if (first_error) *first_error = ERR;
                return 0;
            }
            break;
        case CSL_VAL_ROPE:
            if (!(has(csl, "cos") || has(csl, "sin"))) {
                ERR = "rope missing cos/sin rotation references";
                if (first_error) *first_error = ERR;
                return 0;
            }
            break;
        case CSL_VAL_SAMPLE: REQ("logit", "sample missing logits reference"); break;
        case CSL_VAL_KV_WRITE: case CSL_VAL_KV_READ:
            if (!(has(csl, "cache") || has(csl, "kv_"))) {
                ERR = "kv_write/kv_read missing cache buffer reference";
                if (first_error) *first_error = ERR;
                return 0;
            }
            break;
        case CSL_VAL_FUSED_FFN:
            if (!(has(csl, "silu") || has(csl, "gelu"))) {
                ERR = "fused_ffn missing activation function (silu/gelu)";
                if (first_error) *first_error = ERR;
                return 0;
            }
            REQ("c2d_params", "fused_ffn missing collectives_2d params");
            REQ("reduce_fadds", "fused_ffn missing collectives_2d vector reduction");
            if (has(csl, "param reduce_color") || has(csl, "@fmovs(reduce_out")) {
                ERR = "fused_ffn must use collectives_2d reduce_fadds, not the hand-routed row-chain reduce";
                if (first_error) *first_error = ERR;
                return 0;
            }
            break;
    }
    if (first_error) *first_error = ERR;
    return ERR == NULL;
}
CslErr csl_toolchain_config_from_env_value(const char *raw, CslToolchainConfig *out) {
    const char *p;
    if (!raw || !out) return CSL_ERR_INVALID;
    while (*raw == ' ' || *raw == '\t' || *raw == '\n' || *raw == '\r') raw++;
    p = raw + strlen(raw);
    while (p > raw && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r')) p--;
    if (p == raw) return CSL_ERR_INVALID;
    { /* case-insensitive PATH sentinel, Zig behaviour */
      const char *s = raw; size_t n = (size_t)(p - raw);
      if (n == 4 && (tolower((unsigned char)s[0]) == 'p') && (tolower((unsigned char)s[1]) == 'a') &&
          (tolower((unsigned char)s[2]) == 't') && (tolower((unsigned char)s[3]) == 'h')) {
          out->executable = "PATH"; out->discovery = CSL_DISC_PATH_LOOKUP; return CSL_OK;
      }
      out->executable = raw; out->discovery = CSL_DISC_ENV_PATH; return CSL_OK;
    }
}
CslErr csl_validate_toolchain(const CslToolchainConfig *cfg) {
    if (!cfg || !cfg->executable || cfg->executable[0] == '\0') return CSL_ERR_INVALID;
    return CSL_ERR_UNSUPPORTED; /* running cslc is the caller's job, not this library's */
}
int csl_pattern_kind_from_name(const char *name, CslValidatePattern *out) {
    struct { const char *n; CslValidatePattern k; } tab[] = {
        {"element_wise", CSL_VAL_ELEMENT_WISE}, {"reduction", CSL_VAL_REDUCTION},
        {"gather", CSL_VAL_GATHER}, {"attention_decode", CSL_VAL_ATTENTION_DECODE},
        {"attention_tiled", CSL_VAL_ATTENTION_TILED}, {"attention_linear", CSL_VAL_ATTENTION_LINEAR},
        {"attention_streaming", CSL_VAL_ATTENTION_STREAMING}, {"tiled_matmul", CSL_VAL_TILED_MATMUL},
        {"tiled_matmul_q4k_dequant_b", CSL_VAL_TILED_MATMUL_Q4K},
        {"fused_gemv_dequant", CSL_VAL_FUSED_GEMV_DEQUANT}, {"dense_gemv", CSL_VAL_DENSE_GEMV},
        {"dequant", CSL_VAL_DEQUANT}, {"rope", CSL_VAL_ROPE}, {"sample", CSL_VAL_SAMPLE},
        {"kv_write", CSL_VAL_KV_WRITE}, {"kv_read", CSL_VAL_KV_READ}, {"fused_ffn", CSL_VAL_FUSED_FFN}
    };
    size_t i; if (!name || !out) return 0;
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
        if (strcmp(name, tab[i].n) == 0) { *out = tab[i].k; return 1; }
    return 0;
}
