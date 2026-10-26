// SPDX-License-Identifier: MIT
// Rewrite lists are verbatim from the Zig files named in the header.
#include "csl_host_compile.h"
#include "csl_emit_core.h"
#include <string.h>
#include <stdio.h>
static int is_ident(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}
static CslErr replace_all(char *buf, size_t *len, size_t cap, const char *from, const char *to) {
    size_t fl = strlen(from), tl = strlen(to), idx = 0;
    if (!buf || !len || !from || !to) return CSL_ERR_INVALID;
    while (idx + fl <= *len) {
        if (memcmp(buf + idx, from, fl) != 0) { idx += 1; continue; }
        if (tl > fl) {
            size_t growth = tl - fl;
            if (*len + growth > cap) return CSL_ERR_TOO_LARGE;
            memmove(buf + idx + tl, buf + idx + fl, *len - (idx + fl));
            *len += growth;
        } else if (tl < fl) {
            size_t shrink = fl - tl;
            memmove(buf + idx + tl, buf + idx + fl, *len - (idx + fl));
            *len -= shrink;
        }
        memcpy(buf + idx, to, tl);
        idx += tl;
    }
    return CSL_OK;
}
static void rewrite_scalar_token(char *buf, size_t len, const char *from, const char *to) {
    size_t fl = strlen(from), idx = 0;
    while (idx + fl <= len) {
        if (memcmp(buf + idx, from, fl) != 0) { idx += 1; continue; }
        if ((idx > 0 && is_ident((unsigned char)buf[idx - 1])) ||
            (idx + fl < len && is_ident((unsigned char)buf[idx + fl]))) { idx += 1; continue; }
        memcpy(buf + idx, to, fl);
        idx += fl;
    }
}
#define REP(f, t) do { CslErr _e = replace_all(buf, len, cap, (f), (t)); if (_e) return _e; } while (0)
CslErr csl_rewrite_f16_compile_source(char *buf, size_t *len, size_t cap) {
    if (!buf || !len || *len > cap) return CSL_ERR_INVALID;
    rewrite_scalar_token(buf, *len, "f32", "f16");
    REP("-3.4028235e+38", "-65504.0");
    REP("-1.0e30", "-65504.0");
    REP("@bitcast(f16, u[1])", "@as(f16, 0.000001)");
    REP("var scratch_in: [2]f16 = @zeros([2]f16);", "var scratch_in: [2]u32 = @zeros([2]u32);");
    REP("var scratch_out: [2]f16 = @zeros([2]f16);", "var scratch_out: [2]u32 = @zeros([2]u32);");
    REP("scratch_out[0] = local_max_val;", "scratch_out[0] = @as(u32, @bitcast(u16, local_max_val));");
    REP("scratch_out[0] = best_val;", "scratch_out[0] = @as(u32, @bitcast(u16, best_val));");
    REP("scratch_out[1] = @bitcast(f16, local_max_idx);", "scratch_out[1] = local_max_idx;");
    REP("scratch_out[1] = @bitcast(f16, best_idx);", "scratch_out[1] = best_idx;");
    REP("const incoming_val = scratch_in[0];", "const incoming_val: f16 = @bitcast(f16, @as(u16, scratch_in[0]));");
    REP("const incoming_idx = @bitcast(u32, scratch_in[1]);", "const incoming_idx = scratch_in[1];");
    REP("@fmacs(C_dsd, C_dsd, A_dsd, b_val);", "for (@range(i16, Mt)) |ii| {\n                const c_idx = @as(u32, j) * @as(u32, Mt) + @as(u32, ii);\n                const a_idx = @as(u32, k) * @as(u32, Mt) + @as(u32, ii);\n                C_tile[c_idx] += Ap.*[a_idx] * b_val;\n            }");
    return CSL_OK;
}
CslErr csl_restore_fused_gemv_f32_reduction(char *buf, size_t *len, size_t cap) {
    if (!buf || !len || *len > cap) return CSL_ERR_INVALID;
    REP("@export_name(\"output\", [*]f16, true);\n    @export_name(\"compute\", fn()void);", "@export_name(\"output\", [*]f16, true);\n    @export_name(\"partial\", [*]f32, true);\n    @export_name(\"reduced\", [*]f32, true);\n    @export_name(\"compute\", fn()void);");
    REP("reduce_fadds accumulates f16 partials", "reduce_fadds accumulates f32 partials");
    REP("var partial: [out_dim_per_pe]f16 = @zeros([out_dim_per_pe]f16);", "var partial: [out_dim_per_pe]f32 = @zeros([out_dim_per_pe]f32);");
    REP("var reduced: [out_dim_per_pe]f16 = @zeros([out_dim_per_pe]f16);", "var reduced: [out_dim_per_pe]f32 = @zeros([out_dim_per_pe]f32);");
    REP("var weight_col: [out_dim_per_pe]f16 = @zeros([out_dim_per_pe]f16);", "var weight_col: [out_dim_per_pe]f32 = @zeros([out_dim_per_pe]f32);");
    REP("var partial_ptr: [*]f16 = &partial;", "var partial_ptr: [*]f32 = &partial;");
    REP("var reduced_ptr: [*]f16 = &reduced;", "var reduced_ptr: [*]f32 = &reduced;");
    REP("var sum: f16 = 0.0;", "var sum: f32 = 0.0;");
    REP("const d = @as(f16, @bitcast(f16, d_bits));", "const d = @as(f32, @bitcast(f16, d_bits));");
    REP("weight_col[@as(u32, row)] = @as(f16, byte & 0x0F) * d;", "weight_col[@as(u32, row)] = @as(f32, byte & 0x0F) * d;");
    REP("weight_col[@as(u32, row)] = @as(f16, byte >> 4) * d;", "weight_col[@as(u32, row)] = @as(f32, byte >> 4) * d;");
    REP("const lo = @as(f16, byte & 0x0F) * d;", "const lo = @as(f32, byte & 0x0F) * d;");
    REP("const hi = @as(f16, byte >> 4) * d;", "const hi = @as(f32, byte >> 4) * d;");
    REP("const x_lo = @as(f16, ", "const x_lo = @as(f32, ");
    REP("const x_hi = @as(f16, ", "const x_hi = @as(f32, ");
    REP("sum += lo * @as(f16, ", "sum += lo * @as(f32, ");
    REP("sum += hi * @as(f16, ", "sum += hi * @as(f32, ");
    REP("@ptrcast([*]f16, &partial)", "@ptrcast([*]f32, &partial)");
    REP("@ptrcast([*]f16, &reduced)", "@ptrcast([*]f32, &reduced)");
    return CSL_OK;
}
static CslErr insert_at(char *buf, size_t *len, size_t cap, size_t idx, const char *text) {
    size_t tl = strlen(text);
    if (idx > *len || *len + tl > cap) return CSL_ERR_TOO_LARGE;
    memmove(buf + idx + tl, buf + idx, *len - idx);
    memcpy(buf + idx, text, tl);
    *len += tl;
    return CSL_OK;
}
static const char *find_sub(const char *buf, size_t len, const char *needle, size_t *pos_out) {
    size_t nl = strlen(needle), i;
    if (nl == 0 || nl > len) return NULL;
    for (i = 0; i + nl <= len; i++) if (memcmp(buf + i, needle, nl) == 0) { if (pos_out) *pos_out = i; return buf + i; }
    return NULL;
}
static CslErr insert_after(char *buf, size_t *len, size_t cap, const char *needle, const char *text) {
    size_t pos;
    if (!find_sub(buf, *len, needle, &pos)) return CSL_ERR_INVALID;
    return insert_at(buf, len, cap, pos + strlen(needle), text);
}
static CslErr insert_before(char *buf, size_t *len, size_t cap, const char *needle, const char *text) {
    size_t pos;
    if (!find_sub(buf, *len, needle, &pos)) return CSL_ERR_INVALID;
    return insert_at(buf, len, cap, pos, text);
}
#define CHUNK_WORDS 128
#define CHUNK_COUNT 21
CslErr csl_rewrite_f16_output_pack_for_extent(char *buf, size_t *len, size_t cap, const char *extent) {
    char scratch[24576]; char needle[256]; int chunk; size_t spos; CslErr e;
    if (!buf || !len || !extent || *len > cap) return CSL_ERR_INVALID;
    spos = 0;
    for (chunk = 0; chunk < CHUNK_COUNT; chunk++) {
        int n = snprintf(scratch + spos, sizeof(scratch) - spos, "var output_chunk_%04d: [%d]u32 = @zeros([%d]u32);\n", chunk, CHUNK_WORDS, CHUNK_WORDS);
        if (n < 0 || (size_t)n >= sizeof(scratch) - spos) return CSL_ERR_TOO_LARGE;
        spos += (size_t)n;
    }
    scratch[spos] = '\0';
    snprintf(needle, sizeof(needle), "var output: [%s]f16 = @zeros([%s]f16);\n", extent, extent);
    e = insert_after(buf, len, cap, needle, scratch); if (e) return e;
    spos = 0;
    for (chunk = 0; chunk < CHUNK_COUNT; chunk++) {
        int n = snprintf(scratch + spos, sizeof(scratch) - spos, "var output_chunk_%04d_ptr: [*]u32 = &output_chunk_%04d;\n", chunk, chunk);
        if (n < 0 || (size_t)n >= sizeof(scratch) - spos) return CSL_ERR_TOO_LARGE;
        spos += (size_t)n;
    }
    scratch[spos] = '\0';
    e = insert_after(buf, len, cap, "var output_ptr: [*]f16 = &output;\n", scratch); if (e) return e;
    spos = 0;
    for (chunk = 0; chunk < CHUNK_COUNT; chunk++) {
        int n = snprintf(scratch + spos, sizeof(scratch) - spos,
            "    for (@range(i16, %d)) |word| {\n        const pair = @as(u32, word) + %d;\n        const base = pair * 2;\n        if (base + 1 < @as(u32, %s)) {\n            const lo: u32 = @as(u32, @bitcast(u16, output[base]));\n            const hi: u32 = @as(u32, @bitcast(u16, output[base + 1]));\n            output_chunk_%04d[@as(u32, word)] = lo | (hi << 16);\n        } else {\n            output_chunk_%04d[@as(u32, word)] = 0;\n        }\n    }\n\n",
            CHUNK_WORDS, chunk * CHUNK_WORDS, extent, chunk, chunk);
        if (n < 0 || (size_t)n >= sizeof(scratch) - spos) return CSL_ERR_TOO_LARGE;
        spos += (size_t)n;
    }
    scratch[spos] = '\0';
    e = insert_before(buf, len, cap, "    sys_mod.unblock_cmd_stream();\n", scratch); if (e) return e;
    spos = 0;
    for (chunk = 0; chunk < CHUNK_COUNT; chunk++) {
        int n = snprintf(scratch + spos, sizeof(scratch) - spos, "    @export_name(\"output_chunk_%04d\", [*]u32, true);\n", chunk);
        if (n < 0 || (size_t)n >= sizeof(scratch) - spos) return CSL_ERR_TOO_LARGE;
        spos += (size_t)n;
    }
    scratch[spos] = '\0';
    e = insert_after(buf, len, cap, "    @export_name(\"output\", [*]f16, true);\n", scratch); if (e) return e;
    spos = 0;
    for (chunk = 0; chunk < CHUNK_COUNT; chunk++) {
        int n = snprintf(scratch + spos, sizeof(scratch) - spos, "    @export_symbol(output_chunk_%04d_ptr, \"output_chunk_%04d\");\n", chunk, chunk);
        if (n < 0 || (size_t)n >= sizeof(scratch) - spos) return CSL_ERR_TOO_LARGE;
        spos += (size_t)n;
    }
    scratch[spos] = '\0';
    return insert_after(buf, len, cap, "    @export_symbol(output_ptr, \"output\");\n", scratch);
}
CslErr csl_rewrite_f16_output_pack(char *buf, size_t *len, size_t cap) {
    return csl_rewrite_f16_output_pack_for_extent(buf, len, cap, "hidden_size");
}
static const char *const SOURCE_PATTERNS[] = {
    "element_wise", "residual", "gelu", "reduction", "tiled_matmul",
    "gather", "rope", "attention_streaming", "attention_decode",
    "attention_tiled", "attention_linear", "dequant", "sample",
    "fused_gemv_dequant", "kv_write", "kv_read", "fused_ffn",
};
/* NOTE: dense_gemv is deliberately absent: Zig patternSource returns
   NULL for it; dense sections come from the separate
   emitDenseGemvSections path, which csl_emit_pattern_sections also
   reaches via csl_emit_core (same layout + PE emitters). */
int csl_pattern_has_embedded_source(const char *pattern) {
    size_t i; if (!pattern) return 0;
    for (i = 0; i < sizeof(SOURCE_PATTERNS) / sizeof(SOURCE_PATTERNS[0]); i++)
        if (strcmp(pattern, SOURCE_PATTERNS[i]) == 0) return 1;
    return 0;
}
CslErr csl_emit_pattern_sections(CslBuf *out, const CslKernelDesc *d) {
    /* Descriptor sections via core; fixture WGSL sources are not embedded
       here, so this is the compile-source path minus fixtures/analysis. */
    if (!out || !d) return CSL_ERR_INVALID;
    if (!csl_pattern_has_embedded_source(csl_desc_pattern_name(d)) &&
        d->pattern != CSL_PATTERN_SEMANTIC && d->pattern != CSL_PATTERN_DENSE_GEMV)
        return CSL_ERR_UNSUPPORTED;
    return csl_emit_core(out, d);
}
