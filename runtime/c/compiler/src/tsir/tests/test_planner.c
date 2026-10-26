/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_common.h"

#include "tsir_planner.h"
#include "tsir_targets.h"

static const char *modes_a[] = { "f32", "f16", "bf16" };
static const char *modes_b[] = { "f32" };
static const TsirNativeCollective collectives_a[] = {
    { .kind_name = "subgroup_add", .exactness_name = "bit_exact_solo" },
    { .kind_name = "fabric_allreduce", .exactness_name = "algorithm_exact" }
};

static TsirTargetDescriptor descriptor_a(void) {
    TsirTargetDescriptor d = {0};
    memset(&d, 0, sizeof(d));
    d.pe_working_memory_bytes = 65536;
    d.fabric_color_count = 4;
    d.max_collective_group_size = 32;
    d.native_numerical_modes = modes_a;
    d.native_numerical_mode_count = 3;
    d.native_collectives = collectives_a;
    d.native_collective_count = 2;
    d.runtime_sized_binding_policy = TSIR_POLICY_HOST_COPIED;
    d.name = "test-a";
    d.streaming_gemm = "none";
    d.sub_tile_lane_width = 1;
    return d;
}

static TsirTargetDescriptor descriptor_b(void) {
    TsirTargetDescriptor d = {0};
    memset(&d, 0, sizeof(d));
    d.pe_working_memory_bytes = 64;
    d.fabric_color_count = 0;
    d.max_collective_group_size = 1;
    d.native_numerical_modes = modes_b;
    d.native_numerical_mode_count = 1;
    d.native_collectives = NULL;
    d.native_collective_count = 0;
    d.runtime_sized_binding_policy = TSIR_POLICY_REJECT;
    d.name = "test-b";
    d.streaming_gemm = "none";
    d.sub_tile_lane_width = 1;
    return d;
}

static void test_tiles_and_grid(void) {
    TsirTargetDescriptor a = descriptor_a();
    TsirTargetDescriptor b = descriptor_b();
    TsirPEGridShape grid;
    CHECK(tsir_planner_choose_tile("100") == 64);
    CHECK(tsir_planner_choose_tile("63") == 32);
    CHECK(tsir_planner_choose_tile("1") == 1);
    CHECK(tsir_planner_choose_tile("0") == 1);
    CHECK(tsir_planner_choose_tile("abc") == 1);
    CHECK(tsir_planner_choose_tile("64") == 64);
    CHECK(tsir_planner_choose_tile("65") == 64);
    CHECK(tsir_planner_choose_tile("") == 1);
    CHECK(tsir_planner_choose_tile("12x") == 1);
    grid = tsir_planner_choose_pe_grid(&a);
    CHECK(grid.width == 8 && grid.height == 1);
    grid = tsir_planner_choose_pe_grid(&b);
    CHECK(grid.width == 1 && grid.height == 1);
    CHECK(tsir_supports_numerical_mode(&a, TSIR_SCALAR_BF16));
    CHECK(!tsir_supports_numerical_mode(&a, TSIR_SCALAR_I32));
    CHECK(!tsir_supports_numerical_mode(&b, TSIR_SCALAR_F16));
}

static TsirSemantic make_semantic(const TsirSemanticFunction *func) {
    TsirSemantic semantic = {0};
    memset(&semantic, 0, sizeof(semantic));
    semantic.contract_version = 1;
    semantic.frontend_version = "";
    semantic.functions = func;
    semantic.function_count = 1;
    return semantic;
}

static void test_residency_replicated_and_sliced(void) {
    TsirTargetDescriptor b = descriptor_b();
    TsirBufferBinding binding = {0};
    TsirSemanticFunction func = {0};
    TsirSemantic semantic = {0};
    TsirPlannerOptions options = {0};
    TsirPlannedRealization plan;
    TsirStatus st = TSIR_OK;
    uint64_t shape[1] = { 16 };
    memset(&binding, 0, sizeof(binding));
    binding.name = "x";
    binding.logical_shape = shape;
    binding.logical_shape_len = 1;
    binding.elem = TSIR_SCALAR_F32;
    memset(&func, 0, sizeof(func));
    func.name = "k";
    func.bindings = &binding;
    func.binding_count = 1;
    semantic = make_semantic(&func);
    tsir_planner_default_options(&options);
    st = tsir_plan_realization(&semantic, &b, &options, &plan);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(plan.rejection_count == 0);
    CHECK(plan.function_count == 1);
    CHECK(plan.functions[0].residency_count == 1);
    CHECK(plan.functions[0].residency[0].class ==
        TSIR_RESIDENCY_PE_REPLICATED);
    CHECK_STR(plan.functions[0].emitter_params_json,
        "{\"planner\":\"tsir.first_fit_v1\"}");
    tsir_plan_free_realization(&plan);

    /* Sliced: 256 bytes under budget 64 with width 4 -> 64 fits.
     * Use a descriptor with width 4 (max group 4, colors 1). */
    {
        TsirTargetDescriptor d = descriptor_b();
        uint64_t shape2[1] = { 64 };
        d.fabric_color_count = 1;
        d.max_collective_group_size = 4;
        binding.logical_shape = shape2;
        st = tsir_plan_realization(&semantic, &d, &options, &plan);
        CHECK_STATUS(st, TSIR_OK);
        CHECK(plan.rejection_count == 0);
        CHECK(plan.functions[0].residency_count == 1);
        CHECK(plan.functions[0].residency[0].class ==
            TSIR_RESIDENCY_PE_SLICED);
        CHECK(plan.functions[0].residency[0].axis.has &&
            plan.functions[0].residency[0].axis.value == 0);
        CHECK(plan.functions[0].residency[0].shards.has &&
            plan.functions[0].residency[0].shards.value == 4);
        tsir_plan_free_realization(&plan);
    }

    /* Rejection: same binding under fixture B (width 1, no stream). */
    {
        uint64_t shape2[1] = { 64 };
        binding.logical_shape = shape2;
        st = tsir_plan_realization(&semantic, &b, &options, &plan);
        CHECK_STATUS(st, TSIR_OK);
        CHECK(plan.rejection_count == 1);
        CHECK(plan.rejections[0].reason ==
            TSIR_REJECT_PE_BUDGET_EXHAUSTED);
        CHECK_STR(plan.rejections[0].node_path,
            "functions[0].bindings[0]");
        CHECK_STR(plan.rejections[0].detail,
            "binding footprint exceeds per-PE budget without legal slice or stream");
        CHECK(plan.functions[0].residency_count == 0);
        tsir_plan_free_realization(&plan);
    }
}

static void test_residency_streamed_and_host(void) {
    TsirBufferBinding binding = {0};
    TsirSemanticFunction func = {0};
    TsirSemantic semantic = {0};
    TsirPlannerOptions options = {0};
    TsirPlannedRealization plan;
    TsirStatus st = TSIR_OK;
    uint64_t shape[1] = { 0 };
    TsirTargetDescriptor d = descriptor_a();
    memset(&binding, 0, sizeof(binding));
    binding.name = "x";
    binding.logical_shape = shape;
    binding.logical_shape_len = 1;
    binding.elem = TSIR_SCALAR_F32;
    memset(&func, 0, sizeof(func));
    func.name = "k";
    func.bindings = &binding;
    func.binding_count = 1;
    semantic = make_semantic(&func);
    /* Host-copied policy under descriptor A. */
    tsir_planner_default_options(&options);
    st = tsir_plan_realization(&semantic, &d, &options, &plan);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(plan.functions[0].residency[0].class ==
        TSIR_RESIDENCY_HOST_COPIED);
    tsir_plan_free_realization(&plan);
    /* Streamed policy with loader streaming, chunk 4096. */
    d.runtime_sized_binding_policy = TSIR_POLICY_FABRIC_STREAMED_WITH_LOADER;
    options.loader.fabric_streaming = true;
    options.loader.max_stream_chunk_bytes = 4096;
    st = tsir_plan_realization(&semantic, &d, &options, &plan);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(plan.functions[0].residency[0].class ==
        TSIR_RESIDENCY_FABRIC_STREAMED);
    CHECK(plan.functions[0].residency[0].fabric_color.has &&
        plan.functions[0].residency[0].fabric_color.value == 0);
    CHECK(plan.functions[0].residency[0].chunk_bytes.has &&
        plan.functions[0].residency[0].chunk_bytes.value == 4096);
    tsir_plan_free_realization(&plan);
    /* Chunk 0 means the pe budget. */
    options.loader.max_stream_chunk_bytes = 0;
    st = tsir_plan_realization(&semantic, &d, &options, &plan);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(plan.functions[0].residency[0].chunk_bytes.has &&
        plan.functions[0].residency[0].chunk_bytes.value == 65536);
    tsir_plan_free_realization(&plan);
}

static void test_reduction_and_collective(void) {
    TsirTargetDescriptor a = descriptor_a();
    TsirTargetDescriptor b = descriptor_b();
    TsirReductionRegion reduction;
    TsirCollectiveSemanticNode collective;
    TsirSemanticFunction func = {0};
    TsirSemantic semantic = {0};
    TsirPlannerOptions options = {0};
    TsirPlannedRealization plan;
    TsirStatus st = TSIR_OK;
    TsirIterationAxis axis = {0};
    memset(&reduction, 0, sizeof(reduction));
    reduction.axis = 0;
    reduction.op = TSIR_REDUCTION_SUM;
    reduction.contract.accumulation = TSIR_SCALAR_F32;
    reduction.contract.associativity = TSIR_ASSOC_ASSOCIATIVE_ALLOWED;
    reduction.contract.nan_inf = TSIR_NANINF_PROPAGATE;
    memset(&collective, 0, sizeof(collective));
    collective.kind = TSIR_COLLECTIVE_SUBGROUP_ADD;
    collective.axis = 0;
    collective.exactness.class = TSIR_EXACTNESS_BIT_EXACT_SOLO;
    collective.exactness.tolerance_metric = "";
    collective.dtype = TSIR_SCALAR_F32;
    memset(&axis, 0, sizeof(axis));
    axis.name = "i";
    axis.lower_bound = "0";
    axis.upper_bound = "100";
    axis.step = "1";
    memset(&func, 0, sizeof(func));
    func.name = "k";
    func.axes = &axis;
    func.axis_count = 1;
    func.reductions = &reduction;
    func.reduction_count = 1;
    func.collectives = &collective;
    func.collective_count = 1;
    semantic = make_semantic(&func);
    tsir_planner_default_options(&options);
    st = tsir_plan_realization(&semantic, &a, &options, &plan);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(plan.rejection_count == 0);
    CHECK(plan.functions[0].reduction_count == 1);
    CHECK(plan.functions[0].reductions[0].tree_shape == TSIR_TREE_LINEAR);
    CHECK(plan.functions[0].collective_count == 1);
    /* Tile for "100" is 64; group = min(32, 64) = 32. */
    CHECK(plan.functions[0].collectives[0].group_size == 32);
    CHECK(plan.functions[0].tiles.per_axis[0] == 64);
    tsir_plan_free_realization(&plan);

    /* Unsupported accumulation dtype under fixture B. Zig order is
     * collectives before reductions, so the collective rejection
     * (no native collectives in fixture B) comes first and the
     * reduction rejection second. */
    reduction.contract.accumulation = TSIR_SCALAR_BF16;
    st = tsir_plan_realization(&semantic, &b, &options, &plan);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(plan.rejection_count == 2);
    CHECK(plan.rejections[0].reason ==
        TSIR_REJECT_COLLECTIVE_NOT_REPRESENTABLE);
    CHECK(plan.rejections[1].reason == TSIR_REJECT_TARGET_UNFIT);
    CHECK(plan.functions[0].reduction_count == 0);
    tsir_plan_free_realization(&plan);
}

static void test_fabric_colors(void) {
    TsirTargetDescriptor a = descriptor_a();
    TsirCollectiveSemanticNode nodes[5];
    TsirSemanticFunction func = {0};
    TsirSemantic semantic = {0};
    TsirPlannerOptions options = {0};
    TsirPlannedRealization plan;
    TsirStatus st = TSIR_OK;
    size_t i = 0;
    for (i = 0; i < 5; i++) {
        memset(&nodes[i], 0, sizeof(nodes[i]));
        nodes[i].kind = TSIR_COLLECTIVE_FABRIC_ALLREDUCE;
        nodes[i].axis = -1;
        nodes[i].exactness.class = TSIR_EXACTNESS_ALGORITHM_EXACT;
        nodes[i].exactness.tolerance_metric = "";
        nodes[i].dtype = TSIR_SCALAR_F32;
    }
    memset(&func, 0, sizeof(func));
    func.name = "k";
    func.collectives = nodes;
    func.collective_count = 5;
    semantic = make_semantic(&func);
    tsir_planner_default_options(&options);
    st = tsir_plan_realization(&semantic, &a, &options, &plan);
    CHECK_STATUS(st, TSIR_OK);
    /* 4 colors: first four nodes get colors 0..3, fifth rejected. */
    CHECK(plan.functions[0].collective_count == 4);
    for (i = 0; i < 4; i++) {
        CHECK(plan.functions[0].collectives[i].fabric_color.has);
        CHECK(plan.functions[0].collectives[i].fabric_color.value ==
            (uint32_t)i);
    }
    CHECK(plan.rejection_count == 1);
    CHECK(plan.rejections[0].reason ==
        TSIR_REJECT_COLLECTIVE_NOT_REPRESENTABLE);
    CHECK_STR(plan.rejections[0].detail,
        "target fabric color budget exhausted");
    tsir_plan_free_realization(&plan);
}

static void test_two_failures_accumulate(void) {
    TsirTargetDescriptor b = descriptor_b();
    TsirBufferBinding bindings[2];
    TsirSemanticFunction func = {0};
    TsirSemantic semantic = {0};
    TsirPlannerOptions options = {0};
    TsirPlannedRealization plan;
    TsirStatus st = TSIR_OK;
    uint64_t shape[1] = { 64 };
    size_t i = 0;
    for (i = 0; i < 2; i++) {
        memset(&bindings[i], 0, sizeof(bindings[i]));
        bindings[i].name = "x";
        bindings[i].logical_shape = shape;
        bindings[i].logical_shape_len = 1;
        bindings[i].elem = TSIR_SCALAR_F32;
    }
    memset(&func, 0, sizeof(func));
    func.name = "k";
    func.bindings = bindings;
    func.binding_count = 2;
    semantic = make_semantic(&func);
    tsir_planner_default_options(&options);
    st = tsir_plan_realization(&semantic, &b, &options, &plan);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(plan.rejection_count == 2);
    CHECK_STR(plan.rejections[0].node_path, "functions[0].bindings[0]");
    CHECK_STR(plan.rejections[1].node_path, "functions[0].bindings[1]");
    tsir_plan_free_realization(&plan);
}

static void test_planned_identity(void) {
    TsirTargetDescriptor descriptor = *tsir_target_descriptor(TSIR_TARGET_WEBGPU_GENERIC);
    TsirSemanticFunction function = {0};
    TsirSemantic semantic = make_semantic(&function);
    TsirPlannedRealization plan;
    uint8_t expected[32];
    uint8_t original[32];
    CHECK_STATUS(tsir_target_descriptor_hash(&descriptor, expected), TSIR_OK);
    memcpy(original, expected, sizeof(original));
    CHECK_STATUS(tsir_plan_realization(&semantic, &descriptor, NULL, &plan), TSIR_OK);
    CHECK(plan.function_count == 1);
    CHECK(memcmp(plan.functions[0].target_descriptor_hash, expected, sizeof(expected)) == 0);
    tsir_plan_free_realization(&plan);

    /* The planner must hash identity-only fields too, rather than just its
     * placement inputs or a previously computed caller-supplied byte array. */
    descriptor.pe_persistent_pool_bytes += 1;
    CHECK_STATUS(tsir_target_descriptor_hash(&descriptor, expected), TSIR_OK);
    CHECK(memcmp(original, expected, sizeof(expected)) != 0);
    CHECK_STATUS(tsir_plan_realization(&semantic, &descriptor, NULL, &plan), TSIR_OK);
    CHECK(memcmp(plan.functions[0].target_descriptor_hash, expected, sizeof(expected)) == 0);
    char *text = NULL;
    size_t text_length = 0;
    CHECK_STATUS(tsir_emit_skeleton_function(&plan.functions[0], descriptor.name,
        original, &tsir_spec_dxil, &text, &text_length), TSIR_ERR_INVALID);
    CHECK(text == NULL && text_length == 0);
    CHECK_STATUS(tsir_emit_skeleton_function(&plan.functions[0], descriptor.name,
        expected, &tsir_spec_dxil, &text, &text_length), TSIR_OK);
    CHECK(text != NULL && text_length != 0);
    free(text);
    tsir_plan_free_realization(&plan);
    descriptor.name = NULL;
    memset(&plan, 0xa5, sizeof(plan));
    CHECK_STATUS(tsir_plan_realization(&semantic, &descriptor, NULL, &plan), TSIR_ERR_INVALID);
    CHECK(plan.functions == NULL && plan.function_count == 0 && plan.rejections == NULL);
}

int main(void) {
    test_tiles_and_grid();
    test_residency_replicated_and_sliced();
    test_residency_streamed_and_host();
    test_reduction_and_collective();
    test_fabric_colors();
    test_two_failures_accumulate();
    test_planned_identity();
    return finish_tests("test_planner");
}
