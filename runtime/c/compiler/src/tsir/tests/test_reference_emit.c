/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_common.h"

#include "tsir_emit.h"
#include "tsir_frontend.h"
#include "tsir_reference.h"

/* Build a one-function semantic for a simple reduction. */
static TsirSemantic make_simple(TsirSemanticFunction *func,
    TsirBufferBinding *bindings, TsirReductionRegion *reduction,
    TsirReductionOp op, TsirReductionAssociativity assoc) {
    TsirSemantic semantic;
    memset(func, 0, sizeof(*func));
    func->name = "k";
    func->bindings = bindings;
    func->binding_count = 2;
    func->reductions = reduction;
    func->reduction_count = 1;
    reduction->axis = 0;
    reduction->op = op;
    reduction->contract.accumulation = TSIR_SCALAR_F32;
    reduction->contract.associativity = assoc;
    reduction->contract.nan_inf = TSIR_NANINF_PROPAGATE;
    reduction->target_binding = 1;
    memset(&semantic, 0, sizeof(semantic));
    semantic.contract_version = 1;
    semantic.frontend_version = "";
    semantic.functions = func;
    semantic.function_count = 1;
    return semantic;
}

static TsirRealization empty_realization(void) {
    TsirRealization realization;
    memset(&realization, 0, sizeof(realization));
    realization.contract_version = 1;
    return realization;
}

static void test_empty_and_rejected(void) {
    TsirSemanticFunction func;
    TsirSemantic semantic;
    TsirRealization realization = empty_realization();
    TsirReferenceResult result;
    TsirStatus st = TSIR_OK;
    char hex[65];
    memset(&func, 0, sizeof(func));
    func.name = "k";
    memset(&semantic, 0, sizeof(semantic));
    semantic.contract_version = 1;
    semantic.frontend_version = "";
    semantic.functions = &func;
    semantic.function_count = 1;
    st = tsir_reference_run(&semantic, &realization, NULL, 0, &result);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(result.output_count == 0);
    hex_digest(result.reference_hash, hex);
    CHECK_STR(hex,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    tsir_reference_free_result(&result);
    /* One semantic rejection blocks everything. */
    {
        TsirRejectionEntry rejection;
        memset(&rejection, 0, sizeof(rejection));
        rejection.reason = TSIR_REJECT_TARGET_UNFIT;
        rejection.node_path = "functions[0]";
        rejection.detail = "x";
        semantic.rejections = &rejection;
        semantic.rejection_count = 1;
        st = tsir_reference_run(&semantic, &realization, NULL, 0, &result);
        CHECK_STATUS(st, TSIR_ERR_REJECTED);
        CHECK(result.output_count == 0);
    }
}

static void test_rank1_ops(void) {
    TsirSemanticFunction func;
    TsirBufferBinding bindings[2];
    TsirReductionRegion reduction;
    uint64_t in_shape[1] = { 4 };
    uint64_t out_shape[1] = { 1 };
    uint8_t input[16];
    TsirInputBuffer inputs[1];
    TsirRealization realization = empty_realization();
    TsirReferenceResult result;
    TsirStatus st = TSIR_OK;
    memset(bindings, 0, sizeof(bindings));
    bindings[0].name = "x";
    bindings[0].logical_shape = in_shape;
    bindings[0].logical_shape_len = 1;
    bindings[0].elem = TSIR_SCALAR_F32;
    bindings[0].read_write = false;
    bindings[1].name = "y";
    bindings[1].logical_shape = out_shape;
    bindings[1].logical_shape_len = 1;
    bindings[1].elem = TSIR_SCALAR_F32;
    bindings[1].read_write = true;
    put_f32(input, 0, 1.0f);
    put_f32(input, 1, 2.0f);
    put_f32(input, 2, 3.0f);
    put_f32(input, 3, 4.0f);
    inputs[0].data = input;
    inputs[0].len = sizeof(input);

    memset(&reduction, 0, sizeof(reduction));
    {
        TsirSemantic semantic = make_simple(&func, bindings, &reduction, TSIR_REDUCTION_SUM, TSIR_ASSOC_STRICT_ORDERED);
        st = tsir_reference_run(&semantic, &realization, inputs, 1,
            &result);
        CHECK_STATUS(st, TSIR_OK);
        CHECK(result.output_count == 1);
        CHECK(get_f32(result.outputs[0], 0) == 10.0f);
        tsir_reference_free_result(&result);
    }
    {
        TsirSemantic semantic = make_simple(&func, bindings, &reduction, TSIR_REDUCTION_PRODUCT, TSIR_ASSOC_STRICT_ORDERED);
        st = tsir_reference_run(&semantic, &realization, inputs, 1,
            &result);
        CHECK_STATUS(st, TSIR_OK);
        CHECK(get_f32(result.outputs[0], 0) == 24.0f);
        tsir_reference_free_result(&result);
    }
    {
        TsirSemantic semantic = make_simple(&func, bindings, &reduction, TSIR_REDUCTION_MIN, TSIR_ASSOC_STRICT_ORDERED);
        st = tsir_reference_run(&semantic, &realization, inputs, 1,
            &result);
        CHECK_STATUS(st, TSIR_OK);
        CHECK(get_f32(result.outputs[0], 0) == 1.0f);
        tsir_reference_free_result(&result);
    }
    /* Binomial vs linear, n = 4: [1, 1e8, -1e8, 1] -> linear 1,
     * binomial 0. */
    {
        uint8_t bin_input[16];
        TsirInputBuffer bin_inputs[1];
        TsirRealizationFunction rfunc;
        TsirReductionRealizationNode rnode;
        TsirRealization realization_bin;
        put_f32(bin_input, 0, 1.0f);
        put_f32(bin_input, 1, 100000000.0f);
        put_f32(bin_input, 2, -100000000.0f);
        put_f32(bin_input, 3, 1.0f);
        bin_inputs[0].data = bin_input;
        bin_inputs[0].len = sizeof(bin_input);
        {
            TsirSemantic semantic = make_simple(&func, bindings, &reduction, TSIR_REDUCTION_SUM,
                TSIR_ASSOC_STRICT_ORDERED);
            st = tsir_reference_run(&semantic, &realization, bin_inputs,
                1, &result);
            CHECK_STATUS(st, TSIR_OK);
            CHECK(get_f32(result.outputs[0], 0) == 1.0f);
            tsir_reference_free_result(&result);
        }
        memset(&rnode, 0, sizeof(rnode));
        rnode.semantic_index = 0;
        rnode.tree_shape = TSIR_TREE_BINOMIAL;
        memset(&rfunc, 0, sizeof(rfunc));
        rfunc.semantic_index = 0;
        rfunc.reductions = &rnode;
        rfunc.reduction_count = 1;
        rfunc.emitter_params_json = "{}";
        memset(&realization_bin, 0, sizeof(realization_bin));
        realization_bin.contract_version = 1;
        realization_bin.functions = &rfunc;
        realization_bin.function_count = 1;
        {
            TsirSemantic semantic = make_simple(&func, bindings, &reduction, TSIR_REDUCTION_SUM,
                TSIR_ASSOC_ASSOCIATIVE_ALLOWED);
            st = tsir_reference_run(&semantic, &realization_bin,
                bin_inputs, 1, &result);
            CHECK_STATUS(st, TSIR_OK);
            CHECK(get_f32(result.outputs[0], 0) == 0.0f);
            tsir_reference_free_result(&result);
        }
        /* Associative without a matching node: unsupported. */
        {
            TsirSemantic semantic = make_simple(&func, bindings, &reduction, TSIR_REDUCTION_SUM,
                TSIR_ASSOC_ASSOCIATIVE_ALLOWED);
            st = tsir_reference_run(&semantic, &realization, bin_inputs,
                1, &result);
            CHECK_STATUS(st, TSIR_ERR_UNSUPPORTED);
        }
    }
    /* Empty rank-1 input: identity. */
    {
        uint64_t empty_shape[1] = { 0 };
        bindings[0].logical_shape = empty_shape;
        {
            TsirSemantic semantic = make_simple(&func, bindings, &reduction, TSIR_REDUCTION_SUM,
                TSIR_ASSOC_STRICT_ORDERED);
            st = tsir_reference_run(&semantic, &realization, NULL, 0,
                &result);
            CHECK_STATUS(st, TSIR_OK);
            CHECK(get_f32(result.outputs[0], 0) == 0.0f);
            tsir_reference_free_result(&result);
        }
        bindings[0].logical_shape = in_shape;
    }
    /* Softmax op in a simple-reduction shape: visible invalid. */
    {
        TsirSemantic semantic = make_simple(&func, bindings, &reduction, TSIR_REDUCTION_SOFTMAX_STABLE, TSIR_ASSOC_STRICT_ORDERED);
        st = tsir_reference_run(&semantic, &realization, inputs, 1,
            &result);
        CHECK_STATUS(st, TSIR_ERR_INVALID);
    }
}

static void test_rank2_rank3_rank4(void) {
    TsirSemanticFunction func;
    TsirBufferBinding bindings[2];
    TsirReductionRegion reduction;
    TsirRealization realization = empty_realization();
    TsirReferenceResult result;
    TsirStatus st = TSIR_OK;
    /* Rank 2, axis 1: [[1,2],[3,4]] -> [3,7]. */
    {
        uint64_t in_shape[2] = { 2, 2 };
        uint64_t out_shape[1] = { 2 };
        uint8_t input[16];
        TsirInputBuffer inputs[1];
        TsirSemantic semantic;
        memset(bindings, 0, sizeof(bindings));
        bindings[0].name = "x";
        bindings[0].logical_shape = in_shape;
        bindings[0].logical_shape_len = 2;
        bindings[0].elem = TSIR_SCALAR_F32;
        bindings[1].name = "y";
        bindings[1].logical_shape = out_shape;
        bindings[1].logical_shape_len = 1;
        bindings[1].elem = TSIR_SCALAR_F32;
        bindings[1].read_write = true;
        put_f32(input, 0, 1.0f);
        put_f32(input, 1, 2.0f);
        put_f32(input, 2, 3.0f);
        put_f32(input, 3, 4.0f);
        inputs[0].data = input;
        inputs[0].len = sizeof(input);
        memset(&func, 0, sizeof(func));
        func.name = "k";
        func.bindings = bindings;
        func.binding_count = 2;
        memset(&reduction, 0, sizeof(reduction));
        reduction.axis = 1;
        reduction.op = TSIR_REDUCTION_SUM;
        reduction.contract.accumulation = TSIR_SCALAR_F32;
        reduction.contract.associativity = TSIR_ASSOC_STRICT_ORDERED;
        reduction.contract.nan_inf = TSIR_NANINF_PROPAGATE;
        reduction.target_binding = 1;
        func.reductions = &reduction;
        func.reduction_count = 1;
        memset(&semantic, 0, sizeof(semantic));
        semantic.contract_version = 1;
        semantic.frontend_version = "";
        semantic.functions = &func;
        semantic.function_count = 1;
        st = tsir_reference_run(&semantic, &realization, inputs, 1,
            &result);
        CHECK_STATUS(st, TSIR_OK);
        CHECK(get_f32(result.outputs[0], 0) == 3.0f);
        CHECK(get_f32(result.outputs[0], 1) == 7.0f);
        tsir_reference_free_result(&result);
        /* Axis 0: [4, 6]. */
        reduction.axis = 0;
        st = tsir_reference_run(&semantic, &realization, inputs, 1,
            &result);
        CHECK_STATUS(st, TSIR_OK);
        CHECK(get_f32(result.outputs[0], 0) == 4.0f);
        CHECK(get_f32(result.outputs[0], 1) == 6.0f);
        tsir_reference_free_result(&result);
    }
    /* Rank 4 generic, axis 2, shape [2,1,2,1] values 1..4:
     * output shape [2,1,1], values [1+2, 3+4] = [3,7]. */
    {
        uint64_t in_shape[4] = { 2, 1, 2, 1 };
        uint64_t out_shape[3] = { 2, 1, 1 };
        uint8_t input[16];
        TsirInputBuffer inputs[1];
        TsirSemantic semantic;
        size_t i = 0;
        memset(bindings, 0, sizeof(bindings));
        bindings[0].name = "x";
        bindings[0].logical_shape = in_shape;
        bindings[0].logical_shape_len = 4;
        bindings[0].elem = TSIR_SCALAR_F32;
        bindings[1].name = "y";
        bindings[1].logical_shape = out_shape;
        bindings[1].logical_shape_len = 3;
        bindings[1].elem = TSIR_SCALAR_F32;
        bindings[1].read_write = true;
        for (i = 0; i < 4; i++) {
            put_f32(input, i, (float)(i + 1));
        }
        inputs[0].data = input;
        inputs[0].len = sizeof(input);
        memset(&func, 0, sizeof(func));
        func.name = "k";
        func.bindings = bindings;
        func.binding_count = 2;
        memset(&reduction, 0, sizeof(reduction));
        reduction.axis = 2;
        reduction.op = TSIR_REDUCTION_SUM;
        reduction.contract.accumulation = TSIR_SCALAR_F32;
        reduction.contract.associativity = TSIR_ASSOC_STRICT_ORDERED;
        reduction.contract.nan_inf = TSIR_NANINF_PROPAGATE;
        reduction.target_binding = 1;
        func.reductions = &reduction;
        func.reduction_count = 1;
        memset(&semantic, 0, sizeof(semantic));
        semantic.contract_version = 1;
        semantic.frontend_version = "";
        semantic.functions = &func;
        semantic.function_count = 1;
        st = tsir_reference_run(&semantic, &realization, inputs, 1,
            &result);
        CHECK_STATUS(st, TSIR_OK);
        CHECK(result.output_lens[0] == 8);
        CHECK(get_f32(result.outputs[0], 0) == 3.0f);
        CHECK(get_f32(result.outputs[0], 1) == 7.0f);
        tsir_reference_free_result(&result);
        /* Binomial at rank 4: unsupported. */
        {
            TsirRealizationFunction rfunc;
            TsirReductionRealizationNode rnode;
            TsirRealization realization_bin;
            memset(&rnode, 0, sizeof(rnode));
            rnode.semantic_index = 0;
            rnode.tree_shape = TSIR_TREE_BINOMIAL;
            memset(&rfunc, 0, sizeof(rfunc));
            rfunc.semantic_index = 0;
            rfunc.reductions = &rnode;
            rfunc.reduction_count = 1;
            rfunc.emitter_params_json = "{}";
            memset(&realization_bin, 0, sizeof(realization_bin));
            realization_bin.contract_version = 1;
            realization_bin.functions = &rfunc;
            realization_bin.function_count = 1;
            reduction.contract.associativity =
                TSIR_ASSOC_ASSOCIATIVE_ALLOWED;
            st = tsir_reference_run(&semantic, &realization_bin, inputs,
                1, &result);
            CHECK_STATUS(st, TSIR_ERR_UNSUPPORTED);
        }
    }
}

static void test_gemv_rmsnorm_gather(void) {
    TsirRealization realization = empty_realization();
    TsirReferenceResult result;
    TsirStatus st = TSIR_OK;
    /* GEMV: W [[1,2],[3,4]], x [5,6] -> [17,39]. */
    {
        TsirBufferBinding bindings[3];
        TsirSemanticBodyBinding roles[3];
        TsirSemanticBodyAxis axes_roles[2];
        TsirIterationAxis axes[2];
        TsirReductionRegion reduction;
        TsirSemanticFunction func;
        TsirSemantic semantic;
        uint64_t w_shape[2] = { 2, 2 };
        uint64_t v_shape[1] = { 2 };
        uint64_t o_shape[1] = { 2 };
        uint8_t w[16];
        uint8_t x[8];
        TsirInputBuffer inputs[2];
        size_t i = 0;
        memset(bindings, 0, sizeof(bindings));
        bindings[0].name = "w";
        bindings[0].logical_shape = w_shape;
        bindings[0].logical_shape_len = 2;
        bindings[0].elem = TSIR_SCALAR_F32;
        bindings[1].name = "x";
        bindings[1].logical_shape = v_shape;
        bindings[1].logical_shape_len = 1;
        bindings[1].elem = TSIR_SCALAR_F32;
        bindings[2].name = "y";
        bindings[2].logical_shape = o_shape;
        bindings[2].logical_shape_len = 1;
        bindings[2].elem = TSIR_SCALAR_F32;
        bindings[2].read_write = true;
        roles[0].binding_index = 0;
        roles[0].role = TSIR_ROLE_MATRIX;
        roles[1].binding_index = 1;
        roles[1].role = TSIR_ROLE_VECTOR;
        roles[2].binding_index = 2;
        roles[2].role = TSIR_ROLE_OUTPUT;
        axes_roles[0].axis_index = 0;
        axes_roles[0].role = TSIR_AXIS_OUTPUT;
        axes_roles[1].axis_index = 1;
        axes_roles[1].role = TSIR_AXIS_REDUCTION;
        memset(axes, 0, sizeof(axes));
        axes[0].name = "i";
        axes[0].lower_bound = "0";
        axes[0].upper_bound = "2";
        axes[0].step = "1";
        axes[1].name = "k";
        axes[1].lower_bound = "0";
        axes[1].upper_bound = "2";
        axes[1].step = "1";
        memset(&reduction, 0, sizeof(reduction));
        reduction.axis = 1;
        reduction.op = TSIR_REDUCTION_SUM;
        reduction.contract.accumulation = TSIR_SCALAR_F32;
        reduction.contract.associativity = TSIR_ASSOC_STRICT_ORDERED;
        reduction.contract.nan_inf = TSIR_NANINF_PROPAGATE;
        reduction.target_binding = 2;
        memset(&func, 0, sizeof(func));
        func.name = "gemv";
        func.axes = axes;
        func.axis_count = 2;
        func.bindings = bindings;
        func.binding_count = 3;
        func.reductions = &reduction;
        func.reduction_count = 1;
        func.body.op = TSIR_BODY_FUSED_GEMV;
        func.body.binding_roles = roles;
        func.body.binding_role_count = 3;
        func.body.axis_roles = axes_roles;
        func.body.axis_role_count = 2;
        memset(&semantic, 0, sizeof(semantic));
        semantic.contract_version = 1;
        semantic.frontend_version = "";
        semantic.functions = &func;
        semantic.function_count = 1;
        for (i = 0; i < 4; i++) {
            put_f32(w, i, (float)(i + 1));
        }
        put_f32(x, 0, 5.0f);
        put_f32(x, 1, 6.0f);
        inputs[0].data = w;
        inputs[0].len = sizeof(w);
        inputs[1].data = x;
        inputs[1].len = sizeof(x);
        st = tsir_reference_run(&semantic, &realization, inputs, 2,
            &result);
        CHECK_STATUS(st, TSIR_OK);
        CHECK(get_f32(result.outputs[0], 0) == 17.0f);
        CHECK(get_f32(result.outputs[0], 1) == 39.0f);
        tsir_reference_free_result(&result);
    }
    /* RMSNorm literal eps: x [3,4], scale [1,1], eps 0 ->
     * inv_rms = 1/sqrt(12.5), outputs [3,4]/sqrt(12.5). */
    {
        TsirBufferBinding bindings[3];
        TsirSemanticBodyBinding roles[3];
        TsirSemanticBodyAxis axes_roles[2];
        TsirIterationAxis axes[2];
        TsirReductionRegion reduction;
        TsirSemanticFunction func;
        TsirSemantic semantic;
        uint64_t shape[1] = { 2 };
        uint8_t x[8];
        uint8_t s[8];
        TsirInputBuffer inputs[2];
        float inv = 0.0f;
        memset(bindings, 0, sizeof(bindings));
        bindings[0].name = "x";
        bindings[0].logical_shape = shape;
        bindings[0].logical_shape_len = 1;
        bindings[0].elem = TSIR_SCALAR_F32;
        bindings[1].name = "scale";
        bindings[1].logical_shape = shape;
        bindings[1].logical_shape_len = 1;
        bindings[1].elem = TSIR_SCALAR_F32;
        bindings[2].name = "y";
        bindings[2].logical_shape = shape;
        bindings[2].logical_shape_len = 1;
        bindings[2].elem = TSIR_SCALAR_F32;
        bindings[2].read_write = true;
        roles[0].binding_index = 0;
        roles[0].role = TSIR_ROLE_INPUT;
        roles[1].binding_index = 1;
        roles[1].role = TSIR_ROLE_SCALE;
        roles[2].binding_index = 2;
        roles[2].role = TSIR_ROLE_OUTPUT;
        axes_roles[0].axis_index = 0;
        axes_roles[0].role = TSIR_AXIS_HIDDEN;
        axes_roles[1].axis_index = 1;
        axes_roles[1].role = TSIR_AXIS_REDUCTION;
        memset(axes, 0, sizeof(axes));
        axes[0].name = "d";
        axes[0].lower_bound = "0";
        axes[0].upper_bound = "2";
        axes[0].step = "1";
        axes[1].name = "r";
        axes[1].lower_bound = "0";
        axes[1].upper_bound = "2";
        axes[1].step = "1";
        memset(&reduction, 0, sizeof(reduction));
        reduction.axis = 1;
        reduction.op = TSIR_REDUCTION_SUM;
        reduction.contract.accumulation = TSIR_SCALAR_F32;
        reduction.contract.associativity = TSIR_ASSOC_STRICT_ORDERED;
        reduction.contract.nan_inf = TSIR_NANINF_PROPAGATE;
        reduction.target_binding = 2;
        memset(&func, 0, sizeof(func));
        func.name = "rms";
        func.axes = axes;
        func.axis_count = 2;
        func.bindings = bindings;
        func.binding_count = 3;
        func.reductions = &reduction;
        func.reduction_count = 1;
        func.body.op = TSIR_BODY_RMS_NORM;
        func.body.binding_roles = roles;
        func.body.binding_role_count = 3;
        func.body.axis_roles = axes_roles;
        func.body.axis_role_count = 2;
        func.body.has_rms_norm = true;
        func.body.rms_norm.formula =
            TSIR_RMSNORM_SUM_SQUARES_MEAN_EPSILON_RSQRT_SCALE;
        func.body.rms_norm.epsilon.source = TSIR_EPSILON_LITERAL_F32;
        func.body.rms_norm.epsilon.path = "";
        func.body.rms_norm.epsilon.has_literal_f64 = true;
        func.body.rms_norm.epsilon.literal_f64 = 0.0;
        func.body.rms_norm.hidden_extent_axis = 0;
        func.body.rms_norm.reduction_target =
            TSIR_RMSNORM_INTERMEDIATE_SCALAR;
        memset(&semantic, 0, sizeof(semantic));
        semantic.contract_version = 1;
        semantic.frontend_version = "";
        semantic.functions = &func;
        semantic.function_count = 1;
        put_f32(x, 0, 3.0f);
        put_f32(x, 1, 4.0f);
        put_f32(s, 0, 1.0f);
        put_f32(s, 1, 1.0f);
        inputs[0].data = x;
        inputs[0].len = sizeof(x);
        inputs[1].data = s;
        inputs[1].len = sizeof(s);
        st = tsir_reference_run(&semantic, &realization, inputs, 2,
            &result);
        CHECK_STATUS(st, TSIR_OK);
        inv = 1.0f / sqrtf(12.5f);
        CHECK(fabsf(get_f32(result.outputs[0], 0) - 3.0f * inv) < 1e-6f);
        CHECK(fabsf(get_f32(result.outputs[0], 1) - 4.0f * inv) < 1e-6f);
        tsir_reference_free_result(&result);
        /* Uniform epsilon with a wrong field name: unsupported. */
        func.body.rms_norm.epsilon.source = TSIR_EPSILON_UNIFORM_FIELD;
        func.body.rms_norm.epsilon.path = "uniform:u.epsilon";
        func.body.rms_norm.epsilon.has_literal_f64 = false;
        func.body.rms_norm.epsilon.binding_index.has = true;
        func.body.rms_norm.epsilon.binding_index.value = 0;
        func.body.rms_norm.epsilon.byte_offset.has = true;
        func.body.rms_norm.epsilon.byte_offset.value = 0;
        st = tsir_reference_run(&semantic, &realization, inputs, 2,
            &result);
        CHECK_STATUS(st, TSIR_ERR_UNSUPPORTED);
    }
    /* Gather: indices [2,0], table rows -> [[30,31],[10,11]]. */
    {
        TsirBufferBinding bindings[3];
        TsirSemanticBodyBinding roles[3];
        TsirSemanticBodyAxis axes_roles[2];
        TsirIterationAxis axes[2];
        TsirSemanticFunction func;
        TsirSemantic semantic;
        uint64_t idx_shape[1] = { 2 };
        uint64_t tab_shape[2] = { 3, 2 };
        uint64_t out_shape[2] = { 2, 2 };
        uint8_t indices[8];
        uint8_t table[24];
        TsirInputBuffer inputs[2];
        size_t i = 0;
        memset(bindings, 0, sizeof(bindings));
        bindings[0].name = "idx";
        bindings[0].logical_shape = idx_shape;
        bindings[0].logical_shape_len = 1;
        bindings[0].elem = TSIR_SCALAR_U32;
        bindings[1].name = "tab";
        bindings[1].logical_shape = tab_shape;
        bindings[1].logical_shape_len = 2;
        bindings[1].elem = TSIR_SCALAR_F32;
        bindings[2].name = "out";
        bindings[2].logical_shape = out_shape;
        bindings[2].logical_shape_len = 2;
        bindings[2].elem = TSIR_SCALAR_F32;
        bindings[2].read_write = true;
        roles[0].binding_index = 0;
        roles[0].role = TSIR_ROLE_INDICES;
        roles[1].binding_index = 1;
        roles[1].role = TSIR_ROLE_TABLE;
        roles[2].binding_index = 2;
        roles[2].role = TSIR_ROLE_OUTPUT;
        axes_roles[0].axis_index = 0;
        axes_roles[0].role = TSIR_AXIS_TOKEN;
        axes_roles[1].axis_index = 1;
        axes_roles[1].role = TSIR_AXIS_HIDDEN;
        memset(axes, 0, sizeof(axes));
        axes[0].name = "t";
        axes[0].lower_bound = "0";
        axes[0].upper_bound = "2";
        axes[0].step = "1";
        axes[1].name = "h";
        axes[1].lower_bound = "0";
        axes[1].upper_bound = "2";
        axes[1].step = "1";
        memset(&func, 0, sizeof(func));
        func.name = "gather";
        func.axes = axes;
        func.axis_count = 2;
        func.bindings = bindings;
        func.binding_count = 3;
        func.body.op = TSIR_BODY_GATHER;
        func.body.binding_roles = roles;
        func.body.binding_role_count = 3;
        func.body.axis_roles = axes_roles;
        func.body.axis_role_count = 2;
        memset(&semantic, 0, sizeof(semantic));
        semantic.contract_version = 1;
        semantic.frontend_version = "";
        semantic.functions = &func;
        semantic.function_count = 1;
        indices[0] = 2;
        indices[1] = 0;
        indices[2] = 0;
        indices[3] = 0;
        indices[4] = 0;
        indices[5] = 0;
        indices[6] = 0;
        indices[7] = 0;
        for (i = 0; i < 6; i++) {
            put_f32(table, i, (float)(10 + i));
        }
        /* Table rows are [10,11],[20,21],[30,31] in row-major f32:
         * rebuild accordingly. */
        put_f32(table, 0, 10.0f);
        put_f32(table, 1, 11.0f);
        put_f32(table, 2, 20.0f);
        put_f32(table, 3, 21.0f);
        put_f32(table, 4, 30.0f);
        put_f32(table, 5, 31.0f);
        inputs[0].data = indices;
        inputs[0].len = sizeof(indices);
        inputs[1].data = table;
        inputs[1].len = sizeof(table);
        st = tsir_reference_run(&semantic, &realization, inputs, 2,
            &result);
        CHECK_STATUS(st, TSIR_OK);
        CHECK(get_f32(result.outputs[0], 0) == 30.0f);
        CHECK(get_f32(result.outputs[0], 1) == 31.0f);
        CHECK(get_f32(result.outputs[0], 2) == 10.0f);
        CHECK(get_f32(result.outputs[0], 3) == 11.0f);
        tsir_reference_free_result(&result);
        /* Out-of-range index: unsupported, no output. */
        indices[0] = 3;
        st = tsir_reference_run(&semantic, &realization, inputs, 2,
            &result);
        CHECK_STATUS(st, TSIR_ERR_UNSUPPORTED);
    }
    /* Extended-op shapes are visibly unsupported. */
    {
        TsirReductionRegion reduction;
        TsirSemanticFunction func;
        TsirSemantic semantic;
        TsirSemanticBodyOp ops[7] = {
            TSIR_BODY_RESIDUAL_ADD, TSIR_BODY_GELU_GATED,
            TSIR_BODY_ATTENTION_SCORES, TSIR_BODY_L2_NORMALIZE,
            TSIR_BODY_CONV1D_DEPTHWISE, TSIR_BODY_LINEAR_ATTENTION,
            TSIR_BODY_UNKNOWN
        };
        size_t i = 0;
        memset(&reduction, 0, sizeof(reduction));
        reduction.axis = 0;
        reduction.op = TSIR_REDUCTION_SUM;
        reduction.contract.accumulation = TSIR_SCALAR_F32;
        reduction.contract.associativity = TSIR_ASSOC_STRICT_ORDERED;
        reduction.contract.nan_inf = TSIR_NANINF_PROPAGATE;
        reduction.target_binding = 0;
        for (i = 0; i < 7; i++) {
            memset(&func, 0, sizeof(func));
            func.name = "k";
            func.reductions = &reduction;
            func.reduction_count = 1;
            func.body.op = ops[i];
            memset(&semantic, 0, sizeof(semantic));
            semantic.contract_version = 1;
            semantic.frontend_version = "";
            semantic.functions = &func;
            semantic.function_count = 1;
            st = tsir_reference_run(&semantic, &realization, NULL, 0,
                &result);
            CHECK_STATUS(st, TSIR_ERR_UNSUPPORTED);
        }
    }
    /* Transcendental table metadata. */
    {
        TsirTranscendentalTable table = tsir_transcendentals();
        CHECK(table.exp_ulp_bound == 1);
        CHECK(table.tan_ulp_bound == 2);
        CHECK(table.tanh_ulp_bound == 2);
    }
}

static void test_emit_and_frontend(void) {
    TsirBuffer buf;
    char *text = NULL;
    size_t len = 0;
    TsirStatus st = TSIR_OK;
    /* Metadata block exactness. */
    {
        TsirResidencyDecision decision;
        memset(&decision, 0, sizeof(decision));
        decision.binding_index = 0;
        decision.class = TSIR_RESIDENCY_PE_SLICED;
        decision.axis.has = true;
        decision.axis.value = 1;
        decision.shards.has = true;
        decision.shards.value = 4;
        tsir_buf_init(&buf);
        tsir_emit_write_residency(&buf, &decision, 1);
        CHECK_STATUS(buf.err, TSIR_OK);
        CHECK_STR(buf.data,
            "// residency.count = 1\n"
            "// residency[0].binding_index = 0\n"
            "// residency[0].class = pe_sliced\n"
            "// residency[0].axis = 1\n"
            "// residency[0].shards = 4\n"
            "// residency[0].fabric_color = none\n"
            "// residency[0].chunk_bytes = none\n"
            "\n");
        tsir_buf_free(&buf);
    }
    {
        uint32_t tiles[2] = { 64, 8 };
        tsir_buf_init(&buf);
        tsir_emit_write_tiles(&buf, tiles, 2);
        CHECK_STR(buf.data,
            "// tiles.count = 2\n"
            "// tiles.per_axis[0] = 64\n"
            "// tiles.per_axis[1] = 8\n"
            "\n");
        tsir_buf_free(&buf);
    }
    /* Skeleton header and gates. */
    {
        TsirRealizationFunction rfunc;
        TsirRealization realization;
        uint8_t hash[32];
        memset(hash, 0xab, 32);
        memset(&rfunc, 0, sizeof(rfunc));
        rfunc.semantic_index = 0;
        rfunc.pe_grid.width = 8;
        rfunc.pe_grid.height = 1;
        rfunc.emitter_params_json = "{}";
        memcpy(rfunc.target_descriptor_hash, hash, 32);
        memset(&realization, 0, sizeof(realization));
        realization.contract_version = 1;
        realization.functions = &rfunc;
        realization.function_count = 1;
        st = tsir_emit_skeleton(&realization, 0, "test-target", hash,
            &tsir_spec_dxil, &text, &len);
        CHECK_STATUS(st, TSIR_OK);
        CHECK(text != NULL &&
            strstr(text, "// doe.tsir.dxil_skeleton.version = 1\n") == text);
        CHECK(text != NULL &&
            strstr(text, "// target.name = test-target\n") != NULL);
        CHECK(text != NULL &&
            strstr(text, "// semantic_index = 0\n") != NULL);
        CHECK(text != NULL &&
            strstr(text,
                "; tsir mechanical skeleton: DXIL module body is emitted by later lowering.\n") != NULL);
        free(text);
        text = NULL;
        st = tsir_emit_skeleton(&realization, 5, "test-target", hash,
            &tsir_spec_dxil, &text, &len);
        CHECK_STATUS(st, TSIR_ERR_RANGE);
        CHECK(text == NULL);
        /* Mismatched hash: invalid. */
        {
            uint8_t other[32];
            memset(other, 0, 32);
            st = tsir_emit_skeleton(&realization, 0, "test-target",
                other, &tsir_spec_dxil, &text, &len);
            CHECK_STATUS(st, TSIR_ERR_INVALID);
        }
        /* Rejection gate. */
        {
            TsirRejectionEntry rejection;
            memset(&rejection, 0, sizeof(rejection));
            rejection.reason = TSIR_REJECT_TARGET_UNFIT;
            rejection.node_path = "functions[0]";
            rejection.detail = "x";
            realization.rejections = &rejection;
            realization.rejection_count = 1;
            st = tsir_emit_skeleton(&realization, 0, "test-target",
                hash, &tsir_spec_dxil, &text, &len);
            CHECK_STATUS(st, TSIR_ERR_REJECTED);
            CHECK(text == NULL);
            realization.rejection_count = 0;
        }
        /* Semantic-aware: gates pass, body missing. */
        {
            TsirSemanticFunction sfunc;
            TsirSemantic semantic;
            memset(&sfunc, 0, sizeof(sfunc));
            sfunc.name = "k";
            memset(&semantic, 0, sizeof(semantic));
            semantic.contract_version = 1;
            semantic.frontend_version = "";
            semantic.functions = &sfunc;
            semantic.function_count = 1;
            st = tsir_emit_semantic(&semantic, &realization, 0,
                "test-target", hash, &tsir_spec_msl, &text, &len);
            CHECK_STATUS(st, TSIR_ERR_UNSUPPORTED);
            CHECK(text == NULL);
        }
    }
    /* Dtype routing and context helpers. */
    CHECK_STR(tsir_csl_elem_name(TSIR_SCALAR_BF16), "bf16");
    CHECK_STR(tsir_wgsl_elem_name(TSIR_SCALAR_BF16), "f16");
    CHECK(tsir_is_supported_compute_elem(TSIR_SCALAR_F16));
    CHECK(!tsir_is_supported_compute_elem(TSIR_SCALAR_BF16));
    CHECK_STATUS(tsir_require_supported_compute_elem(TSIR_SCALAR_BF16),
        TSIR_ERR_UNSUPPORTED);
    tsir_buf_init(&buf);
    tsir_write_wgsl_f16_enable(&buf, TSIR_SCALAR_F16);
    CHECK_STR(buf.data, "enable f16;\n\n");
    tsir_buf_free(&buf);
    tsir_buf_init(&buf);
    tsir_write_wgsl_f16_enable(&buf, TSIR_SCALAR_F32);
    CHECK(buf.len == 0);
    tsir_buf_free(&buf);
    tsir_buf_init(&buf);
    tsir_write_csl_sqrt_nr(&buf, TSIR_SCALAR_F32);
    CHECK_STR(buf.data,
        "fn sqrt_nr(x: f32) f32 {\n"
        "    const y0: f32 = math.sqrt(x);\n"
        "    return 0.5 * (y0 + x / y0);\n"
        "}\n\n");
    tsir_buf_free(&buf);
    tsir_buf_init(&buf);
    tsir_write_csl_f16_pack_loop(&buf, "tsir_", "x", "128");
    CHECK_STR(buf.data,
        "    for (@range(i16, 128 / 2)) |pair| {\n"
        "        const base = @as(u32, pair) * 2;\n"
        "        const lo: u32 = @as(u32, @bitcast(u16, tsir_x[base]));\n"
        "        const hi: u32 = @as(u32, @bitcast(u16, tsir_x[base + 1]));\n"
        "        tsir_x_bits[@as(u32, pair)] = lo | (hi << 16);\n"
        "    }\n");
    tsir_buf_free(&buf);
    {
        TsirEmitConfig config;
        tsir_emit_default_config(&config);
        CHECK_STR(config.var_prefix, "tsir_");
        CHECK(!config.chunk_size_default.has);
        CHECK(config.kv_cache_pe_strategy == TSIR_KV_FULL_PER_PE);
    }
    /* Frontend: scalar mapping, classifier, unsupported lowering. */
    CHECK(tsir_ir_scalar_to_kind(TSIR_IR_ABSTRACT_INT) == TSIR_SCALAR_I32);
    CHECK(tsir_ir_scalar_to_kind(TSIR_IR_ABSTRACT_FLOAT) ==
        TSIR_SCALAR_F32);
    CHECK(tsir_ir_scalar_to_kind(TSIR_IR_BOOL) == TSIR_SCALAR_U32);
    CHECK(tsir_ir_scalar_to_kind(TSIR_IR_VOID) == TSIR_SCALAR_U32);
    CHECK(tsir_family_hint_classify(2, 1, true, false) ==
        TSIR_HINT_FUSED_GEMV);
    CHECK(tsir_family_hint_classify(2, 1, false, false) ==
        TSIR_HINT_REDUCTION);
    CHECK(tsir_family_hint_classify(1, 0, false, true) == TSIR_HINT_GATHER);
    CHECK(tsir_family_hint_classify(1, 0, false, false) ==
        TSIR_HINT_ELEMENTWISE);
    CHECK(tsir_family_hint_classify(0, 0, false, false) ==
        TSIR_HINT_UNKNOWN);
    CHECK_STATUS(tsir_frontend_lower(NULL, NULL, NULL, NULL),
        TSIR_ERR_INVALID);
    /* Kernel body dispatch: unsupported for every op. */
    tsir_buf_init(&buf);
    CHECK_STATUS(tsir_emit_kernel_body(NULL, 0, &buf),
        TSIR_ERR_INVALID);
    tsir_buf_free(&buf);
}

int main(void) {
    test_empty_and_rejected();
    test_rank1_ops();
    test_rank2_rank3_rank4();
    test_gemv_rmsnorm_gather();
    test_emit_and_frontend();
    return finish_tests("test_reference_emit");
}
