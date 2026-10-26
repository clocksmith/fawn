/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_common.h"
#include "tsir.h"
#include "tsir_frontend_expr.h"
#include "tsir_frontend_passes.h"
#include "tsir_sha256.h"

static const char text[] = {'a', '\0', 'b'};

static void print_hash(const uint8_t value[32]) {
    char hex[65];
    hex_digest(value, hex);
    puts(hex);
}

static int identities(void) {
    TsirIterationAxis axis = {.name = text, .lower_bound = text,
        .upper_bound = text, .step = text, .name_length = sizeof(text),
        .lower_bound_length = sizeof(text), .upper_bound_length = sizeof(text),
        .step_length = sizeof(text)};
    TsirBufferBinding binding = {.name = text, .elem = TSIR_SCALAR_F32,
        .name_length = sizeof(text)};
    TsirRejectionEntry rejection = {.reason = TSIR_REJECT_TARGET_UNFIT,
        .node_path = text, .detail = text, .node_path_length = sizeof(text),
        .detail_length = sizeof(text)};
    TsirCollectiveSemanticNode collective = {.kind = TSIR_COLLECTIVE_SUBGROUP_ADD,
        .axis = -1, .dtype = TSIR_SCALAR_F32, .exactness = {
            .class = TSIR_EXACTNESS_TOLERANCE_BOUNDED, .tolerance_metric = text,
            .tolerance_metric_length = sizeof(text), .tolerance_epsilon = 0.5}};
    TsirSemanticFunction function = {.name = text, .name_length = sizeof(text),
        .axes = &axis, .axis_count = 1, .bindings = &binding, .binding_count = 1,
        .collectives = &collective, .collective_count = 1,
        .body = {.has_rms_norm = true, .rms_norm = {.epsilon = {
            .source = TSIR_EPSILON_UNIFORM_FIELD, .path = text, .path_length = sizeof(text)}}}};
    TsirSemantic semantic = {.contract_version = 1, .frontend_version = text,
        .frontend_version_length = sizeof(text), .functions = &function,
        .function_count = 1, .rejections = &rejection, .rejection_count = 1};
    TsirRealizationFunction realized = {.emitter_params_json = text,
        .emitter_params_json_length = sizeof(text)};
    TsirRealization realization = {.contract_version = 1, .functions = &realized,
        .function_count = 1, .rejections = &rejection, .rejection_count = 1};
    TsirManifestLoweringEntry entry = {.kernel_ref = text, .backend = text,
        .frontend_version = text, .compiler_version = text,
        .kernel_ref_length = sizeof(text), .backend_length = sizeof(text),
        .frontend_version_length = sizeof(text), .compiler_version_length = sizeof(text),
        .exactness = collective.exactness};
    TsirTargetIdentity descriptor = *tsir_target_descriptor(TSIR_TARGET_WEBGPU_GENERIC);
    TsirNativeCollective capability = {.kind_name = text, .exactness_name = text,
        .kind_name_length = sizeof(text), .exactness_name_length = sizeof(text)};
    TsirDigests digests;
    uint8_t hash[32];
    if (tsir_digests_compute_version_span(&semantic, &realization, text,
            sizeof(text), &digests) != TSIR_OK) return 1;
    print_hash(digests.semantic);
    print_hash(digests.realization);
    print_hash(digests.emitter);
    if (tsir_manifest_entry_digest(&entry, hash) != TSIR_OK) return 1;
    print_hash(hash);
    descriptor.name = text;
    descriptor.name_length = sizeof(text);
    descriptor.native_collectives = &capability;
    descriptor.native_collective_count = 1;
    if (tsir_target_descriptor_hash(&descriptor, hash) != TSIR_OK) return 1;
    print_hash(hash);
    memcpy(realized.target_descriptor_hash, hash, sizeof(hash));
    const TsirBackendTextSpec spec = {.version_key = text, .body_comment = text,
        .version_key_length = sizeof(text), .body_comment_length = sizeof(text)};
    char *skeleton = NULL;
    size_t skeleton_length = 0;
    if (tsir_emit_skeleton_function_span(&realized, text, sizeof(text), hash,
            &spec, &skeleton, &skeleton_length) != TSIR_OK) return 1;
    tsir_sha256((const uint8_t *)skeleton, skeleton_length, hash);
    free(skeleton);
    print_hash(hash);
    return 0;
}

static void text_controls(void) {
    char *copy = tsir_text_duplicate(text, sizeof(text));
    CHECK(copy != NULL && memcmp(copy, text, sizeof(text)) == 0 && copy[3] == 0);
    free(copy);
    CHECK(!tsir_text_equal(text, sizeof(text), "a", 0));
    CHECK(tsir_text_equal("", 0, "", 0));
    CHECK(tsir_text_duplicate(text, SIZE_MAX) == NULL);
    CHECK(!tsir_text_starts_with(text, 1, "ab"));
    TsirRejectionList rejections = {0};
    CHECK(tsir_frontend_rejection_append_span(&rejections, TSIR_REJECT_TARGET_UNFIT,
        text, sizeof(text), text, sizeof(text)));
    CHECK(rejections.items[0].detail_length == sizeof(text));
    CHECK(memcmp(rejections.items[0].detail, text, sizeof(text)) == 0);
    tsir_frontend_rejection_free(&rejections);
    TsirExactness exactness = {.class = TSIR_EXACTNESS_TOLERANCE_BOUNDED,
        .tolerance_metric = "\0", .tolerance_metric_length = 1};
    CHECK(tsir_exactness_is_well_formed(&exactness));
    exactness.class = TSIR_EXACTNESS_BIT_EXACT_SOLO;
    CHECK(!tsir_exactness_is_well_formed(&exactness));
}

static void frontend_controls(void) {
    TsirIrStmt statement = {.kind = TSIR_IR_STMT_BLOCK};
    TsirIrFunction function = {.name = text, .name_length = sizeof(text),
        .stmts = &statement, .stmt_count = 1};
    TsirWgslModule module = {.functions = &function, .function_count = 1};
    TsirSemantic semantic = {0};
    uint8_t source_hash[32] = {0};
    CHECK_STATUS(tsir_frontend_lower_span(&module, source_hash, text,
        sizeof(text), &semantic), TSIR_OK);
    CHECK(semantic.functions[0].name_length == sizeof(text));
    CHECK(memcmp(semantic.functions[0].name, text, sizeof(text)) == 0);
    CHECK(semantic.frontend_version_length == sizeof(text));
    tsir_frontend_free_semantic(&semantic);
    TsirIrGlobal global = {.name = text, .name_length = sizeof(text),
        .class = TSIR_IR_GLOBAL_OVERRIDE};
    char *bound = NULL;
    size_t length = 0;
    CHECK(tsir_expr_override_or_const_name(&global, &bound, &length) == 1);
    CHECK(length == 12 && memcmp(bound, "override:a\0b", length) == 0);
    free(bound);
}

static void planner_emitter_controls(void) {
    TsirTargetDescriptor descriptor = *tsir_target_descriptor(TSIR_TARGET_WEBGPU_GENERIC);
    static const char *const modes[] = {"f32\0extra"};
    const size_t lengths[] = {9};
    descriptor.native_numerical_modes = modes;
    descriptor.native_numerical_mode_count = 1;
    descriptor.native_numerical_mode_lengths = lengths;
    CHECK(!tsir_supports_numerical_mode(&descriptor, TSIR_SCALAR_F32));
    TsirIterationAxis axis = {.name = "i", .lower_bound = "0", .step = "1",
        .upper_bound = "32\0extra", .upper_bound_length = 8};
    TsirSemanticFunction function = {.name = "main", .axes = &axis, .axis_count = 1};
    TsirSemantic semantic = {.functions = &function, .function_count = 1};
    TsirPlannerOptions options;
    TsirPlannedRealization plan = {0};
    tsir_planner_default_options(&options);
    options.emitter_params_json = text;
    options.emitter_params_json_length = sizeof(text);
    CHECK_STATUS(tsir_plan_realization(&semantic, &descriptor, &options, &plan), TSIR_OK);
    CHECK(plan.functions[0].tiles.per_axis[0] == 1);
    CHECK(plan.functions[0].emitter_params_json_length == sizeof(text));
    char *output = NULL;
    size_t output_length = 0;
    CHECK_STATUS(tsir_emit_skeleton_function_span(&plan.functions[0], text, sizeof(text),
        plan.functions[0].target_descriptor_hash, &tsir_spec_dxil, &output, &output_length), TSIR_OK);
    CHECK(output_length > strlen(output));
    const char expected[] = "// target.name = a\0b\n";
    size_t offset = strlen("// doe.tsir.dxil_skeleton.version = 1\n");
    CHECK(memcmp(output + offset, expected, sizeof(expected) - 1) == 0);
    free(output);
    tsir_plan_free_realization(&plan);
}

static void lookup_controls(void) {
    static const char outer[] = {'i', '\0', 'x'};
    static const char inner[] = {'i', '\0', 'y'};
    TsirIrLocal locals[2] = {{.name = outer, .name_length = 3},
        {.name = inner, .name_length = 3}};
    TsirIrExpr expressions[8] = {
        {.kind = TSIR_IR_EXPR_GLOBAL_REF, .ref_index = 0},
        {.kind = TSIR_IR_EXPR_LOCAL_REF, .ref_index = 0},
        {.kind = TSIR_IR_EXPR_LOAD, .inner = 1},
        {.kind = TSIR_IR_EXPR_LOCAL_REF, .ref_index = 1},
        {.kind = TSIR_IR_EXPR_LOAD, .inner = 3},
        {.kind = TSIR_IR_EXPR_BINARY, .binary_lhs = 2, .binary_rhs = 4},
        {.kind = TSIR_IR_EXPR_INDEX, .index_base = 0, .index_index = 5},
        {.kind = TSIR_IR_EXPR_LOAD, .inner = 6}};
    TsirIrFunction function = {.name = "main", .locals = locals, .local_count = 2,
        .exprs = expressions, .expr_count = 8};
    TsirIterationAxis axes[2] = {{.name = outer, .name_length = 3,
        .upper_bound = "uniform:u.hidden"}, {.name = inner, .name_length = 3}};
    TsirReductionRegion reduction = {.axis = 1, .op = TSIR_REDUCTION_SUM};
    TsirKernelFamilyHint hint = TSIR_HINT_UNKNOWN;
    CHECK_STATUS(tsir_family_hint_infer_ir(&function, axes, 2, &reduction, 1, &hint), TSIR_OK);
    CHECK(hint == TSIR_HINT_FUSED_GEMV);
    axes[1].name = "i\0z";
    CHECK_STATUS(tsir_family_hint_infer_ir(&function, axes, 2, &reduction, 1, &hint), TSIR_OK);
    CHECK(hint == TSIR_HINT_REDUCTION);

    TsirIrType types[2] = {{.kind = TSIR_IR_TYPE_SCALAR, .scalar = TSIR_IR_F32},
        {.kind = TSIR_IR_TYPE_STRUCT, .struct_id = 0}};
    TsirIrStructField field = {.name = "eps", .ty = 0};
    TsirIrStructDef definition = {.name = "U", .fields = &field, .field_count = 1};
    TsirIrGlobal global = {.name = "u\0x", .name_length = 3, .ty = 1};
    TsirWgslModule module = {.types = types, .type_count = 2, .structs = &definition,
        .struct_count = 1, .globals = &global, .global_count = 1};
    TsirBufferBinding bindings[4] = {{.name = "input"}, {.name = "scale"},
        {.name = "output"}, {.name = "u\0x", .name_length = 3}};
    axes[0].upper_bound = "uniform:u\0x.hidden";
    axes[0].upper_bound_length = sizeof("uniform:u\0x.hidden") - 1;
    axes[1].upper_bound = axes[0].upper_bound;
    axes[1].upper_bound_length = axes[0].upper_bound_length;
    TsirSemanticBody body = {0};
    CHECK_STATUS(tsir_frontend_infer_body(&module, TSIR_HINT_REDUCTION, axes, 2,
        bindings, 4, &reduction, 1, &body), TSIR_OK);
    CHECK(body.op == TSIR_BODY_RMS_NORM && body.has_rms_norm);
    CHECK(body.rms_norm.epsilon.path_length == sizeof("uniform:u\0x.eps") - 1);
    CHECK(memcmp(body.rms_norm.epsilon.path, "uniform:u\0x.eps",
        body.rms_norm.epsilon.path_length) == 0);
    free((void *)body.binding_roles);
    free((void *)body.axis_roles);
    free((void *)body.rms_norm.epsilon.path);
    bindings[0].name = "input\0wrong";
    bindings[0].name_length = sizeof("input\0wrong") - 1;
    CHECK_STATUS(tsir_frontend_infer_body(&module, TSIR_HINT_REDUCTION, axes, 2,
        bindings, 4, &reduction, 1, &body), TSIR_OK);
    CHECK(body.op == TSIR_BODY_UNKNOWN && !body.has_rms_norm);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--identities") == 0) return identities();
    text_controls();
    frontend_controls();
    planner_emitter_controls();
    lookup_controls();
    return finish_tests("text spans");
}
