/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_FRONTEND_PASSES_H
#define DOE_TSIR_FRONTEND_PASSES_H

#include "tsir_frontend.h"

/* Frontend passes, mirroring frontend_collectives.zig,
 * frontend_rejections.zig, and frontend_body.zig. Each returns a
 * TsirStatus (TSIR_OK or TSIR_ERR_NOMEM); a failed call may have
 * appended partial output, and the caller frees the lists.
 * Collective dtype fallback and body empty-body fallbacks are
 * Zig's own: they are not errors. */

/* frontend_collectives.collectCollectives: one node per builtin
 * subgroup/workgroup call site, in statement-walk order. */
TsirStatus tsir_frontend_collect_collectives(
    const TsirWgslModule *module, const TsirIrFunction *func,
    uint32_t func_index, TsirCollectiveList *out,
    TsirRejectionList *rejections);

/* frontend_rejections.recoverRejections: one typed rejection per
 * while / bare loop and per decreasing for loop, with Zig's
 * dot-delimited node paths. */
TsirStatus tsir_frontend_recover_rejections(
    const TsirIrFunction *func, uint32_t func_index,
    TsirRejectionList *rejections);

/* frontend_body.inferSemanticBody: fused_gemv / gather role
 * assignment, or rms_norm when the binding names and equal bounds
 * match and the uniform epsilon resolves; otherwise an empty body
 * (op unknown, no roles). The returned body's role arrays and
 * rms_norm epsilon path are freshly allocated and owned by the
 * caller (freed by tsir_frontend_free_semantic when the body is
 * part of a lowered semantic). */
TsirStatus tsir_frontend_infer_body(const TsirWgslModule *module,
    TsirKernelFamilyHint hint, const TsirIterationAxis *axes,
    size_t axis_count, const TsirBufferBinding *bindings,
    size_t binding_count, const TsirReductionRegion *reductions,
    size_t reduction_count, TsirSemanticBody *out);

#endif
