/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_EXTENDED_INTERNAL_H
#define DOE_TSIR_EXTENDED_INTERNAL_H

#include "tsir_reference.h"

bool tsir_ext_one_function(const TsirSemantic *semantic,
    const TsirSemanticFunction **func_out);
bool tsir_ext_get_input(const TsirSemanticFunction *func,
    const TsirInputBuffer *inputs, size_t input_count,
    uint32_t binding_index, const uint8_t **data_out,
    size_t *len_out);
bool tsir_ext_read_f32(const uint8_t *bytes, size_t len, size_t index,
    float *out);
bool tsir_ext_write_f32(uint8_t *bytes, size_t len, size_t index,
    float value);
bool tsir_ext_expected_bytes(const TsirBufferBinding *binding,
    uint64_t *out);
bool tsir_ext_find_role_once(const TsirSemanticBody *body,
    TsirSemanticBindingRole role, uint32_t *out);
bool tsir_ext_roles_exact(const TsirSemanticBody *body,
    const TsirSemanticBindingRole *roles, size_t count);
int tsir_try_attention(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out);
int tsir_try_l2(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out);
int tsir_try_conv(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out);
int tsir_try_linear_attention(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out);

#endif
