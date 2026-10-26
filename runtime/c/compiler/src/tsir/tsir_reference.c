/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_reference.h"
#include "tsir_reference_internal.h"

#include "tsir_scalar.h"
#include "tsir_sha256.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void free_partial(TsirReferenceResult *result) {
    size_t i = 0;
    if (result->outputs != NULL) {
        for (i = 0; i < result->output_count; i++) {
            free(result->outputs[i]);
        }
        free(result->outputs);
    }
    free(result->output_lens);
    result->outputs = NULL;
    result->output_lens = NULL;
    result->output_count = 0;
}

void tsir_reference_free_result(TsirReferenceResult *result) {
    if (result == NULL) {
        return;
    }
    free_partial(result);
    memset(result->reference_hash, 0, 32);
}

TsirTranscendentalTable tsir_transcendentals(void) {
    TsirTranscendentalTable table;
    table.exp_ulp_bound = 1;
    table.log_ulp_bound = 1;
    table.sin_ulp_bound = 1;
    table.cos_ulp_bound = 1;
    table.tan_ulp_bound = 2;
    table.tanh_ulp_bound = 2;
    table.rsqrt_ulp_bound = 1;
    table.recip_ulp_bound = 1;
    return table;
}

TsirStatus finish_result(TsirReferenceResult *result,
    uint8_t *single_output, size_t single_len) {
    TsirSha256 ctx;
    result->outputs = (uint8_t **)malloc(sizeof(uint8_t *));
    result->output_lens = (size_t *)malloc(sizeof(size_t));
    if (result->outputs == NULL || result->output_lens == NULL) {
        free(result->outputs);
        free(result->output_lens);
        free(single_output);
        result->outputs = NULL;
        result->output_lens = NULL;
        return TSIR_ERR_NOMEM;
    }
    result->outputs[0] = single_output;
    result->output_lens[0] = single_len;
    result->output_count = 1;
    tsir_sha256_init(&ctx);
    tsir_sha256_update(&ctx, single_output, single_len);
    tsir_sha256_final(&ctx, result->reference_hash);
    return TSIR_OK;
}

/* Find the single read-only and single read-write binding indices.
 * Returns false unless there is exactly one of each. */
bool find_read_write_pair(const TsirSemanticFunction *func,
    size_t *read_index, size_t *write_index) {
    size_t i = 0;
    bool have_read = false;
    bool have_write = false;
    for (i = 0; i < func->binding_count; i++) {
        if (func->bindings[i].read_write) {
            if (have_write) {
                return false;
            }
            have_write = true;
            *write_index = i;
        } else {
            if (have_read) {
                return false;
            }
            have_read = true;
            *read_index = i;
        }
    }
    return have_read && have_write;
}

/* Resolve the effective tree shape for an associative_allowed
 * reduction from the matching realization node, mirroring Zig. On
 * any mismatch this is NOMATCH, not an error. */
int effective_tree_shape(const TsirSemanticFunction *func,
    const TsirRealization *realization,
    const TsirReductionRegion *reduction,
    TsirReductionTreeShape *out_shape) {
    (void)func;
    if (reduction->contract.associativity == TSIR_ASSOC_STRICT_ORDERED) {
        *out_shape = TSIR_TREE_LINEAR;
        return TRY_MATCH;
    }
    if (realization == NULL || realization->function_count != 1) {
        return TRY_NOMATCH;
    } else {
        const TsirRealizationFunction *rf = &realization->functions[0];
        if (rf->semantic_index != 0 || rf->reduction_count != 1) {
            return TRY_NOMATCH;
        }
        if (rf->reductions[0].semantic_index != 0) {
            return TRY_NOMATCH;
        }
        *out_shape = rf->reductions[0].tree_shape;
        return TRY_MATCH;
    }
}

/* Fold one reduction axis worth of values. vals holds reduce_len
 * values in axis order. */
TsirStatus fold_values(TsirReductionOp op,
    TsirReductionTreeShape shape, const float *vals, size_t count,
    float *out) {
    float identity = 0.0f;
    float acc = 0.0f;
    size_t i = 0;
    TsirStatus status = TSIR_OK;
    if (count == 0) {
        status = tsir_reduction_identity_f32(op, out);
        return status;
    }
    if (shape == TSIR_TREE_BINOMIAL) {
        float *work = (float *)malloc(count * sizeof(float));
        size_t work_count = 0;
        if (work == NULL) {
            return TSIR_ERR_NOMEM;
        }
        memcpy(work, vals, count * sizeof(float));
        work_count = count;
        while (work_count > 1) {
            size_t next_count = 0;
            size_t idx = 0;
            while (idx < work_count) {
                float combined = 0.0f;
                if (idx + 1 < work_count) {
                    status = tsir_combine_f32(op, work[idx], work[idx + 1],
                        &combined);
                    if (status != TSIR_OK) {
                        free(work);
                        return status;
                    }
                } else {
                    combined = work[idx];
                }
                work[next_count] = combined;
                next_count += 1;
                idx += 2;
            }
            work_count = next_count;
        }
        *out = work[0];
        free(work);
        return TSIR_OK;
    }
    status = tsir_reduction_identity_f32(op, &identity);
    if (status != TSIR_OK) {
        return status;
    }
    acc = identity;
    for (i = 0; i < count; i++) {
        status = tsir_combine_f32(op, acc, vals[i], &acc);
        if (status != TSIR_OK) {
            return status;
        }
    }
    *out = acc;
    return TSIR_OK;
}

/* ---- empty kernel ---- */

static int try_empty_kernel(const TsirSemantic *semantic,
    const TsirInputBuffer *inputs, size_t input_count,
    TsirReferenceResult *result) {
    const TsirSemanticFunction *func = NULL;
    if (semantic->function_count != 1) {
        return TRY_NOMATCH;
    }
    func = &semantic->functions[0];
    if (func->binding_count != 0 || func->reduction_count != 0 ||
        func->collective_count != 0) {
        return TRY_NOMATCH;
    }
    if (input_count != 0) {
        return TRY_NOMATCH;
    }
    (void)inputs;
    result->outputs = NULL;
    result->output_lens = NULL;
    result->output_count = 0;
    tsir_sha256(NULL, 0, result->reference_hash);
    return TRY_MATCH;
}

/* ---- simple reduction ---- */

static int try_simple_reduction(const TsirSemantic *semantic,
    const TsirRealization *realization, const TsirInputBuffer *inputs,
    size_t input_count, TsirReferenceResult *result,
    TsirStatus *err_out) {
    const TsirSemanticFunction *func = NULL;
    const TsirReductionRegion *reduction = NULL;
    TsirReductionTreeShape shape = TSIR_TREE_LINEAR;
    size_t read_index = 0;
    size_t write_index = 0;
    const TsirBufferBinding *rb = NULL;
    const TsirBufferBinding *wb = NULL;
    uint64_t expected = 0;
    uint64_t out_expected = 0;
    TsirStatus status = TSIR_OK;
    size_t rank = 0;
    size_t axis = 0;
    size_t i = 0;

    *err_out = TSIR_OK;
    if (semantic->function_count != 1) {
        return TRY_NOMATCH;
    }
    func = &semantic->functions[0];
    if (func->binding_count != 2 || func->reduction_count != 1 ||
        func->collective_count != 0) {
        return TRY_NOMATCH;
    }
    reduction = &func->reductions[0];
    if (reduction->contract.accumulation != TSIR_SCALAR_F32) {
        return TRY_NOMATCH;
    }
    if (effective_tree_shape(func, realization, reduction, &shape) !=
        TRY_MATCH) {
        return TRY_NOMATCH;
    }
    if (!find_read_write_pair(func, &read_index, &write_index)) {
        return TRY_NOMATCH;
    }
    if (reduction->target_binding != write_index) {
        return TRY_NOMATCH;
    }
    rb = &func->bindings[read_index];
    wb = &func->bindings[write_index];
    if (rb->elem != TSIR_SCALAR_F32 && rb->elem != TSIR_SCALAR_F16 &&
        rb->elem != TSIR_SCALAR_BF16) {
        return TRY_NOMATCH;
    }
    if (wb->elem != TSIR_SCALAR_F32 && wb->elem != TSIR_SCALAR_F16 &&
        wb->elem != TSIR_SCALAR_BF16) {
        return TRY_NOMATCH;
    }
    /* A compound softmax is not a scalar fold. Both retained Zig and C
     * reject this invalid reduction contract instead of fabricating a value. */
    if (reduction->op == TSIR_REDUCTION_SOFTMAX_STABLE) {
        *err_out = TSIR_ERR_INVALID;
        return -1;
    }
    if (reduction->op != TSIR_REDUCTION_SUM &&
        reduction->op != TSIR_REDUCTION_PRODUCT &&
        reduction->op != TSIR_REDUCTION_MIN &&
        reduction->op != TSIR_REDUCTION_MAX) {
        return TRY_NOMATCH;
    }
    rank = rb->logical_shape_len;
    axis = reduction->axis;

    if (rank == 1) {
        uint64_t n64 = 0;
        size_t n = 0;
        uint8_t *output = NULL;
        float folded = 0.0f;
        if (wb->logical_shape_len != 1 || wb->logical_shape[0] != 1) {
            return TRY_NOMATCH;
        }
        if (axis != 0) {
            return TRY_NOMATCH;
        }
        n64 = rb->logical_shape[0];
        if (n64 > SIZE_MAX) {
            return TRY_NOMATCH;
        }
        n = (size_t)n64;
        if (n == 0) {
            if (input_count != 0 &&
                !(input_count == 1 && inputs[0].len == 0)) {
                return TRY_NOMATCH;
            }
            status = tsir_reduction_identity_f32(reduction->op, &folded);
            if (status != TSIR_OK) {
                *err_out = status;
                return -1;
            }
            output = (uint8_t *)malloc(tsir_scalar_byte_size(wb->elem));
            if (output == NULL) {
                *err_out = TSIR_ERR_NOMEM;
                return -1;
            }
            status = tsir_write_f32(output,
                tsir_scalar_byte_size(wb->elem), wb->elem, 0, folded);
            if (status != TSIR_OK) {
                free(output);
                *err_out = status;
                return -1;
            }
            status = finish_result(result, output,
                tsir_scalar_byte_size(wb->elem));
            if (status != TSIR_OK) {
                *err_out = status;
                return -1;
            }
            return TRY_MATCH;
        }
        if (input_count != 1) {
            return TRY_NOMATCH;
        }
        status = tsir_expected_bytes(rb, &expected);
        if (status != TSIR_OK) {
            return TRY_NOMATCH;
        }
        if (inputs[0].len != expected) {
            return TRY_NOMATCH;
        }
        {
            float *vals = (float *)malloc(n * sizeof(float));
            if (vals == NULL) {
                *err_out = TSIR_ERR_NOMEM;
                return -1;
            }
            for (i = 0; i < n; i++) {
                status = tsir_read_f32(inputs[0].data, inputs[0].len,
                    rb->elem, i, &vals[i]);
                if (status != TSIR_OK) {
                    free(vals);
                    *err_out = status;
                    return -1;
                }
            }
            status = fold_values(reduction->op, shape, vals, n, &folded);
            free(vals);
            if (status != TSIR_OK) {
                *err_out = status;
                return -1;
            }
        }
        output = (uint8_t *)malloc(tsir_scalar_byte_size(wb->elem));
        if (output == NULL) {
            *err_out = TSIR_ERR_NOMEM;
            return -1;
        }
        status = tsir_write_f32(output, tsir_scalar_byte_size(wb->elem),
            wb->elem, 0, folded);
        if (status != TSIR_OK) {
            free(output);
            *err_out = status;
            return -1;
        }
        status = finish_result(result, output,
            tsir_scalar_byte_size(wb->elem));
        if (status != TSIR_OK) {
            *err_out = status;
            return -1;
        }
        return TRY_MATCH;
    }

    /* Ranks 2 and above: generic row-major reduction. Binomial is
     * supported only for ranks 1-3 in Zig; rank 4+ binomial falls
     * through. */
    if (rank == 0) {
        return TRY_NOMATCH;
    }
    if (shape == TSIR_TREE_BINOMIAL && rank >= 4) {
        return TRY_NOMATCH;
    }
    if (axis >= rank) {
        return TRY_NOMATCH;
    }
    if (wb->logical_shape_len != rank - 1) {
        return TRY_NOMATCH;
    }
    for (i = 0; i < rank; i++) {
        size_t out_dim = 0;
        if (i == axis) {
            continue;
        }
        out_dim = i < axis ? i : i - 1;
        if (wb->logical_shape[out_dim] != rb->logical_shape[i]) {
            return TRY_NOMATCH;
        }
    }
    if (input_count != 1) {
        return TRY_NOMATCH;
    }
    status = tsir_expected_bytes(rb, &expected);
    if (status != TSIR_OK) {
        return TRY_NOMATCH;
    }
    if (inputs[0].len != expected) {
        return TRY_NOMATCH;
    }
    status = tsir_expected_bytes(wb, &out_expected);
    if (status != TSIR_OK) {
        return TRY_NOMATCH;
    }
    {
        size_t out_count = 1;
        size_t reduce_len = 0;
        size_t *strides = NULL;
        size_t *out_coords = NULL;
        float *vals = NULL;
        uint8_t *output = NULL;
        size_t out_i = 0;
        size_t out_rank = rank - 1;
        for (i = 0; i < rank; i++) {
            if (i != axis) {
                if (rb->logical_shape[i] > 0 &&
                    out_count > SIZE_MAX / rb->logical_shape[i]) {
                    return TRY_NOMATCH;
                }
                out_count *= rb->logical_shape[i];
            }
        }
        if (rb->logical_shape[axis] > SIZE_MAX) {
            return TRY_NOMATCH;
        }
        reduce_len = rb->logical_shape[axis];
        if (out_expected > SIZE_MAX) {
            return TRY_NOMATCH;
        }
        output = (uint8_t *)malloc(out_expected == 0 ? 1 : out_expected);
        if (output == NULL) {
            *err_out = TSIR_ERR_NOMEM;
            return -1;
        }
        strides = (size_t *)malloc(rank * sizeof(size_t));
        out_coords = (size_t *)malloc((out_rank == 0 ? 1 : out_rank) *
            sizeof(size_t));
        vals = (float *)malloc((reduce_len == 0 ? 1 : reduce_len) *
            sizeof(float));
        if (strides == NULL || out_coords == NULL || vals == NULL) {
            free(output);
            free(strides);
            free(out_coords);
            free(vals);
            *err_out = TSIR_ERR_NOMEM;
            return -1;
        }
        strides[rank - 1] = 1;
        for (i = rank - 1; i > 0; i--) {
            strides[i - 1] = strides[i] * rb->logical_shape[i];
        }
        memset(out_coords, 0, out_rank * sizeof(size_t));
        for (out_i = 0; out_i < out_count; out_i++) {
            size_t r = 0;
            float folded = 0.0f;
            size_t base = 0;
            size_t out_dim = 0;
            for (i = 0; i < rank; i++) {
                if (i == axis) {
                    continue;
                }
                base += out_coords[out_dim] * strides[i];
                out_dim += 1;
            }
            for (r = 0; r < reduce_len; r++) {
                size_t flat = base + r * strides[axis];
                status = tsir_read_f32(inputs[0].data, inputs[0].len,
                    rb->elem, flat, &vals[r]);
                if (status != TSIR_OK) {
                    free(output);
                    free(strides);
                    free(out_coords);
                    free(vals);
                    *err_out = status;
                    return -1;
                }
            }
            status = fold_values(reduction->op, shape, vals, reduce_len,
                &folded);
            if (status != TSIR_OK) {
                free(output);
                free(strides);
                free(out_coords);
                free(vals);
                *err_out = status;
                return -1;
            }
            status = tsir_write_f32(output, out_expected, wb->elem, out_i,
                folded);
            if (status != TSIR_OK) {
                free(output);
                free(strides);
                free(out_coords);
                free(vals);
                *err_out = status;
                return -1;
            }
            /* Odometer over the surviving coordinates. */
            if (out_rank > 0) {
                size_t pos = out_rank - 1;
                for (;;) {
                    out_coords[pos] += 1;
                    if (out_coords[pos] <
                        wb->logical_shape[pos]) {
                        break;
                    }
                    out_coords[pos] = 0;
                    if (pos == 0) {
                        break;
                    }
                    pos -= 1;
                }
            }
        }
        free(strides);
        free(out_coords);
        free(vals);
        status = finish_result(result, output, out_expected);
        if (status != TSIR_OK) {
            *err_out = status;
            return -1;
        }
        return TRY_MATCH;
    }
}

/* Find a body binding role, requiring exactly one occurrence. */
bool find_body_role(const TsirSemanticBody *body,
    TsirSemanticBindingRole role, uint32_t *out_index) {
    size_t i = 0;
    bool found = false;
    for (i = 0; i < body->binding_role_count; i++) {
        if (body->binding_roles[i].role == role) {
            if (found) {
                return false;
            }
            found = true;
            *out_index = body->binding_roles[i].binding_index;
        }
    }
    if (!found) {
        return false;
    }
    return true;
}

bool find_axis_role(const TsirSemanticBody *body,
    TsirSemanticAxisRole role, uint32_t *out_index) {
    size_t i = 0;
    bool found = false;
    for (i = 0; i < body->axis_role_count; i++) {
        if (body->axis_roles[i].role == role) {
            if (found) {
                return false;
            }
            found = true;
            *out_index = body->axis_roles[i].axis_index;
        }
    }
    if (!found) {
        return false;
    }
    return true;
}

/* ---- run ---- */

TsirStatus tsir_reference_run(const TsirSemantic *semantic,
    const TsirRealization *realization, const TsirInputBuffer *inputs,
    size_t input_count, TsirReferenceResult *out) {
    TsirStatus err = TSIR_OK;
    int tried = 0;
    if (semantic == NULL || realization == NULL || out == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (input_count > 0 && inputs == NULL) {
        return TSIR_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    if (semantic->rejection_count != 0 ||
        realization->rejection_count != 0) {
        return TSIR_ERR_REJECTED;
    }
    tried = try_empty_kernel(semantic, inputs, input_count, out);
    if (tried == TRY_MATCH) {
        return TSIR_OK;
    }
    tried = try_simple_reduction(semantic, realization, inputs,
        input_count, out, &err);
    if (tried == TRY_MATCH) {
        return TSIR_OK;
    }
    if (tried < 0) {
        return err;
    }
    tried = try_fused_gemv(semantic, realization, inputs, input_count,
        out, &err);
    if (tried == TRY_MATCH) {
        return TSIR_OK;
    }
    if (tried < 0) {
        return err;
    }
    tried = try_rms_norm(semantic, inputs, input_count, out, &err);
    if (tried == TRY_MATCH) {
        return TSIR_OK;
    }
    if (tried < 0) {
        return err;
    }
    tried = try_gather(semantic, inputs, input_count, out, &err);
    if (tried == TRY_MATCH) {
        return TSIR_OK;
    }
    if (tried < 0) {
        return err;
    }
    tried = tsir_try_extended_ops(semantic, inputs, input_count,
        out, &err);
    if (tried == TRY_MATCH) {
        return TSIR_OK;
    }
    if (tried < 0) {
        return err;
    }
    /* Every other unmatched shape: visible unsupported, never a
     * fabricated output. */
    return TSIR_ERR_UNSUPPORTED;
}
