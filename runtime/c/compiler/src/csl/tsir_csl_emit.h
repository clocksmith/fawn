// SPDX-License-Identifier: MIT
#ifndef DOE_TSIR_CSL_EMIT_H
#define DOE_TSIR_CSL_EMIT_H

#include "tsir_csl_common.h"

/* Port of emit_csl.zig (237 lines), adapted to the descriptor seam
 * in tsir_csl_common.h: there is no C realization artifact or
 * target descriptor, so the caller supplies the contract fields
 * Zig reads from them explicitly.
 *
 * TsirCslRealization fields, and their Zig sources:
 *   target_name: targets.TargetDescriptor correctness name.
 *   descriptor_hash: 32 bytes, targets.descriptorHash(descriptor).
 *     Zig compares it against the realization function's stored
 *     hash; this port has no stored hash to compare against, so
 *     the caller must perform that comparison before calling (a
 *     mismatch is a caller error, not something this port can
 *     detect). The hash is only rendered into the contract header.
 *   semantic_index, pe_grid_width, pe_grid_height: the realization
 *     function's fields, rendered verbatim.
 *   emitter_params_json: the realization function's field, rendered
 *     quoted. NULL means the empty string.
 *   has_rejection: realization.rejections.len != 0. When set, both
 *     emit functions return CSL_ERR_INVALID before writing anything
 *     (Zig RejectedRealization).
 *   bindings: the semantic function's bindings, used ONLY by the
 *     semantic layout for per-binding @export_name lines. The
 *     caller must pass the same binding list as the kernel
 *     descriptor's; this port cannot cross-check a second list
 *     against a SemanticFunction it does not have.
 *   residency_text, tiles_text, collectives_text, reductions_text:
 *     pre-rendered emit_semantic_metadata.zig blocks for the
 *     pe_program header. That metadata file is NOT in scope and
 *     is NOT re-ported here; the caller supplies its output
 *     verbatim (each block ending with a blank line, as Zig
 *     writes). NULL means the empty block (count = 0 plus a
 *     blank line), which is the only metadata this port renders
 *     itself.
 *
 * What is NOT ported, visibly:
 *   - emitterCodeDigest: Zig hashes the embedded source bytes of
 *     emit_csl.zig, emit_kernel_body.zig and
 *     emit_semantic_metadata.zig. This C port has no such bytes,
 *     and hashing the C sources would produce a different digest
 *     that must never be mistaken for Zig's. The function below
 *     therefore returns CSL_ERR_UNSUPPORTED instead of a digest.
 *   - Function-index range checks: Zig checks function_index
 *     against realization.functions.len and semantic_index against
 *     semantic.functions.len. This port has no such lists, so the
 *     caller owns index validity; a wrong semantic_index is
 *     rendered, not rejected. That is a documented seam loss, not
 *     a silent success claim.
 *   - The emitSemanticFunctionWithConfig variant is folded into
 *     tsir_csl_emit_semantic: the config travels on the kernel
 *     descriptor (desc->config), so there is no second entry point.
 *   - The skeleton's stub compute body is emitted verbatim, as in
 *     Zig; it is a skeleton, not a kernel body, and must never be
 *     presented as one. */

typedef struct TsirCslRealization {
    const char *target_name;
    const unsigned char *descriptor_hash;
    uint32_t semantic_index;
    uint32_t pe_grid_width;
    uint32_t pe_grid_height;
    const char *emitter_params_json;
    int has_rejection;
    const TsirCslBinding *bindings;
    size_t binding_count;
    const char *residency_text;
    const char *tiles_text;
    const char *collectives_text;
    const char *reductions_text;
} TsirCslRealization;

/* Realization-only skeleton for one realization function
 * (Zig emitFunction): layout without memcpy import or binding
 * exports, pe_program with a stub compute function. */
CslErr tsir_csl_emit_skeleton(const TsirCslRealization *real,
    CslBuf *out);

/* Semantic-aware emission (Zig emitSemanticFunction, via the
 * default-config wrapper): layout with memcpy import and
 * per-binding exports, pe_program with contract header, metadata
 * blocks, pe_id/num_pes params, then the kernel body for desc->op
 * via tsir_csl_emit_kernel_body. Only attention_scores adds a
 * layout kv_len param, as in Zig. */
CslErr tsir_csl_emit_semantic(const TsirCslRealization *real,
    const TsirCslKernelDesc *desc, CslBuf *out);

/* Zig emitterCodeDigest, deliberately unported: always returns
 * CSL_ERR_UNSUPPORTED. See the header comment. Never fill out
 * with a hash of the C sources. */
CslErr tsir_csl_emitter_code_digest(unsigned char out_hash[32]);

#endif
