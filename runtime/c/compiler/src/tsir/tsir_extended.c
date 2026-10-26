/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_extended.h"
#include "tsir_extended_internal.h"

#include "tsir_reference_internal.h"
#include "tsir_scalar.h"
#include "tsir_transcendental.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool tsir_ext_one_function(const TsirSemantic *semantic,
    const TsirSemanticFunction **func_out) {
    if (semantic->function_count != 1) {
        return false;
    }
    *func_out = &semantic->functions[0];
    return true;
}

/* Input slot for a binding, following reference_inputs.zig:
 * inputs are ordered by ascending binding index among read-only
 * bindings only; a read-write binding has no input slot. */
bool tsir_ext_get_input(const TsirSemanticFunction *func,
    const TsirInputBuffer *inputs, size_t input_count,
    uint32_t binding_index, const uint8_t **data_out,
    size_t *len_out) {
    size_t slot = 0;
    size_t i = 0;
    if (binding_index >= func->binding_count) {
        return false;
    }
    if (func->bindings[binding_index].read_write) {
        return false;
    }
    for (i = 0; i < binding_index; i++) {
        if (!func->bindings[i].read_write) {
            slot += 1;
        }
    }
    if (slot >= input_count) {
        return false;
    }
    *data_out = inputs[slot].data;
    *len_out = inputs[slot].len;
    return true;
}

bool tsir_ext_read_f32(const uint8_t *bytes, size_t len, size_t index,
    float *out) {
    return tsir_read_f32(bytes, len, TSIR_SCALAR_F32, index,
        out) == TSIR_OK;
}

bool tsir_ext_write_f32(uint8_t *bytes, size_t len, size_t index,
    float value) {
    return tsir_write_f32(bytes, len, TSIR_SCALAR_F32, index,
        value) == TSIR_OK;
}

bool tsir_ext_expected_bytes(const TsirBufferBinding *binding,
    uint64_t *out) {
    return tsir_expected_bytes(binding, out) == TSIR_OK;
}

/* Find a role exactly once; duplicates or absence -> false.
 * (Attention and conv1d use Zig's last-wins rule instead and do
 * not use this helper.) */
bool tsir_ext_find_role_once(const TsirSemanticBody *body,
    TsirSemanticBindingRole role, uint32_t *out) {
    bool found = false;
    size_t i = 0;
    for (i = 0; i < body->binding_role_count; i++) {
        if (body->binding_roles[i].role == role) {
            if (found) {
                return false;
            }
            found = true;
            *out = body->binding_roles[i].binding_index;
        }
    }
    return found;
}

bool tsir_ext_roles_exact(const TsirSemanticBody *body,
    const TsirSemanticBindingRole *roles, size_t count) {
    size_t i = 0;
    if (body->binding_role_count != count) {
        return false;
    }
    for (i = 0; i < count; i++) {
        uint32_t index = 0;
        if (!tsir_ext_find_role_once(body, roles[i], &index)) {
            return false;
        }
    }
    return true;
}

static int try_identity(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out) {
    const TsirSemanticFunction *func = NULL;
    size_t read_index = 0;
    size_t write_index = 0;
    bool have_read = false;
    bool have_write = false;
    size_t i = 0;
    const TsirBufferBinding *rb = NULL;
    const TsirBufferBinding *wb = NULL;
    uint64_t expected = 0;
    uint8_t *output = NULL;
    (void)err_out;
    if (!tsir_ext_one_function(semantic, &func)) {
        return TRY_NOMATCH;
    }
    if (func->reduction_count != 0 || func->collective_count != 0) {
        return TRY_NOMATCH;
    }
    for (i = 0; i < func->binding_count; i++) {
        if (func->bindings[i].read_write) {
            if (have_write) {
                return TRY_NOMATCH;
            }
            have_write = true;
            write_index = i;
        } else {
            if (have_read) {
                return TRY_NOMATCH;
            }
            have_read = true;
            read_index = i;
        }
    }
    if (!have_read || !have_write) {
        return TRY_NOMATCH;
    }
    rb = &func->bindings[read_index];
    wb = &func->bindings[write_index];
    if (rb->elem != wb->elem) {
        return TRY_NOMATCH;
    }
    if (rb->logical_shape_len != wb->logical_shape_len) {
        return TRY_NOMATCH;
    }
    for (i = 0; i < rb->logical_shape_len; i++) {
        if (rb->logical_shape[i] != wb->logical_shape[i]) {
            return TRY_NOMATCH;
        }
    }
    if (input_count != 1) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_expected_bytes(rb, &expected)) {
        return TRY_NOMATCH;
    }
    if (inputs[0].len != expected) {
        return TRY_NOMATCH;
    }
    output = (uint8_t *)malloc(expected == 0 ? 1 : expected);
    if (output == NULL) {
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    memcpy(output, inputs[0].data, expected);
    if (finish_result(result, output, expected) != TSIR_OK) {
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    return TRY_MATCH;
}

static int try_residual(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out) {
    const TsirSemanticFunction *func = NULL;
    uint32_t ai = 0;
    uint32_t bi = 0;
    uint32_t oi = 0;
    const TsirBufferBinding *ab = NULL;
    const TsirBufferBinding *bb = NULL;
    const TsirBufferBinding *ob = NULL;
    const uint8_t *a_bytes = NULL;
    const uint8_t *b_bytes = NULL;
    size_t a_len = 0;
    size_t b_len = 0;
    uint64_t expected_in = 0;
    uint64_t expected_out = 0;
    uint8_t *output = NULL;
    size_t len = 0;
    size_t i = 0;
    static const TsirSemanticBindingRole roles[3] = {
        TSIR_ROLE_SUMMAND_A, TSIR_ROLE_SUMMAND_B, TSIR_ROLE_OUTPUT
    };
    (void)err_out;
    if (!tsir_ext_one_function(semantic, &func)) {
        return TRY_NOMATCH;
    }
    if (func->collective_count != 0 || func->reduction_count != 0) {
        return TRY_NOMATCH;
    }
    if (func->binding_count != 3 || func->axis_count != 1) {
        return TRY_NOMATCH;
    }
    if (func->body.op != TSIR_BODY_RESIDUAL_ADD) {
        return TRY_NOMATCH;
    }
    if (func->body.axis_role_count != 1 ||
        func->body.axis_roles[0].role != TSIR_AXIS_HIDDEN) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_roles_exact(&func->body, roles, 3)) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_find_role_once(&func->body, TSIR_ROLE_SUMMAND_A, &ai) ||
        !tsir_ext_find_role_once(&func->body, TSIR_ROLE_SUMMAND_B, &bi) ||
        !tsir_ext_find_role_once(&func->body, TSIR_ROLE_OUTPUT, &oi)) {
        return TRY_NOMATCH;
    }
    if (ai >= func->binding_count || bi >= func->binding_count ||
        oi >= func->binding_count) {
        return TRY_NOMATCH;
    }
    if (ai == bi || bi == oi || ai == oi) {
        return TRY_NOMATCH;
    }
    ab = &func->bindings[ai];
    bb = &func->bindings[bi];
    ob = &func->bindings[oi];
    if (ab->read_write || bb->read_write || !ob->read_write) {
        return TRY_NOMATCH;
    }
    if (ab->elem != TSIR_SCALAR_F32 ||
        bb->elem != TSIR_SCALAR_F32 ||
        ob->elem != TSIR_SCALAR_F32) {
        return TRY_NOMATCH;
    }
    if (ab->logical_shape_len != 1 || bb->logical_shape_len != 1 ||
        ob->logical_shape_len != 1) {
        return TRY_NOMATCH;
    }
    if (bb->logical_shape[0] != ab->logical_shape[0] ||
        ob->logical_shape[0] != ab->logical_shape[0]) {
        return TRY_NOMATCH;
    }
    if (ab->logical_shape[0] > (uint64_t)SIZE_MAX) {
        return TRY_NOMATCH;
    }
    len = (size_t)ab->logical_shape[0];
    if (input_count != tsir_count_read_only_bindings(func)) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_get_input(func, inputs, input_count, ai, &a_bytes,
        &a_len) ||
        !tsir_ext_get_input(func, inputs, input_count, bi, &b_bytes,
            &b_len)) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_expected_bytes(ab, &expected_in) ||
        a_len != expected_in || b_len != expected_in) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_expected_bytes(ob, &expected_out)) {
        return TRY_NOMATCH;
    }
    output = (uint8_t *)malloc(
        expected_out == 0 ? 1 : expected_out);
    if (output == NULL) {
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    for (i = 0; i < len; i++) {
        float a = 0.0f;
        float b = 0.0f;
        if (!tsir_ext_read_f32(a_bytes, a_len, i, &a) ||
            !tsir_ext_read_f32(b_bytes, b_len, i, &b) ||
            !tsir_ext_write_f32(output, expected_out, i, a + b)) {
            free(output);
            return TRY_NOMATCH;
        }
    }
    if (finish_result(result, output, expected_out) != TSIR_OK) {
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    return TRY_MATCH;
}

static int try_gated(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out) {
    const TsirSemanticFunction *func = NULL;
    uint32_t gi = 0;
    uint32_t ii = 0;
    uint32_t oi = 0;
    const TsirBufferBinding *gb = NULL;
    const TsirBufferBinding *ib = NULL;
    const TsirBufferBinding *ob = NULL;
    const uint8_t *gate_bytes = NULL;
    const uint8_t *input_bytes = NULL;
    size_t gate_len = 0;
    size_t input_len = 0;
    uint64_t expected_in = 0;
    uint64_t expected_out = 0;
    uint8_t *output = NULL;
    size_t len = 0;
    size_t i = 0;
    int kind = 0;
    static const TsirSemanticBindingRole roles[3] = {
        TSIR_ROLE_GATE, TSIR_ROLE_INPUT, TSIR_ROLE_OUTPUT
    };
    (void)err_out;
    if (!tsir_ext_one_function(semantic, &func)) {
        return TRY_NOMATCH;
    }
    if (func->collective_count != 0 || func->reduction_count != 0) {
        return TRY_NOMATCH;
    }
    if (func->binding_count != 3 || func->axis_count != 1) {
        return TRY_NOMATCH;
    }
    if (func->body.op == TSIR_BODY_GELU_GATED) {
        kind = 0;
    } else if (func->body.op == TSIR_BODY_SILU_GATED) {
        kind = 1;
    } else if (func->body.op == TSIR_BODY_SIGMOID_GATED) {
        kind = 2;
    } else {
        return TRY_NOMATCH;
    }
    if (func->body.axis_role_count != 1 ||
        func->body.axis_roles[0].role != TSIR_AXIS_HIDDEN) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_roles_exact(&func->body, roles, 3)) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_find_role_once(&func->body, TSIR_ROLE_GATE, &gi) ||
        !tsir_ext_find_role_once(&func->body, TSIR_ROLE_INPUT, &ii) ||
        !tsir_ext_find_role_once(&func->body, TSIR_ROLE_OUTPUT, &oi)) {
        return TRY_NOMATCH;
    }
    if (gi >= func->binding_count || ii >= func->binding_count ||
        oi >= func->binding_count) {
        return TRY_NOMATCH;
    }
    if (gi == ii || ii == oi || gi == oi) {
        return TRY_NOMATCH;
    }
    gb = &func->bindings[gi];
    ib = &func->bindings[ii];
    ob = &func->bindings[oi];
    if (gb->read_write || ib->read_write || !ob->read_write) {
        return TRY_NOMATCH;
    }
    if (gb->elem != TSIR_SCALAR_F32 ||
        ib->elem != TSIR_SCALAR_F32 ||
        ob->elem != TSIR_SCALAR_F32) {
        return TRY_NOMATCH;
    }
    if (gb->logical_shape_len != 1 || ib->logical_shape_len != 1 ||
        ob->logical_shape_len != 1) {
        return TRY_NOMATCH;
    }
    if (ib->logical_shape[0] != gb->logical_shape[0] ||
        ob->logical_shape[0] != gb->logical_shape[0]) {
        return TRY_NOMATCH;
    }
    if (gb->logical_shape[0] > (uint64_t)SIZE_MAX) {
        return TRY_NOMATCH;
    }
    len = (size_t)gb->logical_shape[0];
    if (input_count != tsir_count_read_only_bindings(func)) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_get_input(func, inputs, input_count, gi, &gate_bytes,
        &gate_len) ||
        !tsir_ext_get_input(func, inputs, input_count, ii, &input_bytes,
            &input_len)) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_expected_bytes(gb, &expected_in) ||
        gate_len != expected_in || input_len != expected_in) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_expected_bytes(ob, &expected_out)) {
        return TRY_NOMATCH;
    }
    output = (uint8_t *)malloc(
        expected_out == 0 ? 1 : expected_out);
    if (output == NULL) {
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    for (i = 0; i < len; i++) {
        float x = 0.0f;
        float v = 0.0f;
        float act = 0.0f;
        if (!tsir_ext_read_f32(gate_bytes, gate_len, i, &x) ||
            !tsir_ext_read_f32(input_bytes, input_len, i, &v)) {
            free(output);
            return TRY_NOMATCH;
        }
        if (kind == 0) {
            const float gelu_a = 0.7978845608028654f;
            const float gelu_b = 0.044715f;
            float inner = gelu_a * (x + gelu_b * x * x * x);
            if (inner < -15.0f) {
                inner = -15.0f;
            }
            if (inner > 15.0f) {
                inner = 15.0f;
            }
            act = 0.5f * x * (1.0f + tsir_tanh_f32(inner));
        } else {
            float z = -x;
            if (z < -15.0f) {
                z = -15.0f;
            }
            if (z > 15.0f) {
                z = 15.0f;
            }
            if (kind == 1) {
                act = x / (1.0f + tsir_exp_f32(z));
            } else {
                act = 1.0f / (1.0f + tsir_exp_f32(z));
            }
        }
        if (!tsir_ext_write_f32(output, expected_out, i, act * v)) {
            free(output);
            return TRY_NOMATCH;
        }
    }
    if (finish_result(result, output, expected_out) != TSIR_OK) {
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    return TRY_MATCH;
}

int tsir_try_extended_ops(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out) {
    int tried = 0;
    tried = try_identity(semantic, inputs, input_count, result,
        err_out);
    if (tried != TRY_NOMATCH) {
        return tried;
    }
    tried = try_residual(semantic, inputs, input_count, result,
        err_out);
    if (tried != TRY_NOMATCH) {
        return tried;
    }
    tried = try_gated(semantic, inputs, input_count, result,
        err_out);
    if (tried != TRY_NOMATCH) {
        return tried;
    }
    tried = tsir_try_attention(semantic, inputs, input_count, result,
        err_out);
    if (tried != TRY_NOMATCH) {
        return tried;
    }
    tried = tsir_try_l2(semantic, inputs, input_count, result, err_out);
    if (tried != TRY_NOMATCH) {
        return tried;
    }
    tried = tsir_try_conv(semantic, inputs, input_count, result,
        err_out);
    if (tried != TRY_NOMATCH) {
        return tried;
    }
    tried = tsir_try_linear_attention(semantic, inputs, input_count,
        result, err_out);
    return tried;
}
