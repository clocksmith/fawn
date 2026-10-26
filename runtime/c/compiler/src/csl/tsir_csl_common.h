// SPDX-License-Identifier: MIT
#ifndef DOE_TSIR_CSL_COMMON_H
#define DOE_TSIR_CSL_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "csl_error.h"
#include "csl_text.h"

/* TSIR CSL-only kernel-body emitters, C port of the Zig files listed
 * in tsir-mapping.md. UNTESTED. DO NOT PUSH. PARITY NOT VERIFIED.
 *
 * Key seam, read this first: there is no C WGSL IR module in scope,
 * so these emitters do NOT take a Zig IR module or a SemanticFunction.
 * They take a TsirCslKernelDesc: a kernel descriptor whose fields are
 * exactly the IR-derived inputs the Zig emitters read (pattern, dims,
 * dtype, bindings). Anything the Zig emitters read that has no
 * descriptor field here is either a layout-supplied CSL param (named
 * in the per-op notes below, never invented as a descriptor value)
 * or an explicit CSL_ERR_UNSUPPORTED at the point it would be needed.
 *
 * Ownership and failure, mirroring the shared CSL infrastructure:
 *   - The caller owns the CslBuf and every string/pointer stored in
 *     a descriptor. This port copies nothing and frees nothing.
 *   - On any error the emitter returns the first CslErr and stops.
 *     The buffer may then hold partial CSL text; the caller must
 *     discard it. No partial output is ever presented as a body.
 *   - There are no hidden globals. All state is in the descriptor,
 *     the config, or the caller-owned buffer.
 *
 * Error mapping from Zig EmitError:
 *   - InvalidBodyContract -> CSL_ERR_INVALID (bad dims, bad mode,
 *     missing literal, eps < 0, non-read-write state, and the
 *     emit_csl gates: rejected realization, descriptor-hash
 *     mismatch, caller-supplied index out of range).
 *   - MissingBindingRole, UnsupportedScalarKind,
 *     UnsupportedKernelBody -> CSL_ERR_UNSUPPORTED (visible, never
 *     a silent stub).
 *   - Allocator failure -> CSL_ERR_NOMEM, buffer growth failure ->
 *     CSL_ERR_NOMEM or CSL_ERR_TOO_LARGE, as csl_text reports them.
 *
 * Relationship to emit/csl: these kernel bodies are the CSL bodies
 * that the emit/csl semantic ops (for example emitGeluPe in
 * emit_csl_semantic_ops.zig) need. That semantic-op file is NOT
 * duplicated here; this port only supplies the TSIR-side bodies.
 * The f16-pack helpers (emit_csl_f16_pack.zig) are likewise NOT
 * re-ported: they already exist in tsir_emit.c, referenced by
 * interface name only.
 *
 * Float literals: Zig prints scale/eps with {e} (shortest scientific
 * form). This port prints them with the helper in tsir_csl_common.c
 * (fixed 6-digit %e form). The numeric value is the same when parsed,
 * but the literal text is NOT byte-identical to Zig. Parity tests
 * must therefore parse the literal, never string-compare it. See
 * tsir-tests.md and the per-file divergences in tsir-mapping.md. */

typedef enum TsirCslElem {
    TSIR_CSL_F32 = 0,
    TSIR_CSL_F16 = 1,
    TSIR_CSL_BF16 = 2,
    TSIR_CSL_I32 = 3,
    TSIR_CSL_U32 = 4
} TsirCslElem;

/* Binding roles, mirroring schema.SemanticBindingRole values used by
 * the seven in-scope Zig files. Roles used only by the excluded
 * emit_kernel_body.zig bodies are deliberately absent. */
typedef enum TsirCslRole {
    TSIR_CSL_ROLE_QUERY = 0,
    TSIR_CSL_ROLE_KEY = 1,
    TSIR_CSL_ROLE_VALUE = 2,
    TSIR_CSL_ROLE_OUTPUT = 3,
    TSIR_CSL_ROLE_INPUT = 4,
    TSIR_CSL_ROLE_WEIGHT = 5,
    TSIR_CSL_ROLE_BIAS = 6,
    TSIR_CSL_ROLE_GATE = 7,
    TSIR_CSL_ROLE_LINEAR_STATE = 8,
    TSIR_CSL_ROLE_KEY_PROJECTION = 9,
    TSIR_CSL_ROLE_VALUE_PROJECTION = 10,
    TSIR_CSL_ROLE_KEY_CACHE = 11,
    TSIR_CSL_ROLE_VALUE_CACHE = 12,
    TSIR_CSL_ROLE_DECODE_POSITION = 13,
    TSIR_CSL_ROLE_KEY_OUTPUT = 14,
    TSIR_CSL_ROLE_VALUE_OUTPUT = 15
} TsirCslRole;

typedef struct TsirCslBinding {
    /* CSL identifier, as Zig binding.name. The caller guarantees it
     * is a valid CSL identifier; this port does not sanitize it. */
    const char *name;
    TsirCslElem elem;
    /* 1 when the buffer is read-write (schema BufferBinding
     * read_write); only linear_state is checked for this in Zig. */
    int read_write;
    TsirCslRole role;
} TsirCslBinding;

typedef enum TsirCslKvStrategy {
    TSIR_CSL_KV_FULL_PER_PE = 0,
    TSIR_CSL_KV_SLOT_SHARDED = 1
} TsirCslKvStrategy;

typedef enum TsirCslAttnStrategy {
    TSIR_CSL_ATTN_FULL_PER_PE = 0,
    TSIR_CSL_ATTN_KV_AXIS_SHARDED = 1
} TsirCslAttnStrategy;

typedef enum TsirCslAttnPeIdSource {
    TSIR_CSL_ATTN_ID_TILE_PARAM = 0,
    TSIR_CSL_ATTN_ID_LAYOUT_COORDINATES = 1
} TsirCslAttnPeIdSource;

/* Mirrors emit_kernel_context.zig Config. Zig optional fields are
 * represented as has_* plus value, exactly as TsirEmitConfig does in
 * tsir_emit.h. Fields not read by the seven in-scope files
 * (gemma_one_plus_weight_offset, hidden_size_default,
 * read_len_default) are kept so a future integration can pass one
 * TsirEmitConfig-shaped struct without a second interface; they are
 * documented as unused by this port, not silently honoured. */
typedef struct TsirCslConfig {
    /* Default "tsir_" when NULL is handled by the validate helper:
     * a NULL config pointer means all defaults. When a config is
     * supplied, var_prefix must be non-NULL (empty string allowed,
     * matching the live emit_csl_semantic_ops path). */
    const char *var_prefix;
    int has_chunk_size_default;
    uint32_t chunk_size_default;
    int has_hidden_size_default;
    uint32_t hidden_size_default;
    int gemma_one_plus_weight_offset;
    int has_head_dim_default;
    uint32_t head_dim_default;
    int has_max_seq_len_default;
    uint32_t max_seq_len_default;
    int has_read_len_default;
    uint32_t read_len_default;
    TsirCslKvStrategy kv_cache_pe_strategy;
    int has_kv_slots_per_pe_default;
    uint32_t kv_slots_per_pe_default;
    TsirCslAttnStrategy attention_pe_strategy;
    TsirCslAttnPeIdSource attention_pe_id_source;
    int has_attention_slots_per_pe_default;
    uint32_t attention_slots_per_pe_default;
} TsirCslConfig;

typedef enum TsirCslOp {
    TSIR_CSL_OP_ATTENTION_SCORES = 0,
    TSIR_CSL_OP_CONV1D_DEPTHWISE = 1,
    TSIR_CSL_OP_GELU_GATED = 2,
    TSIR_CSL_OP_SILU_GATED = 3,
    TSIR_CSL_OP_SIGMOID_GATED = 4,
    TSIR_CSL_OP_KV_WRITE = 5,
    TSIR_CSL_OP_KV_READ = 6,
    TSIR_CSL_OP_L2_NORMALIZE = 7,
    TSIR_CSL_OP_LINEAR_ATTENTION = 8,
    TSIR_CSL_OP_UNKNOWN = 9
} TsirCslOp;

typedef enum TsirCslSoftmaxMode {
    TSIR_CSL_SOFTMAX_TWO_PASS_STABLE = 0,
    TSIR_CSL_SOFTMAX_STREAMING_ONLINE = 1
} TsirCslSoftmaxMode;

typedef enum TsirCslCausalMode {
    TSIR_CSL_CAUSAL_NONE = 0,
    TSIR_CSL_CAUSAL_CAUSAL = 1,
    TSIR_CSL_CAUSAL_SLIDING_WINDOW = 2
} TsirCslCausalMode;

typedef enum TsirCslScaleSource {
    TSIR_CSL_SCALE_LITERAL_F32 = 0,
    TSIR_CSL_SCALE_UNIFORM_FIELD = 1
} TsirCslScaleSource;

typedef enum TsirCslNormMode {
    TSIR_CSL_NORM_SHARED = 0,
    TSIR_CSL_NORM_PER_HEAD = 1
} TsirCslNormMode;

/* Body parameters, one group per op. Only the group matching desc->op
 * is read. Every Zig body field that the seven files read is here;
 * fields Zig reads from elsewhere (layout-supplied CSL params such
 * as kv_len, num_tokens, value_dim_per_pe, a_log, slots_per_pe when
 * no config default exists) are deliberately NOT fields: they are
 * emitted as CSL params and supplied by the layout/host at run time,
 * exactly as in Zig. */
typedef struct TsirCslBodyParams {
    /* AttentionScoresBody. */
    uint32_t attn_head_dim;
    TsirCslSoftmaxMode attn_softmax_mode;
    TsirCslCausalMode attn_causal_mode;
    int attn_has_softcap;
    TsirCslScaleSource attn_scale_source;
    int attn_has_scale_literal;
    double attn_scale_literal;
    int attn_has_sliding_window_size;
    uint32_t attn_sliding_window_size;
    /* Absent means 1 (single-Q decode shape), as in Zig where
     * query_seq_len is ?u32 defaulting to 1. */
    int attn_has_query_seq_len;
    uint32_t attn_query_seq_len;
    /* Conv1DDepthwiseBody. */
    uint32_t conv_channels;
    uint32_t conv_kernel_size;
    int conv_has_bias;
    /* L2NormalizeBody. eps is f32 in Zig; stored as double here so
     * the caller passes the exact f32 value widened, never a
     * re-parsed decimal. */
    uint32_t l2_hidden;
    double l2_eps;
    /* LinearAttentionBody. */
    uint32_t lin_key_dim;
    uint32_t lin_value_dim;
    uint32_t lin_key_heads;
    uint32_t lin_value_heads;
    TsirCslNormMode lin_norm_mode;
    int lin_has_dt_bias;
} TsirCslBodyParams;

typedef struct TsirCslKernelDesc {
    TsirCslOp op;
    const TsirCslBinding *bindings;
    size_t binding_count;
    /* NULL means Zig default_config (var_prefix "tsir_", all
     * defaults absent, full_per_pe strategies, tile_param pe id). */
    const TsirCslConfig *config;
    TsirCslBodyParams body;
} TsirCslKernelDesc;

/* Fill *out with Zig defaults (var_prefix "tsir_", no defaults,
 * full_per_pe, tile_param). The caller may then override fields. */
void tsir_csl_default_config(TsirCslConfig *out);

/* CSL type spelling for an element, mirroring cslElemName:
 * f32, f16, bf16, i32, u32. Returns NULL for an unknown element. */
const char *tsir_csl_elem_name(TsirCslElem elem);

/* True only for f32 and f16, mirroring isSupportedComputeElem. */
int tsir_csl_is_supported_compute_elem(TsirCslElem elem);
CslErr tsir_csl_require_supported_compute_elem(TsirCslElem elem);

/* Validate a descriptor and resolve its config: checks desc and
 * binding names non-NULL/non-empty, and returns the effective config
 * (the supplied one, or defaults written into *default_storage when
 * desc->config is NULL). Returns CSL_ERR_INVALID on a bad
 * descriptor, CSL_ERR_UNSUPPORTED never. The caller must keep
 * default_storage alive for the duration of the emit call. */
CslErr tsir_csl_resolve_config(const TsirCslKernelDesc *desc,
    TsirCslConfig *default_storage, const TsirCslConfig **out_config);

/* First binding with the given role, or CSL_ERR_UNSUPPORTED when no
 * binding carries that role (Zig MissingBindingRole). */
CslErr tsir_csl_binding_for_role(const TsirCslKernelDesc *desc,
    TsirCslRole role, const TsirCslBinding **out);

/* CSL_ERR_UNSUPPORTED when binding->elem differs from elem. */
CslErr tsir_csl_require_elem(const TsirCslBinding *binding,
    TsirCslElem elem);

/* CSL text helpers, mirroring emit_kernel_context.zig exactly:
 *   array:   var {prefix}{name}: [{extent}]{ty} = @zeros(...);
 *   pointer: var {prefix}{name}_ptr: [*]{ty} = &{prefix}{name};
 *   export:  "    @export_symbol({prefix}{name}_ptr, \"{name}\");\n"
 *   sqrt_nr: f32 form, or the f16 form widening through f32. */
CslErr tsir_csl_write_buffer_array(CslBuf *out, const char *prefix,
    const char *name, const char *extent, const char *elem_type);
CslErr tsir_csl_write_buffer_pointer(CslBuf *out, const char *prefix,
    const char *name, const char *elem_type);
CslErr tsir_csl_write_export_symbol(CslBuf *out, const char *prefix,
    const char *name);
CslErr tsir_csl_write_sqrt_nr(CslBuf *out, TsirCslElem elem);

/* Single-line CSL imports, identical in every emitter that uses
 * them; shared so the spelling cannot drift between emitters. */
CslErr tsir_csl_write_sys_mod(CslBuf *out);
CslErr tsir_csl_write_math_import(CslBuf *out);

/* Shared compute tail, byte-identical in every kernel body in
 * this port: unblock, close compute, comptime block exporting
 * each named buffer (in the given order) plus compute, close.
 * names are CSL identifiers owned by the caller; count may not
 * be zero. This replaces the per-emitter tail blocks that were
 * previously copied verbatim into all seven bodies. */
CslErr tsir_csl_write_compute_tail(CslBuf *out, const char *prefix,
    const char *const *names, size_t count);

/* Scientific float literal for a scale/eps value, in the %e form
 * documented in the header comment (NOT Zig {e} byte-identical).
 * out is always NUL-terminated on success. */
CslErr tsir_csl_format_scientific(char *out, size_t cap, double value);

#endif
