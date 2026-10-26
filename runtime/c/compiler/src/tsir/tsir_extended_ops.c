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

int tsir_try_attention(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out) {
    const TsirSemanticFunction *func = NULL;
    const TsirAttentionScoresBody *attn = NULL;
    uint32_t qi = 0;
    uint32_t ki = 0;
    uint32_t vi = 0;
    uint32_t oi = 0;
    bool have_q = false;
    bool have_k = false;
    bool have_v = false;
    bool have_o = false;
    size_t i = 0;
    const TsirBufferBinding *qb = NULL;
    const TsirBufferBinding *kb = NULL;
    const TsirBufferBinding *vb = NULL;
    const TsirBufferBinding *ob = NULL;
    const uint8_t *q_bytes = NULL;
    const uint8_t *k_bytes = NULL;
    const uint8_t *v_bytes = NULL;
    size_t q_len = 0;
    size_t k_len = 0;
    size_t v_len = 0;
    uint64_t k_total = 1;
    uint64_t v_total = 1;
    size_t head_dim = 0;
    size_t kv_len = 0;
    size_t sliding_start = 0;
    bool has_sliding = false;
    float scale = 0.0f;
    float *scores = NULL;
    float max_score = 0.0f;
    float sum_exp = 0.0f;
    uint8_t *output = NULL;
    size_t d = 0;
    size_t k = 0;
    (void)err_out;
    if (!tsir_ext_one_function(semantic, &func)) {
        return TRY_NOMATCH;
    }
    if (func->collective_count != 0) {
        return TRY_NOMATCH;
    }
    if (func->body.op != TSIR_BODY_ATTENTION_SCORES) {
        return TRY_NOMATCH;
    }
    if (!func->body.has_attention_scores) {
        return TRY_NOMATCH;
    }
    attn = &func->body.attention_scores;
    if (attn->softmax_mode != TSIR_SOFTMAX_TWO_PASS_STABLE) {
        return TRY_NOMATCH;
    }
    if (attn->has_softcap) {
        return TRY_NOMATCH;
    }
    if (attn->scale_source != TSIR_SCALE_LITERAL_F32) {
        return TRY_NOMATCH;
    }
    if (!attn->has_scale_literal_f64) {
        return TRY_NOMATCH;
    }
    scale = (float)attn->scale_literal_f64;
    if (attn->causal_mode == TSIR_CAUSAL_SLIDING_WINDOW) {
        if (!attn->sliding_window_size.has ||
            attn->sliding_window_size.value == 0) {
            return TRY_NOMATCH;
        }
        has_sliding = true;
    } else if (attn->causal_mode != TSIR_CAUSAL_NONE &&
        attn->causal_mode != TSIR_CAUSAL_CAUSAL) {
        return TRY_NOMATCH;
    }
    if (attn->query_seq_len.has &&
        attn->query_seq_len.value > 1) {
        return TRY_NOMATCH;
    }
    if (input_count != tsir_count_read_only_bindings(func)) {
        return TRY_NOMATCH;
    }
    for (i = 0; i < func->body.binding_role_count; i++) {
        TsirSemanticBindingRole role =
            func->body.binding_roles[i].role;
        uint32_t index = func->body.binding_roles[i].binding_index;
        switch (role) {
            case TSIR_ROLE_QUERY:
                qi = index;
                have_q = true;
                break;
            case TSIR_ROLE_KEY:
                ki = index;
                have_k = true;
                break;
            case TSIR_ROLE_VALUE:
                vi = index;
                have_v = true;
                break;
            case TSIR_ROLE_OUTPUT:
                oi = index;
                have_o = true;
                break;
            case TSIR_ROLE_KV_LEN_BUFFER:
            case TSIR_ROLE_PAGE_TABLE:
                break;
            case TSIR_ROLE_MATRIX:
            case TSIR_ROLE_VECTOR:
            case TSIR_ROLE_INPUT:
            case TSIR_ROLE_SCALE:
            case TSIR_ROLE_INDICES:
            case TSIR_ROLE_TABLE:
            case TSIR_ROLE_SUMMAND_A:
            case TSIR_ROLE_SUMMAND_B:
            case TSIR_ROLE_GATE:
            case TSIR_ROLE_KEY_PROJECTION:
            case TSIR_ROLE_VALUE_PROJECTION:
            case TSIR_ROLE_KEY_CACHE:
            case TSIR_ROLE_VALUE_CACHE:
            case TSIR_ROLE_DECODE_POSITION:
            case TSIR_ROLE_KEY_OUTPUT:
            case TSIR_ROLE_VALUE_OUTPUT:
            case TSIR_ROLE_WEIGHT:
            case TSIR_ROLE_BIAS:
            case TSIR_ROLE_LINEAR_STATE:
            default:
                return TRY_NOMATCH;
        }
    }
    if (!have_q || !have_k || !have_v || !have_o) {
        return TRY_NOMATCH;
    }
    if (qi >= func->binding_count || ki >= func->binding_count ||
        vi >= func->binding_count || oi >= func->binding_count) {
        return TRY_NOMATCH;
    }
    qb = &func->bindings[qi];
    kb = &func->bindings[ki];
    vb = &func->bindings[vi];
    ob = &func->bindings[oi];
    if (qb->read_write || kb->read_write || vb->read_write ||
        !ob->read_write) {
        return TRY_NOMATCH;
    }
    if (qb->elem != TSIR_SCALAR_F32 ||
        kb->elem != TSIR_SCALAR_F32 ||
        vb->elem != TSIR_SCALAR_F32 ||
        ob->elem != TSIR_SCALAR_F32) {
        return TRY_NOMATCH;
    }
    if (attn->head_dim == 0) {
        return TRY_NOMATCH;
    }
    head_dim = attn->head_dim;
    if (qb->logical_shape_len != 1 ||
        qb->logical_shape[0] != attn->head_dim) {
        return TRY_NOMATCH;
    }
    if (ob->logical_shape_len != 1 ||
        ob->logical_shape[0] != attn->head_dim) {
        return TRY_NOMATCH;
    }
    if (kb->logical_shape_len < 1) {
        return TRY_NOMATCH;
    }
    for (i = 0; i < kb->logical_shape_len; i++) {
        if (kb->logical_shape[i] != 0 &&
            k_total > UINT64_MAX / kb->logical_shape[i]) {
            return TRY_NOMATCH;
        }
        k_total *= kb->logical_shape[i];
    }
    if (k_total == 0 || k_total % attn->head_dim != 0) {
        return TRY_NOMATCH;
    }
    kv_len = (size_t)(k_total / attn->head_dim);
    if (kv_len == 0) {
        return TRY_NOMATCH;
    }
    for (i = 0; i < vb->logical_shape_len; i++) {
        if (vb->logical_shape[i] != 0 &&
            v_total > UINT64_MAX / vb->logical_shape[i]) {
            return TRY_NOMATCH;
        }
        v_total *= vb->logical_shape[i];
    }
    if (v_total != k_total) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_get_input(func, inputs, input_count, qi, &q_bytes,
        &q_len) ||
        !tsir_ext_get_input(func, inputs, input_count, ki, &k_bytes,
            &k_len) ||
        !tsir_ext_get_input(func, inputs, input_count, vi, &v_bytes,
            &v_len)) {
        return TRY_NOMATCH;
    }
    if (q_len != head_dim * 4 || k_len != kv_len * head_dim * 4 ||
        v_len != kv_len * head_dim * 4) {
        return TRY_NOMATCH;
    }
    if (has_sliding) {
        size_t window = attn->sliding_window_size.value;
        sliding_start = (kv_len > window) ? kv_len - window : 0;
    }
    scores = (float *)malloc(kv_len * sizeof(float));
    output = (uint8_t *)malloc(head_dim * 4);
    if (scores == NULL || output == NULL) {
        free(scores);
        free(output);
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    max_score = -INFINITY;
    for (k = 0; k < kv_len; k++) {
        float dot = 0.0f;
        float sc = 0.0f;
        for (d = 0; d < head_dim; d++) {
            float qv = 0.0f;
            float kv = 0.0f;
            if (!tsir_ext_read_f32(q_bytes, q_len, d, &qv) ||
                !tsir_ext_read_f32(k_bytes, k_len, k * head_dim + d, &kv)) {
                free(scores);
                free(output);
                return TRY_NOMATCH;
            }
            dot += qv * kv;
        }
        sc = dot * scale;
        if (attn->causal_mode == TSIR_CAUSAL_CAUSAL) {
            if (k > kv_len - 1) {
                sc = -1.0e30f;
            }
        } else if (has_sliding) {
            if (k < sliding_start) {
                sc = -1.0e30f;
            }
        }
        scores[k] = sc;
        if (sc > max_score) {
            max_score = sc;
        }
    }
    sum_exp = 0.0f;
    for (k = 0; k < kv_len; k++) {
        float e = tsir_exp_f32(scores[k] - max_score);
        scores[k] = e;
        sum_exp += e;
    }
    if (sum_exp == 0.0f) {
        free(scores);
        free(output);
        return TRY_NOMATCH;
    }
    for (d = 0; d < head_dim; d++) {
        float acc = 0.0f;
        for (k = 0; k < kv_len; k++) {
            float vv = 0.0f;
            if (!tsir_ext_read_f32(v_bytes, v_len, k * head_dim + d, &vv)) {
                free(scores);
                free(output);
                return TRY_NOMATCH;
            }
            acc += vv * (scores[k] / sum_exp);
        }
        if (!tsir_ext_write_f32(output, head_dim * 4, d, acc)) {
            free(scores);
            free(output);
            return TRY_NOMATCH;
        }
    }
    free(scores);
    if (finish_result(result, output, head_dim * 4) != TSIR_OK) {
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    return TRY_MATCH;
}

int tsir_try_l2(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out) {
    const TsirSemanticFunction *func = NULL;
    const TsirL2NormalizeBody *body = NULL;
    uint32_t ii = 0;
    uint32_t oi = 0;
    const TsirBufferBinding *ib = NULL;
    const TsirBufferBinding *ob = NULL;
    const uint8_t *in_bytes = NULL;
    size_t in_len = 0;
    uint8_t *output = NULL;
    size_t hidden = 0;
    size_t d = 0;
    float sq = 0.0f;
    float inv = 0.0f;
    (void)err_out;
    if (!tsir_ext_one_function(semantic, &func)) {
        return TRY_NOMATCH;
    }
    if (func->collective_count != 0 || func->reduction_count != 0) {
        return TRY_NOMATCH;
    }
    if (func->body.op != TSIR_BODY_L2_NORMALIZE) {
        return TRY_NOMATCH;
    }
    if (!func->body.has_l2_normalize) {
        return TRY_NOMATCH;
    }
    body = &func->body.l2_normalize;
    if (body->hidden == 0) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_find_role_once(&func->body, TSIR_ROLE_INPUT, &ii) ||
        !tsir_ext_find_role_once(&func->body, TSIR_ROLE_OUTPUT, &oi)) {
        return TRY_NOMATCH;
    }
    if (func->body.binding_role_count != 2) {
        return TRY_NOMATCH;
    }
    if (ii >= func->binding_count || oi >= func->binding_count) {
        return TRY_NOMATCH;
    }
    ib = &func->bindings[ii];
    ob = &func->bindings[oi];
    if (ib->read_write || !ob->read_write) {
        return TRY_NOMATCH;
    }
    if (ib->elem != TSIR_SCALAR_F32 ||
        ob->elem != TSIR_SCALAR_F32) {
        return TRY_NOMATCH;
    }
    hidden = body->hidden;
    if (input_count != tsir_count_read_only_bindings(func)) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_get_input(func, inputs, input_count, ii, &in_bytes,
        &in_len)) {
        return TRY_NOMATCH;
    }
    if (in_len != hidden * 4) {
        return TRY_NOMATCH;
    }
    output = (uint8_t *)malloc(hidden * 4);
    if (output == NULL) {
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    for (d = 0; d < hidden; d++) {
        float v = 0.0f;
        if (!tsir_ext_read_f32(in_bytes, in_len, d, &v)) {
            free(output);
            return TRY_NOMATCH;
        }
        sq += v * v;
    }
    inv = 1.0f / tsir_sqrt_f32(sq + body->eps);
    for (d = 0; d < hidden; d++) {
        float v = 0.0f;
        if (!tsir_ext_read_f32(in_bytes, in_len, d, &v) ||
            !tsir_ext_write_f32(output, hidden * 4, d, v * inv)) {
            free(output);
            return TRY_NOMATCH;
        }
    }
    if (finish_result(result, output, hidden * 4) != TSIR_OK) {
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    return TRY_MATCH;
}

int tsir_try_conv(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out) {
    const TsirSemanticFunction *func = NULL;
    const TsirConv1DDepthwiseBody *body = NULL;
    uint32_t ii = 0;
    uint32_t wi = 0;
    uint32_t bi = 0;
    uint32_t oi = 0;
    bool have_i = false;
    bool have_w = false;
    bool have_b = false;
    bool have_o = false;
    size_t i = 0;
    const TsirBufferBinding *ib = NULL;
    const TsirBufferBinding *wb = NULL;
    const TsirBufferBinding *ob = NULL;
    const uint8_t *in_bytes = NULL;
    const uint8_t *weight_bytes = NULL;
    const uint8_t *bias_bytes = NULL;
    size_t in_len = 0;
    size_t weight_len = 0;
    size_t bias_len = 0;
    size_t channels = 0;
    size_t kernel_size = 0;
    size_t tokens = 0;
    uint8_t *output = NULL;
    size_t t = 0;
    size_t c = 0;
    size_t k = 0;
    (void)err_out;
    if (!tsir_ext_one_function(semantic, &func)) {
        return TRY_NOMATCH;
    }
    if (func->collective_count != 0 || func->reduction_count != 0) {
        return TRY_NOMATCH;
    }
    if (func->body.op != TSIR_BODY_CONV1D_DEPTHWISE) {
        return TRY_NOMATCH;
    }
    if (!func->body.has_conv1d_depthwise) {
        return TRY_NOMATCH;
    }
    body = &func->body.conv1d_depthwise;
    if (body->channels == 0 || body->kernel_size == 0) {
        return TRY_NOMATCH;
    }
    for (i = 0; i < func->body.binding_role_count; i++) {
        TsirSemanticBindingRole role =
            func->body.binding_roles[i].role;
        uint32_t index = func->body.binding_roles[i].binding_index;
        switch (role) {
            case TSIR_ROLE_INPUT:
                ii = index;
                have_i = true;
                break;
            case TSIR_ROLE_WEIGHT:
                wi = index;
                have_w = true;
                break;
            case TSIR_ROLE_BIAS:
                bi = index;
                have_b = true;
                break;
            case TSIR_ROLE_OUTPUT:
                oi = index;
                have_o = true;
                break;
            case TSIR_ROLE_MATRIX:
            case TSIR_ROLE_VECTOR:
            case TSIR_ROLE_SCALE:
            case TSIR_ROLE_INDICES:
            case TSIR_ROLE_TABLE:
            case TSIR_ROLE_QUERY:
            case TSIR_ROLE_KEY:
            case TSIR_ROLE_VALUE:
            case TSIR_ROLE_KV_LEN_BUFFER:
            case TSIR_ROLE_PAGE_TABLE:
            case TSIR_ROLE_SUMMAND_A:
            case TSIR_ROLE_SUMMAND_B:
            case TSIR_ROLE_GATE:
            case TSIR_ROLE_KEY_PROJECTION:
            case TSIR_ROLE_VALUE_PROJECTION:
            case TSIR_ROLE_KEY_CACHE:
            case TSIR_ROLE_VALUE_CACHE:
            case TSIR_ROLE_DECODE_POSITION:
            case TSIR_ROLE_KEY_OUTPUT:
            case TSIR_ROLE_VALUE_OUTPUT:
            case TSIR_ROLE_LINEAR_STATE:
            default:
                return TRY_NOMATCH;
        }
    }
    if (!have_i || !have_w || !have_o) {
        return TRY_NOMATCH;
    }
    if (body->has_bias && !have_b) {
        return TRY_NOMATCH;
    }
    if (!body->has_bias && have_b) {
        return TRY_NOMATCH;
    }
    if (ii >= func->binding_count || wi >= func->binding_count ||
        oi >= func->binding_count ||
        (have_b && bi >= func->binding_count)) {
        return TRY_NOMATCH;
    }
    ib = &func->bindings[ii];
    wb = &func->bindings[wi];
    ob = &func->bindings[oi];
    if (ib->read_write || wb->read_write || !ob->read_write) {
        return TRY_NOMATCH;
    }
    if (ib->elem != TSIR_SCALAR_F32 ||
        wb->elem != TSIR_SCALAR_F32 ||
        ob->elem != TSIR_SCALAR_F32) {
        return TRY_NOMATCH;
    }
    if (input_count != tsir_count_read_only_bindings(func)) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_get_input(func, inputs, input_count, ii, &in_bytes,
        &in_len) ||
        !tsir_ext_get_input(func, inputs, input_count, wi, &weight_bytes,
            &weight_len)) {
        return TRY_NOMATCH;
    }
    if (have_b) {
        if (!tsir_ext_get_input(func, inputs, input_count, bi, &bias_bytes,
            &bias_len)) {
            return TRY_NOMATCH;
        }
    }
    channels = body->channels;
    kernel_size = body->kernel_size;
    if (in_len % (channels * 4) != 0) {
        return TRY_NOMATCH;
    }
    tokens = in_len / (channels * 4);
    if (tokens == 0) {
        return TRY_NOMATCH;
    }
    if (weight_len != channels * kernel_size * 4) {
        return TRY_NOMATCH;
    }
    if (have_b && bias_len != channels * 4) {
        return TRY_NOMATCH;
    }
    output = (uint8_t *)malloc(tokens * channels * 4);
    if (output == NULL) {
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    for (t = 0; t < tokens; t++) {
        for (c = 0; c < channels; c++) {
            float acc = 0.0f;
            if (have_b) {
                if (!tsir_ext_read_f32(bias_bytes, bias_len, c, &acc)) {
                    free(output);
                    return TRY_NOMATCH;
                }
            }
            for (k = 0; k < kernel_size; k++) {
                int64_t t_in = (int64_t)t -
                    (int64_t)(kernel_size - 1 - k);
                float v = 0.0f;
                float w = 0.0f;
                if (t_in < 0) {
                    continue;
                }
                if (!tsir_ext_read_f32(in_bytes, in_len,
                    (size_t)t_in * channels + c, &v) ||
                    !tsir_ext_read_f32(weight_bytes, weight_len,
                        c * kernel_size + k, &w)) {
                    free(output);
                    return TRY_NOMATCH;
                }
                acc += v * w;
            }
            if (!tsir_ext_write_f32(output, tokens * channels * 4,
                t * channels + c, acc)) {
                free(output);
                return TRY_NOMATCH;
            }
        }
    }
    if (finish_result(result, output, tokens * channels * 4) !=
        TSIR_OK) {
        *err_out = TSIR_ERR_NOMEM;
        return -1;
    }
    return TRY_MATCH;
}

TsirStatus tsir_linear_attention_step(const float *query,
    const float *key, const float *value, const float *gate,
    const float *state_in, size_t value_dim, size_t key_dim,
    float *state_out, float *out) {
    float a_log = 0.5f;
    float alpha = 0.0f;
    float decay = 0.0f;
    size_t d = 0;
    size_t k = 0;
    if (query == NULL || key == NULL || value == NULL ||
        gate == NULL || state_in == NULL || state_out == NULL ||
        out == NULL || value_dim == 0 || key_dim == 0) {
        return TSIR_ERR_INVALID;
    }
    alpha = 1.0f - tsir_exp_f32(-a_log);
    decay = 1.0f - alpha;
    memcpy(state_out, state_in,
        value_dim * key_dim * sizeof(float));
    for (d = 0; d < value_dim; d++) {
        float prev_dot = 0.0f;
        float correction = 0.0f;
        float scaled = 0.0f;
        for (k = 0; k < key_dim; k++) {
            prev_dot += state_out[d * key_dim + k] * key[k];
        }
        correction = query[d] - prev_dot;
        scaled = alpha * correction;
        for (k = 0; k < key_dim; k++) {
            float prev_state = state_out[d * key_dim + k];
            float delta = scaled * key[k];
            state_out[d * key_dim + k] =
                decay * prev_state + delta;
        }
    }
    for (d = 0; d < value_dim; d++) {
        float acc = 0.0f;
        float sigmoid_g = 0.0f;
        for (k = 0; k < key_dim; k++) {
            acc += state_out[d * key_dim + k] * value[k];
        }
        sigmoid_g = 1.0f / (1.0f + tsir_exp_f32(-gate[d]));
        out[d] = acc + sigmoid_g * query[d];
    }
    return TSIR_OK;
}

int tsir_try_linear_attention(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result, TsirStatus *err_out) {
    const TsirSemanticFunction *func = NULL;
    const TsirLinearAttentionBody *body = NULL;
    uint32_t qi = 0;
    uint32_t ki = 0;
    uint32_t vi = 0;
    uint32_t gi = 0;
    uint32_t si = 0;
    uint32_t oi = 0;
    bool have_q = false;
    bool have_k = false;
    bool have_v = false;
    bool have_g = false;
    bool have_s = false;
    bool have_o = false;
    size_t i = 0;
    const TsirBufferBinding *qb = NULL;
    const TsirBufferBinding *kb = NULL;
    const TsirBufferBinding *vb = NULL;
    const TsirBufferBinding *gb = NULL;
    const TsirBufferBinding *sb = NULL;
    const TsirBufferBinding *ob = NULL;
    const uint8_t *q_bytes = NULL;
    const uint8_t *k_bytes = NULL;
    const uint8_t *v_bytes = NULL;
    const uint8_t *g_bytes = NULL;
    const uint8_t *s_bytes = NULL;
    size_t q_len = 0;
    size_t k_len = 0;
    size_t v_len = 0;
    size_t g_len = 0;
    size_t s_len = 0;
    size_t value_dim = 0;
    size_t key_dim = 0;
    (void)result;
    (void)err_out;
    if (!tsir_ext_one_function(semantic, &func)) {
        return TRY_NOMATCH;
    }
    if (func->collective_count != 0 || func->reduction_count != 0) {
        return TRY_NOMATCH;
    }
    if (func->body.op != TSIR_BODY_LINEAR_ATTENTION) {
        return TRY_NOMATCH;
    }
    if (!func->body.has_linear_attention) {
        return TRY_NOMATCH;
    }
    body = &func->body.linear_attention;
    if (body->norm_mode != TSIR_LINEAR_NORM_SHARED ||
        body->has_dt_bias) {
        return TRY_NOMATCH;
    }
    if (body->key_dim == 0 || body->value_dim == 0) {
        return TRY_NOMATCH;
    }
    for (i = 0; i < func->body.binding_role_count; i++) {
        TsirSemanticBindingRole role =
            func->body.binding_roles[i].role;
        uint32_t index = func->body.binding_roles[i].binding_index;
        switch (role) {
            case TSIR_ROLE_QUERY:
                qi = index;
                have_q = true;
                break;
            case TSIR_ROLE_KEY:
                ki = index;
                have_k = true;
                break;
            case TSIR_ROLE_VALUE:
                vi = index;
                have_v = true;
                break;
            case TSIR_ROLE_GATE:
                gi = index;
                have_g = true;
                break;
            case TSIR_ROLE_LINEAR_STATE:
                si = index;
                have_s = true;
                break;
            case TSIR_ROLE_OUTPUT:
                oi = index;
                have_o = true;
                break;
            case TSIR_ROLE_MATRIX:
            case TSIR_ROLE_VECTOR:
            case TSIR_ROLE_INPUT:
            case TSIR_ROLE_SCALE:
            case TSIR_ROLE_INDICES:
            case TSIR_ROLE_TABLE:
            case TSIR_ROLE_KV_LEN_BUFFER:
            case TSIR_ROLE_PAGE_TABLE:
            case TSIR_ROLE_SUMMAND_A:
            case TSIR_ROLE_SUMMAND_B:
            case TSIR_ROLE_KEY_PROJECTION:
            case TSIR_ROLE_VALUE_PROJECTION:
            case TSIR_ROLE_KEY_CACHE:
            case TSIR_ROLE_VALUE_CACHE:
            case TSIR_ROLE_DECODE_POSITION:
            case TSIR_ROLE_KEY_OUTPUT:
            case TSIR_ROLE_VALUE_OUTPUT:
            case TSIR_ROLE_WEIGHT:
            case TSIR_ROLE_BIAS:
            default:
                return TRY_NOMATCH;
        }
    }
    if (!have_q || !have_k || !have_v || !have_g || !have_s ||
        !have_o) {
        return TRY_NOMATCH;
    }
    if (qi >= func->binding_count || ki >= func->binding_count ||
        vi >= func->binding_count || gi >= func->binding_count ||
        si >= func->binding_count || oi >= func->binding_count) {
        return TRY_NOMATCH;
    }
    qb = &func->bindings[qi];
    kb = &func->bindings[ki];
    vb = &func->bindings[vi];
    gb = &func->bindings[gi];
    sb = &func->bindings[si];
    ob = &func->bindings[oi];
    if (qb->read_write || kb->read_write || vb->read_write ||
        gb->read_write) {
        return TRY_NOMATCH;
    }
    if (!sb->read_write || !ob->read_write) {
        return TRY_NOMATCH;
    }
    if (qb->elem != TSIR_SCALAR_F32 ||
        kb->elem != TSIR_SCALAR_F32 ||
        vb->elem != TSIR_SCALAR_F32 ||
        gb->elem != TSIR_SCALAR_F32 ||
        sb->elem != TSIR_SCALAR_F32 ||
        ob->elem != TSIR_SCALAR_F32) {
        return TRY_NOMATCH;
    }
    value_dim = body->value_dim;
    key_dim = body->key_dim;
    if (input_count != tsir_count_read_only_bindings(func)) {
        return TRY_NOMATCH;
    }
    if (!tsir_ext_get_input(func, inputs, input_count, qi, &q_bytes,
        &q_len) ||
        !tsir_ext_get_input(func, inputs, input_count, ki, &k_bytes,
            &k_len) ||
        !tsir_ext_get_input(func, inputs, input_count, vi, &v_bytes,
            &v_len) ||
        !tsir_ext_get_input(func, inputs, input_count, gi, &g_bytes,
            &g_len)) {
        return TRY_NOMATCH;
    }
    if (q_len != value_dim * 4 || k_len != key_dim * 4 ||
        v_len != key_dim * 4 || g_len != value_dim * 4) {
        return TRY_NOMATCH;
    }
    /* Faithful Zig fall-through: the state binding is read-write
     * by the declared shape, and inputBytesForReadOnlyBinding
     * returns null for any read-write binding, so this lookup
     * always fails and the Zig interpreter never produces a
     * linear_attention result. C preserves that instead of
     * redefining the input convention; the math itself is in
     * tsir_linear_attention_step. */
    if (!tsir_ext_get_input(func, inputs, input_count, si, &s_bytes,
        &s_len)) {
        return TRY_NOMATCH;
    }
    return TRY_NOMATCH;
}

