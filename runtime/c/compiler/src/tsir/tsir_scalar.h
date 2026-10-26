/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_SCALAR_H
#define DOE_TSIR_SCALAR_H

#include "tsir_schema.h"

/* Reference scalar helpers, mirroring reference_scalar.zig. All
 * conversions are explicit bit manipulations, not compiler half types,
 * so the build stays -Wpedantic clean and the rounding is reviewable.
 * f32 arithmetic in combine helpers is plain IEEE-754 float arithmetic
 * under the caller's floating-point environment; the library does not
 * change rounding modes. Integer element kinds are never valid Phase A
 * operands: read/write return TSIR_ERR_UNSUPPORTED for them instead of
 * reinterpreting bits. */

TsirStatus tsir_reduction_identity_f32(TsirReductionOp op, float *out);
TsirStatus tsir_combine_f32(TsirReductionOp op, float acc, float val,
    float *out);
TsirStatus tsir_read_f32(const uint8_t *bytes, size_t byte_len,
    TsirScalarKind elem, size_t elem_index, float *out);
TsirStatus tsir_write_f32(uint8_t *bytes, size_t byte_len,
    TsirScalarKind elem, size_t elem_index, float value);
uint16_t tsir_f32_to_bf16_rne(float value);
float tsir_bf16_to_f32(uint16_t bits);
uint16_t tsir_f32_to_f16_bits(float value);
float tsir_f16_bits_to_f32(uint16_t bits);

/* Product of logical_shape dims times elem size. A zero dimension
 * yields 0. Multiplication overflow yields TSIR_ERR_OVERFLOW and
 * leaves *out untouched. */
TsirStatus tsir_expected_bytes(const TsirBufferBinding *binding,
    uint64_t *out);

/* Read-only binding helpers, mirroring reference_inputs.zig: inputs
 * are ordered by ascending binding index among read-only bindings. */
size_t tsir_count_read_only_bindings(const TsirSemanticFunction *func);
bool tsir_input_for_read_only_binding(const TsirSemanticFunction *func,
    const uint8_t *const *inputs, const size_t *input_lens,
    size_t input_count, uint32_t binding_index,
    const uint8_t **out_data, size_t *out_len);

#endif
