/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_REFERENCE_H
#define DOE_TSIR_REFERENCE_H

#include "tsir_schema.h"

/* Reference interpreter, mirroring reference_interpreter.zig,
 * reference_reduction.zig, reference_scalar.zig, reference_types.zig,
 * and reference_inputs.zig for the implemented paths (empty kernel,
 * simple reduction, fused GEMV, RMSNorm, gather). Dispatch order
 * matches Zig run(): rejections first, then empty, simple reduction,
 * GEMV, RMSNorm, gather, then TSIR_ERR_UNSUPPORTED for every other
 * shape (including all extended-op shapes). No output buffer is
 * allocated on a failure return.
 *
 * Ownership: on success the caller owns result.outputs (each buffer
 * and the array itself) and releases them with
 * tsir_reference_free_result. The reference_hash is SHA-256 over the
 * concatenation of the output buffers in declared order.
 *
 * Deliberate divergence: Zig traps (unreachable) when a
 * softmax_stable op reaches the scalar fold helpers. C returns
 * TSIR_ERR_INVALID instead; neither path produces a value. See
 * parity-mapping.md R5/R6 and gaps.md. */

typedef struct TsirInputBuffer {
    const uint8_t *data;
    size_t len;
} TsirInputBuffer;

typedef struct TsirReferenceResult {
    uint8_t reference_hash[32];
    uint8_t **outputs;
    size_t *output_lens;
    size_t output_count;
} TsirReferenceResult;

TsirStatus tsir_reference_run(const TsirSemantic *semantic,
    const TsirRealization *realization, const TsirInputBuffer *inputs,
    size_t input_count, TsirReferenceResult *out);
void tsir_reference_free_result(TsirReferenceResult *result);

/* Declared transcendental ULP bounds (metadata only; the minimax
 * polynomial implementations live in the missing extended-op paths). */
typedef struct TsirTranscendentalTable {
    uint32_t exp_ulp_bound;
    uint32_t log_ulp_bound;
    uint32_t sin_ulp_bound;
    uint32_t cos_ulp_bound;
    uint32_t tan_ulp_bound;
    uint32_t tanh_ulp_bound;
    uint32_t rsqrt_ulp_bound;
    uint32_t recip_ulp_bound;
} TsirTranscendentalTable;

TsirTranscendentalTable tsir_transcendentals(void);

#endif
