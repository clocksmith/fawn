/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_EXTENDED_H
#define DOE_TSIR_EXTENDED_H

#include "tsir_reference.h"

/* reference_extended_ops.zig, in Zig dispatch order: identity,
 * residual_add, gated (gelu/silu/sigmoid), attention_scores,
 * l2_normalize, conv1d_depthwise, linear_attention. Return values
 * follow tsir_reference_internal.h: TRY_MATCH fills the result,
 * TRY_NOMATCH means the shape did not match (fall through), a
 * negative return means *err_out must be propagated immediately
 * (allocation failure only; shape mismatches are never errors).
 * Transcendental values use tsir_transcendental.h (double
 * precision, rounded to f32); bit parity against Zig for those
 * paths is unestablished, see gaps.md and tsir_transcendental.h.
 *
 * Linear attention: Zig's tryLinearAttention reads its
 * read-write state binding through inputBytesForReadOnlyBinding,
 * which returns null for any read-write binding, so the Zig path
 * always falls through for its own declared shape. This port
 * preserves that fall-through in the dispatch function below.
 * The state-update math itself is available separately as
 * tsir_linear_attention_step (a_log pinned to 0.5, exactly as
 * Zig's interpreter pins it) and is tested directly. */

int tsir_try_extended_ops(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out);

TsirStatus tsir_linear_attention_step(const float *query,
    const float *key, const float *value, const float *gate,
    const float *state_in, size_t value_dim, size_t key_dim,
    float *state_out, float *out);

#endif
