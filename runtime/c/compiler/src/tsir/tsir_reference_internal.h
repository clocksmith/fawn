/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_REFERENCE_INTERNAL_H
#define DOE_TSIR_REFERENCE_INTERNAL_H

#include "tsir_reference.h"

/* Shared helpers between tsir_reference.c (dispatch, empty kernel,
 * simple reduction) and tsir_reference_bodies.c (GEMV, RMSNorm,
 * gather). Internal to this library; not part of the public API.
 * Try outcomes: TRY_MATCH fills the result, TRY_NOMATCH means the
 * shape did not match (fall through), a negative return means the
 * TsirStatus in *err_out must be propagated immediately. */
#define TRY_MATCH 1
#define TRY_NOMATCH 0

TsirStatus finish_result(TsirReferenceResult *result,
    uint8_t *single_output, size_t single_len);
bool find_read_write_pair(const TsirSemanticFunction *func,
    size_t *read_index, size_t *write_index);
int effective_tree_shape(const TsirSemanticFunction *func,
    const TsirRealization *realization,
    const TsirReductionRegion *reduction,
    TsirReductionTreeShape *out_shape);
TsirStatus fold_values(TsirReductionOp op,
    TsirReductionTreeShape shape, const float *vals, size_t count,
    float *out);
bool find_body_role(const TsirSemanticBody *body,
    TsirSemanticBindingRole role, uint32_t *out_index);
bool find_axis_role(const TsirSemanticBody *body,
    TsirSemanticAxisRole role, uint32_t *out_index);
int try_fused_gemv(const TsirSemantic *semantic,
    const TsirRealization *realization, const TsirInputBuffer *inputs,
    size_t input_count, TsirReferenceResult *result,
    TsirStatus *err_out);
int try_rms_norm(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out);
int try_gather(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out);

/* Extended ops (tsir_extended.c): identity, residual_add, gated,
 * attention_scores, l2_normalize, conv1d_depthwise,
 * linear_attention (the last always falls through, see
 * tsir_extended.h). Same TRY_MATCH / TRY_NOMATCH / negative
 * contract as the try_* helpers above. */
int tsir_try_extended_ops(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out);

#endif
