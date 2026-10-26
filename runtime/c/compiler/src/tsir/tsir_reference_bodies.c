/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_reference_internal.h"

#include "tsir_scalar.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- fused GEMV ---- */

int try_fused_gemv(const TsirSemantic *semantic,
    const TsirRealization *realization, const TsirInputBuffer *inputs,
    size_t input_count, TsirReferenceResult *result,
    TsirStatus *err_out) {
    const TsirSemanticFunction *func = NULL;
    const TsirReductionRegion *reduction = NULL;
    TsirReductionTreeShape shape = TSIR_TREE_LINEAR;
    uint32_t mi = 0;
    uint32_t vi = 0;
    uint32_t oi = 0;
    uint32_t out_axis = 0;
    uint32_t red_axis = 0;
    const TsirBufferBinding *mb = NULL;
    const TsirBufferBinding *vb = NULL;
    const TsirBufferBinding *ob = NULL;
    uint64_t expected_mb = 0;
    uint64_t expected_vb = 0;
    uint64_t m64 = 0;
    uint64_t k64 = 0;
    size_t m = 0;
    size_t k = 0;
    TsirStatus status = TSIR_OK;
    size_t i = 0;
    *err_out = TSIR_OK;
    if (semantic->function_count != 1) {
        return TRY_NOMATCH;
    }
    func = &semantic->functions[0];
    if (func->collective_count != 0 || func->binding_count != 3 ||
        func->reduction_count != 1 || func->axis_count != 2) {
        return TRY_NOMATCH;
    }
    if (func->body.op != TSIR_BODY_FUSED_GEMV ||
        func->body.binding_role_count != 3 ||
        func->body.axis_role_count != 2) {
        return TRY_NOMATCH;
    }
    if (!find_body_role(&func->body, TSIR_ROLE_MATRIX, &mi) ||
        !find_body_role(&func->body, TSIR_ROLE_VECTOR, &vi) ||
        !find_body_role(&func->body, TSIR_ROLE_OUTPUT, &oi)) {
        return TRY_NOMATCH;
    }
    if (mi >= func->binding_count || vi >= func->binding_count ||
        oi >= func->binding_count || mi == vi || vi == oi || mi == oi) {
        return TRY_NOMATCH;
    }
    if (!find_axis_role(&func->body, TSIR_AXIS_OUTPUT, &out_axis) ||
        !find_axis_role(&func->body, TSIR_AXIS_REDUCTION, &red_axis)) {
        return TRY_NOMATCH;
    }
    if (out_axis == red_axis || out_axis >= func->axis_count ||
        red_axis >= func->axis_count) {
        return TRY_NOMATCH;
    }
    reduction = &func->reductions[0];
    if (reduction->op != TSIR_REDUCTION_SUM ||
        reduction->axis != red_axis ||
        reduction->target_binding != oi ||
        reduction->contract.accumulation != TSIR_SCALAR_F32) {
        return TRY_NOMATCH;
    }
    /* Zig GEMV does not check nan_inf; the code is followed, not the
     * docstring summary. */
    if (effective_tree_shape(func, realization, reduction, &shape) !=
        TRY_MATCH) {
        return TRY_NOMATCH;
    }
    mb = &func->bindings[mi];
    vb = &func->bindings[vi];
    ob = &func->bindings[oi];
    if (mb->elem != vb->elem || vb->elem != ob->elem) {
        return TRY_NOMATCH;
    }
    if (mb->elem != TSIR_SCALAR_F32 && mb->elem != TSIR_SCALAR_F16 &&
        mb->elem != TSIR_SCALAR_BF16) {
        return TRY_NOMATCH;
    }
    if (mb->logical_shape_len != 2 || vb->logical_shape_len != 1 ||
        ob->logical_shape_len != 1) {
        return TRY_NOMATCH;
    }
    m64 = mb->logical_shape[0];
    k64 = mb->logical_shape[1];
    if (vb->logical_shape[0] != k64 || ob->logical_shape[0] != m64) {
        return TRY_NOMATCH;
    }
    if (mb->read_write || vb->read_write || !ob->read_write) {
        return TRY_NOMATCH;
    }
    if (m64 > SIZE_MAX || k64 > SIZE_MAX) {
        return TRY_NOMATCH;
    }
    m = (size_t)m64;
    k = (size_t)k64;
    if (out_axis != 0 || red_axis != 1) {
        return TRY_NOMATCH;
    }
    if (input_count != 2) {
        return TRY_NOMATCH;
    }
    {
        const TsirInputBuffer *matrix_bytes = NULL;
        const TsirInputBuffer *vector_bytes = NULL;
        uint8_t *output = NULL;
        size_t out_bytes = 0;
        float *scratch = NULL;
        bool matrix_first = mi < vi;
        matrix_bytes = matrix_first ? &inputs[0] : &inputs[1];
        vector_bytes = matrix_first ? &inputs[1] : &inputs[0];
        status = tsir_expected_bytes(mb, &expected_mb);
        if (status != TSIR_OK) {
            return TRY_NOMATCH;
        }
        status = tsir_expected_bytes(vb, &expected_vb);
        if (status != TSIR_OK) {
            return TRY_NOMATCH;
        }
        if (matrix_bytes->len != expected_mb ||
            vector_bytes->len != expected_vb) {
            return TRY_NOMATCH;
        }
        if (m > 0 && m > SIZE_MAX / tsir_scalar_byte_size(ob->elem)) {
            return TRY_NOMATCH;
        }
        out_bytes = m * tsir_scalar_byte_size(ob->elem);
        output = (uint8_t *)malloc(out_bytes == 0 ? 1 : out_bytes);
        if (output == NULL) {
            *err_out = TSIR_ERR_NOMEM;
            return -1;
        }
        if (shape == TSIR_TREE_BINOMIAL && k > 0) {
            scratch = (float *)malloc(k * sizeof(float));
            if (scratch == NULL) {
                free(output);
                *err_out = TSIR_ERR_NOMEM;
                return -1;
            }
        }
        if (k == 0) {
            for (i = 0; i < m; i++) {
                status = tsir_write_f32(output, out_bytes, ob->elem, i,
                    0.0f);
                if (status != TSIR_OK) {
                    free(output);
                    free(scratch);
                    *err_out = status;
                    return -1;
                }
            }
        } else {
            for (i = 0; i < m; i++) {
                if (shape == TSIR_TREE_BINOMIAL) {
                    float folded = 0.0f;
                    size_t kk = 0;
                    for (kk = 0; kk < k; kk++) {
                        float wv = 0.0f;
                        float xv = 0.0f;
                        status = tsir_read_f32(matrix_bytes->data,
                            matrix_bytes->len, mb->elem, i * k + kk, &wv);
                        if (status != TSIR_OK) {
                            free(output);
                            free(scratch);
                            *err_out = status;
                            return -1;
                        }
                        status = tsir_read_f32(vector_bytes->data,
                            vector_bytes->len, vb->elem, kk, &xv);
                        if (status != TSIR_OK) {
                            free(output);
                            free(scratch);
                            *err_out = status;
                            return -1;
                        }
                        scratch[kk] = wv * xv;
                    }
                    status = fold_values(TSIR_REDUCTION_SUM, shape,
                        scratch, k, &folded);
                    if (status != TSIR_OK) {
                        free(output);
                        free(scratch);
                        *err_out = status;
                        return -1;
                    }
                    status = tsir_write_f32(output, out_bytes, ob->elem,
                        i, folded);
                    if (status != TSIR_OK) {
                        free(output);
                        free(scratch);
                        *err_out = status;
                        return -1;
                    }
                } else {
                    float acc = 0.0f;
                    size_t kk = 0;
                    for (kk = 0; kk < k; kk++) {
                        float wv = 0.0f;
                        float xv = 0.0f;
                        status = tsir_read_f32(matrix_bytes->data,
                            matrix_bytes->len, mb->elem, i * k + kk, &wv);
                        if (status != TSIR_OK) {
                            free(output);
                            free(scratch);
                            *err_out = status;
                            return -1;
                        }
                        status = tsir_read_f32(vector_bytes->data,
                            vector_bytes->len, vb->elem, kk, &xv);
                        if (status != TSIR_OK) {
                            free(output);
                            free(scratch);
                            *err_out = status;
                            return -1;
                        }
                        acc += wv * xv;
                    }
                    status = tsir_write_f32(output, out_bytes, ob->elem,
                        i, acc);
                    if (status != TSIR_OK) {
                        free(output);
                        free(scratch);
                        *err_out = status;
                        return -1;
                    }
                }
            }
        }
        free(scratch);
        status = finish_result(result, output, out_bytes);
        if (status != TSIR_OK) {
            *err_out = status;
            return -1;
        }
        return TRY_MATCH;
    }
}

/* ---- RMSNorm ---- */

static bool resolve_rms_norm_epsilon(const TsirSemanticFunction *func,
    const TsirInputBuffer *inputs, size_t input_count,
    const TsirRmsNormEpsilon *epsilon, float *out) {
    if (epsilon->source == TSIR_EPSILON_LITERAL_F32) {
        if (epsilon->path == NULL || epsilon->path[0] != '\0') {
            return false;
        }
        if (epsilon->binding_index.has || epsilon->byte_offset.has) {
            return false;
        }
        if (!epsilon->has_literal_f64) {
            return false;
        }
        if (!isfinite(epsilon->literal_f64)) {
            return false;
        }
        *out = (float)epsilon->literal_f64;
        return true;
    }
    if (epsilon->source == TSIR_EPSILON_UNIFORM_FIELD) {
        const char *path = epsilon->path;
        const char *prefix = "uniform:";
        const char *rest = NULL;
        const char *dot = NULL;
        size_t binding_len = 0;
        const char *field = NULL;
        const TsirBufferBinding *binding = NULL;
        const TsirInputBuffer *uniform = NULL;
        size_t slot = 0;
        size_t i = 0;
        uint32_t offset = 0;
        uint32_t bits = 0;
        float value = 0.0f;
        if (path == NULL || strncmp(path, prefix, 8) != 0) {
            return false;
        }
        rest = path + 8;
        dot = strchr(rest, '.');
        if (dot == NULL || dot == rest || dot[1] == '\0') {
            return false;
        }
        binding_len = (size_t)(dot - rest);
        field = dot + 1;
        if (strcmp(field, "eps") != 0) {
            return false;
        }
        if (epsilon->has_literal_f64) {
            return false;
        }
        if (!epsilon->binding_index.has || !epsilon->byte_offset.has) {
            return false;
        }
        if (epsilon->binding_index.value >= func->binding_count) {
            return false;
        }
        binding = &func->bindings[epsilon->binding_index.value];
        if (binding->read_write) {
            return false;
        }
        if (binding->name == NULL ||
            strlen(binding->name) != binding_len ||
            strncmp(binding->name, rest, binding_len) != 0) {
            return false;
        }
        for (i = 0; i < epsilon->binding_index.value; i++) {
            if (!func->bindings[i].read_write) {
                slot += 1;
            }
        }
        if (slot >= input_count) {
            return false;
        }
        uniform = &inputs[slot];
        offset = epsilon->byte_offset.value;
        if (offset > uniform->len || uniform->len - offset < 4) {
            return false;
        }
        bits = (uint32_t)uniform->data[offset] |
            ((uint32_t)uniform->data[offset + 1] << 8) |
            ((uint32_t)uniform->data[offset + 2] << 16) |
            ((uint32_t)uniform->data[offset + 3] << 24);
        memcpy(&value, &bits, sizeof(value));
        if (!isfinite(value)) {
            return false;
        }
        *out = value;
        return true;
    }
    return false;
}

int try_rms_norm(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out) {
    const TsirSemanticFunction *func = NULL;
    const TsirReductionRegion *reduction = NULL;
    uint32_t ii = 0;
    uint32_t si = 0;
    uint32_t oi = 0;
    uint32_t hid_axis = 0;
    uint32_t red_axis = 0;
    const TsirBufferBinding *ib = NULL;
    const TsirBufferBinding *sb = NULL;
    const TsirBufferBinding *ob = NULL;
    float epsilon = 0.0f;
    uint64_t hidden64 = 0;
    size_t hidden = 0;
    const TsirInputBuffer *input_bytes = NULL;
    const TsirInputBuffer *scale_bytes = NULL;
    uint64_t expected_in = 0;
    uint64_t expected_scale = 0;
    uint64_t expected_out = 0;
    TsirStatus status = TSIR_OK;
    size_t d = 0;
    *err_out = TSIR_OK;
    if (semantic->function_count != 1) {
        return TRY_NOMATCH;
    }
    func = &semantic->functions[0];
    if (func->collective_count != 0 || func->binding_count < 3 ||
        func->reduction_count != 1 || func->axis_count != 2) {
        return TRY_NOMATCH;
    }
    if (func->body.op != TSIR_BODY_RMS_NORM || !func->body.has_rms_norm) {
        return TRY_NOMATCH;
    }
    if (func->body.rms_norm.formula !=
        TSIR_RMSNORM_SUM_SQUARES_MEAN_EPSILON_RSQRT_SCALE) {
        return TRY_NOMATCH;
    }
    if (func->body.rms_norm.reduction_target !=
        TSIR_RMSNORM_INTERMEDIATE_SCALAR) {
        return TRY_NOMATCH;
    }
    if (input_count != tsir_count_read_only_bindings(func)) {
        return TRY_NOMATCH;
    }
    if (!resolve_rms_norm_epsilon(func, inputs, input_count,
        &func->body.rms_norm.epsilon, &epsilon)) {
        return TRY_NOMATCH;
    }
    if (func->body.binding_role_count != 3 ||
        func->body.axis_role_count != 2) {
        return TRY_NOMATCH;
    }
    if (!find_body_role(&func->body, TSIR_ROLE_INPUT, &ii) ||
        !find_body_role(&func->body, TSIR_ROLE_SCALE, &si) ||
        !find_body_role(&func->body, TSIR_ROLE_OUTPUT, &oi)) {
        return TRY_NOMATCH;
    }
    if (ii >= func->binding_count || si >= func->binding_count ||
        oi >= func->binding_count || ii == si || si == oi || ii == oi) {
        return TRY_NOMATCH;
    }
    if (!find_axis_role(&func->body, TSIR_AXIS_HIDDEN, &hid_axis) ||
        !find_axis_role(&func->body, TSIR_AXIS_REDUCTION, &red_axis)) {
        return TRY_NOMATCH;
    }
    if (hid_axis == red_axis || hid_axis >= func->axis_count ||
        red_axis >= func->axis_count) {
        return TRY_NOMATCH;
    }
    if (func->body.rms_norm.hidden_extent_axis != hid_axis) {
        return TRY_NOMATCH;
    }
    if (hid_axis != 0 || red_axis != 1) {
        return TRY_NOMATCH;
    }
    reduction = &func->reductions[0];
    if (reduction->op != TSIR_REDUCTION_SUM ||
        reduction->axis != red_axis ||
        reduction->contract.accumulation != TSIR_SCALAR_F32 ||
        reduction->contract.associativity != TSIR_ASSOC_STRICT_ORDERED ||
        reduction->contract.nan_inf != TSIR_NANINF_PROPAGATE) {
        return TRY_NOMATCH;
    }
    ib = &func->bindings[ii];
    sb = &func->bindings[si];
    ob = &func->bindings[oi];
    if (ib->read_write || sb->read_write || !ob->read_write) {
        return TRY_NOMATCH;
    }
    if (ib->elem != sb->elem || sb->elem != ob->elem) {
        return TRY_NOMATCH;
    }
    if (ib->elem != TSIR_SCALAR_F32 && ib->elem != TSIR_SCALAR_F16 &&
        ib->elem != TSIR_SCALAR_BF16) {
        return TRY_NOMATCH;
    }
    if (ib->logical_shape_len != 1 || sb->logical_shape_len != 1 ||
        ob->logical_shape_len != 1) {
        return TRY_NOMATCH;
    }
    hidden64 = ib->logical_shape[0];
    if (sb->logical_shape[0] != hidden64 ||
        ob->logical_shape[0] != hidden64) {
        return TRY_NOMATCH;
    }
    if (hidden64 > SIZE_MAX) {
        return TRY_NOMATCH;
    }
    hidden = (size_t)hidden64;
    {
        size_t slot_in = 0;
        size_t slot_scale = 0;
        size_t i = 0;
        for (i = 0; i < ii; i++) {
            if (!func->bindings[i].read_write) {
                slot_in += 1;
            }
        }
        for (i = 0; i < si; i++) {
            if (!func->bindings[i].read_write) {
                slot_scale += 1;
            }
        }
        input_bytes = &inputs[slot_in];
        scale_bytes = &inputs[slot_scale];
    }
    status = tsir_expected_bytes(ib, &expected_in);
    if (status != TSIR_OK) {
        return TRY_NOMATCH;
    }
    status = tsir_expected_bytes(sb, &expected_scale);
    if (status != TSIR_OK) {
        return TRY_NOMATCH;
    }
    if (input_bytes->len != expected_in ||
        scale_bytes->len != expected_scale) {
        return TRY_NOMATCH;
    }
    status = tsir_expected_bytes(ob, &expected_out);
    if (status != TSIR_OK) {
        return TRY_NOMATCH;
    }
    {
        uint8_t *output = (uint8_t *)malloc(expected_out == 0 ? 1 : expected_out);
        if (output == NULL) {
            *err_out = TSIR_ERR_NOMEM;
            return -1;
        }
        if (hidden != 0) {
            float sum_sq = 0.0f;
            float mean_sq = 0.0f;
            float inv_rms = 0.0f;
            size_t r = 0;
            for (r = 0; r < hidden; r++) {
                float x = 0.0f;
                status = tsir_read_f32(input_bytes->data, input_bytes->len,
                    ib->elem, r, &x);
                if (status != TSIR_OK) {
                    free(output);
                    *err_out = status;
                    return -1;
                }
                sum_sq += x * x;
            }
            mean_sq = sum_sq / (float)hidden;
            inv_rms = 1.0f / sqrtf(mean_sq + epsilon);
            for (d = 0; d < hidden; d++) {
                float x = 0.0f;
                float scale = 0.0f;
                status = tsir_read_f32(input_bytes->data, input_bytes->len,
                    ib->elem, d, &x);
                if (status != TSIR_OK) {
                    free(output);
                    *err_out = status;
                    return -1;
                }
                status = tsir_read_f32(scale_bytes->data, scale_bytes->len,
                    sb->elem, d, &scale);
                if (status != TSIR_OK) {
                    free(output);
                    *err_out = status;
                    return -1;
                }
                status = tsir_write_f32(output, expected_out, ob->elem, d,
                    x * inv_rms * scale);
                if (status != TSIR_OK) {
                    free(output);
                    *err_out = status;
                    return -1;
                }
            }
        }
        status = finish_result(result, output, expected_out);
        if (status != TSIR_OK) {
            *err_out = status;
            return -1;
        }
        return TRY_MATCH;
    }
}

/* ---- gather ---- */

int try_gather(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out) {
    const TsirSemanticFunction *func = NULL;
    uint32_t ii = 0;
    uint32_t ti = 0;
    uint32_t oi = 0;
    uint32_t tok_axis = 0;
    uint32_t hid_axis = 0;
    const TsirBufferBinding *ib = NULL;
    const TsirBufferBinding *tb = NULL;
    const TsirBufferBinding *ob = NULL;
    uint64_t tokens64 = 0;
    uint64_t vocab64 = 0;
    uint64_t hidden64 = 0;
    size_t tokens = 0;
    size_t vocab = 0;
    size_t hidden = 0;
    const TsirInputBuffer *indices_bytes = NULL;
    const TsirInputBuffer *table_bytes = NULL;
    uint64_t expected_indices = 0;
    uint64_t expected_table = 0;
    uint64_t expected_out = 0;
    TsirStatus status = TSIR_OK;
    size_t t = 0;
    *err_out = TSIR_OK;
    if (semantic->function_count != 1) {
        return TRY_NOMATCH;
    }
    func = &semantic->functions[0];
    if (func->collective_count != 0 || func->reduction_count != 0 ||
        func->binding_count != 3 || func->axis_count != 2) {
        return TRY_NOMATCH;
    }
    if (func->body.op != TSIR_BODY_GATHER ||
        func->body.binding_role_count != 3 ||
        func->body.axis_role_count != 2) {
        return TRY_NOMATCH;
    }
    if (!find_body_role(&func->body, TSIR_ROLE_INDICES, &ii) ||
        !find_body_role(&func->body, TSIR_ROLE_TABLE, &ti) ||
        !find_body_role(&func->body, TSIR_ROLE_OUTPUT, &oi)) {
        return TRY_NOMATCH;
    }
    if (ii >= func->binding_count || ti >= func->binding_count ||
        oi >= func->binding_count || ii == ti || ti == oi || ii == oi) {
        return TRY_NOMATCH;
    }
    if (!find_axis_role(&func->body, TSIR_AXIS_TOKEN, &tok_axis) ||
        !find_axis_role(&func->body, TSIR_AXIS_HIDDEN, &hid_axis)) {
        return TRY_NOMATCH;
    }
    if (tok_axis == hid_axis || tok_axis >= func->axis_count ||
        hid_axis >= func->axis_count) {
        return TRY_NOMATCH;
    }
    if (tok_axis != 0 || hid_axis != 1) {
        return TRY_NOMATCH;
    }
    ib = &func->bindings[ii];
    tb = &func->bindings[ti];
    ob = &func->bindings[oi];
    if (ib->read_write || tb->read_write || !ob->read_write) {
        return TRY_NOMATCH;
    }
    if (ib->elem != TSIR_SCALAR_U32) {
        return TRY_NOMATCH;
    }
    if (tb->elem != ob->elem) {
        return TRY_NOMATCH;
    }
    if (tb->elem != TSIR_SCALAR_F32 && tb->elem != TSIR_SCALAR_F16 &&
        tb->elem != TSIR_SCALAR_BF16) {
        return TRY_NOMATCH;
    }
    if (ib->logical_shape_len != 1 || tb->logical_shape_len != 2 ||
        ob->logical_shape_len != 2) {
        return TRY_NOMATCH;
    }
    tokens64 = ib->logical_shape[0];
    vocab64 = tb->logical_shape[0];
    hidden64 = tb->logical_shape[1];
    if (ob->logical_shape[0] != tokens64 ||
        ob->logical_shape[1] != hidden64) {
        return TRY_NOMATCH;
    }
    if (tokens64 > SIZE_MAX || vocab64 > SIZE_MAX || hidden64 > SIZE_MAX) {
        return TRY_NOMATCH;
    }
    tokens = (size_t)tokens64;
    vocab = (size_t)vocab64;
    hidden = (size_t)hidden64;
    if (input_count != 2) {
        return TRY_NOMATCH;
    }
    {
        bool indices_first = ii < ti;
        indices_bytes = indices_first ? &inputs[0] : &inputs[1];
        table_bytes = indices_first ? &inputs[1] : &inputs[0];
    }
    status = tsir_expected_bytes(ib, &expected_indices);
    if (status != TSIR_OK) {
        return TRY_NOMATCH;
    }
    status = tsir_expected_bytes(tb, &expected_table);
    if (status != TSIR_OK) {
        return TRY_NOMATCH;
    }
    if (indices_bytes->len != expected_indices ||
        table_bytes->len != expected_table) {
        return TRY_NOMATCH;
    }
    for (t = 0; t < tokens; t++) {
        uint32_t row = 0;
        size_t off = t * 4;
        if (off + 4 > indices_bytes->len) {
            return TRY_NOMATCH;
        }
        row = (uint32_t)indices_bytes->data[off] |
            ((uint32_t)indices_bytes->data[off + 1] << 8) |
            ((uint32_t)indices_bytes->data[off + 2] << 16) |
            ((uint32_t)indices_bytes->data[off + 3] << 24);
        if (row >= vocab) {
            /* Zig falls through to NotImplemented here; C reports
             * the same fall-through as unsupported at run level. */
            return TRY_NOMATCH;
        }
    }
    status = tsir_expected_bytes(ob, &expected_out);
    if (status != TSIR_OK) {
        return TRY_NOMATCH;
    }
    {
        uint8_t *output = (uint8_t *)malloc(expected_out == 0 ? 1 : expected_out);
        size_t elem_bytes = tsir_scalar_byte_size(ob->elem);
        if (output == NULL) {
            *err_out = TSIR_ERR_NOMEM;
            return -1;
        }
        for (t = 0; t < tokens; t++) {
            uint32_t row = 0;
            size_t off = t * 4;
            size_t h = 0;
            row = (uint32_t)indices_bytes->data[off] |
                ((uint32_t)indices_bytes->data[off + 1] << 8) |
                ((uint32_t)indices_bytes->data[off + 2] << 16) |
                ((uint32_t)indices_bytes->data[off + 3] << 24);
            for (h = 0; h < hidden; h++) {
                size_t src = (row * hidden + h) * elem_bytes;
                size_t dst = (t * hidden + h) * elem_bytes;
                memcpy(output + dst, table_bytes->data + src, elem_bytes);
            }
        }
        status = finish_result(result, output, expected_out);
        if (status != TSIR_OK) {
            *err_out = status;
            return -1;
        }
        return TRY_MATCH;
    }
}

