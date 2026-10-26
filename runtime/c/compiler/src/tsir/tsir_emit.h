/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_EMIT_H
#define DOE_TSIR_EMIT_H

#include "tsir_schema.h"

/* Emitters, mirroring emit_semantic_metadata.zig,
 * emit_text_skeleton.zig, emit_dxil.zig, emit_msl.zig,
 * emit_spir_v.zig, emit_webgpu.zig (realization-only path),
 * emit_dtype_routing.zig, emit_kernel_context.zig, and
 * emit_csl_f16_pack.zig (text helpers only).
 *
 * All writers append to a TsirBuffer, preserving the first error and
 * never publishing partial output on failure: the emit functions
 * below return TSIR_OK with *out_text owned by the caller (free it),
 * or an error with *out_text left NULL.
 *
 * Realization-only skeleton emission is complete and byte-exact for
 * the DXIL, MSL, and SPIR-V specs and for the WebGPU realization-only
 * shape. Semantic-aware emission performs every Zig gate (rejection
 * gate, index gate, target-descriptor-hash gate) and then returns
 * TSIR_ERR_UNSUPPORTED, because the kernel-body writers it would call
 * are not implemented (see gaps.md, E13). That failure is visible,
 * not a skeleton with a fabricated body.
 *
 * Target descriptor hash: Zig computes it in the targets module
 * (outside TSIR). Callers supply the 32-byte hash and the target name
 * explicitly; the emitter compares the supplied hash against the
 * realization function's stored hash, which is exactly the check the
 * Zig skeleton performs once the hash exists. */

typedef struct TsirBackendTextSpec {
    const char *version_key;
    const char *body_comment;
} TsirBackendTextSpec;

extern const TsirBackendTextSpec tsir_spec_dxil;
extern const TsirBackendTextSpec tsir_spec_msl;
extern const TsirBackendTextSpec tsir_spec_spir_v;

/* Semantic metadata writers (emit_semantic_metadata.zig). Each block
 * ends with a blank line, exactly as in Zig. */
void tsir_emit_write_residency(TsirBuffer *buf,
    const TsirResidencyDecision *decisions, size_t count);
void tsir_emit_write_tiles(TsirBuffer *buf, const uint32_t *tiles,
    size_t count);
void tsir_emit_write_collectives(TsirBuffer *buf,
    const TsirCollectiveRealizationNode *nodes, size_t count);
void tsir_emit_write_reductions(TsirBuffer *buf,
    const TsirReductionRealizationNode *nodes, size_t count);
void tsir_emit_write_hash(TsirBuffer *buf, const uint8_t hash[32]);
void tsir_emit_write_quoted(TsirBuffer *buf, const char *text);

/* Realization-only skeleton for one realization function. */
TsirStatus tsir_emit_skeleton_function(
    const TsirRealizationFunction *function, const char *target_name,
    const uint8_t target_descriptor_hash[32],
    const TsirBackendTextSpec *spec, char **out_text, size_t *out_len);

/* Realization-only skeleton for one function of a realization. */
TsirStatus tsir_emit_skeleton(const TsirRealization *realization,
    size_t function_index, const char *target_name,
    const uint8_t target_descriptor_hash[32],
    const TsirBackendTextSpec *spec, char **out_text, size_t *out_len);

/* Semantic-aware emission: gates, contract header, metadata, then
 * the kernel body for the backend named by spec->version_key
 * (dxil, msl, spir_v, webgpu). The hash gate compares
 * target_descriptor_hash against the realization function's stored
 * hash. On any failure no partial text is published. */
TsirStatus tsir_emit_semantic_function(
    const TsirSemanticFunction *semantic_function,
    const TsirRealizationFunction *function, const char *target_name,
    const uint8_t target_descriptor_hash[32],
    const TsirBackendTextSpec *spec, char **out_text, size_t *out_len);
TsirStatus tsir_emit_semantic(const TsirSemantic *semantic,
    const TsirRealization *realization, size_t function_index,
    const char *target_name, const uint8_t target_descriptor_hash[32],
    const TsirBackendTextSpec *spec, char **out_text, size_t *out_len);

/* Kernel-backend order, matching emit_kernel_body.zig Backend:
 * webgpu, csl, msl, dxil, spir_v. CSL returns
 * TSIR_ERR_UNSUPPORTED: CSL bodies are quarantined and are not
 * built in this directory. */
typedef enum TsirKernelBackend {
    TSIR_KERNEL_WEBGPU = 0,
    TSIR_KERNEL_CSL = 1,
    TSIR_KERNEL_MSL = 2,
    TSIR_KERNEL_DXIL = 3,
    TSIR_KERNEL_SPIR_V = 4
} TsirKernelBackend;

/* Kernel-body dispatch (tsir_kernel_body.c): WGSL, MSL, DXIL and
 * SPIR-V bodies for fused_gemv, rms_norm, gather; every other op
 * and the CSL backend return TSIR_ERR_UNSUPPORTED, and a failure
 * sets buf->err (first error preserved) with no body text. */
TsirStatus tsir_emit_kernel_body(const TsirSemanticFunction *func,
    int backend, TsirBuffer *buf);

/* Dtype routing (emit_dtype_routing.zig). */
const char *tsir_csl_elem_name(TsirScalarKind elem);
const char *tsir_wgsl_elem_name(TsirScalarKind elem);
bool tsir_is_supported_compute_elem(TsirScalarKind elem);
TsirStatus tsir_require_supported_compute_elem(TsirScalarKind elem);
void tsir_write_wgsl_f16_enable(TsirBuffer *buf, TsirScalarKind elem);

/* Kernel context (emit_kernel_context.zig). */
typedef enum TsirKvCachePeStrategy {
    TSIR_KV_FULL_PER_PE = 0,
    TSIR_KV_SLOT_SHARDED = 1
} TsirKvCachePeStrategy;

typedef enum TsirAttentionPeStrategy {
    TSIR_ATTN_FULL_PER_PE = 0,
    TSIR_ATTN_KV_AXIS_SHARDED = 1
} TsirAttentionPeStrategy;

typedef enum TsirAttentionPeIdSource {
    TSIR_ATTN_ID_TILE_PARAM = 0,
    TSIR_ATTN_ID_LAYOUT_COORDINATES = 1
} TsirAttentionPeIdSource;

typedef struct TsirEmitConfig {
    const char *var_prefix;
    TsirOptU32 chunk_size_default;
    TsirOptU32 hidden_size_default;
    bool gemma_one_plus_weight_offset;
    TsirOptU32 head_dim_default;
    TsirOptU32 max_seq_len_default;
    TsirOptU32 read_len_default;
    TsirKvCachePeStrategy kv_cache_pe_strategy;
    TsirOptU32 kv_slots_per_pe_default;
    TsirAttentionPeStrategy attention_pe_strategy;
    TsirAttentionPeIdSource attention_pe_id_source;
    TsirOptU32 attention_slots_per_pe_default;
} TsirEmitConfig;

void tsir_emit_default_config(TsirEmitConfig *config);
TsirStatus tsir_binding_for_role(const TsirSemanticFunction *func,
    TsirSemanticBindingRole role, const TsirBufferBinding **out);
TsirStatus tsir_binding_for_index(const TsirSemanticFunction *func,
    uint32_t binding_index, const TsirBufferBinding **out);
TsirStatus tsir_require_elem(const TsirBufferBinding *binding,
    TsirScalarKind elem);
void tsir_write_csl_sqrt_nr(TsirBuffer *buf, TsirScalarKind elem);
void tsir_write_csl_buffer_array(TsirBuffer *buf, const char *prefix,
    const char *name, const char *extent, const char *elem_type);
void tsir_write_csl_buffer_pointer(TsirBuffer *buf, const char *prefix,
    const char *name, const char *elem_type);
void tsir_write_csl_export_symbol(TsirBuffer *buf, const char *prefix,
    const char *name);

/* CSL f16 pack helpers (emit_csl_f16_pack.zig, text only). */
void tsir_write_csl_f16_buffer_array(TsirBuffer *buf, const char *prefix,
    const char *name, const char *extent);
void tsir_write_csl_f16_buffer_pointer(TsirBuffer *buf, const char *prefix,
    const char *name);
void tsir_write_csl_f16_pack_loop(TsirBuffer *buf, const char *prefix,
    const char *name, const char *extent);
void tsir_write_csl_f16_export_symbol(TsirBuffer *buf, const char *prefix,
    const char *name);

#endif
