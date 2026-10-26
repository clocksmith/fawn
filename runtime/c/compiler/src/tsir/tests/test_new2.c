/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_common.h"

#include "tsir_bootstrap.h"
#include "tsir_emit.h"
#include "tsir_sha256.h"
#include "tsir_frontend.h"

/* ---- kernel-body fixtures ---- */

static TsirSemanticFunction kb_func;
static TsirBufferBinding kb_bindings[4];
static TsirSemanticBodyBinding kb_roles[3];
static TsirSemantic kb_semantic;

static void kb_setup(TsirSemanticBodyOp op,
    const TsirSemanticBindingRole *roles) {
    size_t i = 0;
    memset(&kb_func, 0, sizeof(kb_func));
    memset(kb_bindings, 0, sizeof(kb_bindings));
    memset(kb_roles, 0, sizeof(kb_roles));
    for (i = 0; i < 3; i++) {
        kb_roles[i].binding_index = (uint32_t)i;
        kb_roles[i].role = roles[i];
    }
    kb_func.name = "main";
    kb_func.bindings = kb_bindings;
    kb_func.binding_count = 3;
    kb_func.body.op = op;
    kb_func.body.binding_roles = kb_roles;
    kb_func.body.binding_role_count = 3;
    memset(&kb_semantic, 0, sizeof(kb_semantic));
    kb_semantic.functions = &kb_func;
    kb_semantic.function_count = 1;
}

static void kb_binding(size_t index, uint32_t group,
    uint32_t binding, TsirScalarKind elem) {
    kb_bindings[index].name = "b";
    kb_bindings[index].group = group;
    kb_bindings[index].binding = binding;
    kb_bindings[index].elem = elem;
}

static void test_kernel_bodies(void) {
    TsirBuffer buf;
    TsirStatus status = TSIR_OK;
    static const TsirSemanticBindingRole gemv_roles[3] = {
        TSIR_ROLE_MATRIX, TSIR_ROLE_VECTOR, TSIR_ROLE_OUTPUT
    };
    static const TsirSemanticBindingRole gather_roles[3] = {
        TSIR_ROLE_INDICES, TSIR_ROLE_TABLE, TSIR_ROLE_OUTPUT
    };
    static const TsirSemanticBindingRole rms_roles[3] = {
        TSIR_ROLE_INPUT, TSIR_ROLE_SCALE, TSIR_ROLE_OUTPUT
    };
    /* WGSL fused_gemv, exact full text. */
    kb_setup(TSIR_BODY_FUSED_GEMV, gemv_roles);
    kb_binding(0, 0, 0, TSIR_SCALAR_F32);
    kb_binding(1, 0, 1, TSIR_SCALAR_F32);
    kb_binding(2, 0, 2, TSIR_SCALAR_F32);
    tsir_buf_init(&buf);
    status = tsir_emit_kernel_body(&kb_func, TSIR_KERNEL_WEBGPU,
        &buf);
    CHECK_STATUS(status, TSIR_OK);
    CHECK_STR(buf.data,
        "@group(0) @binding(0) var<storage, read> tsir_matrix: array<f32>;\n"
        "@group(0) @binding(1) var<storage, read> tsir_vector: array<f32>;\n"
        "@group(0) @binding(2) var<storage, read_write> tsir_output: array<f32>;\n"
        "\n"
        "@compute @workgroup_size(1, 1, 1)\n"
        "fn main(@builtin(global_invocation_id) gid: vec3<u32>) {\n"
        "    let row = gid.x;\n"
        "    let rows = arrayLength(&tsir_output);\n"
        "    let cols = arrayLength(&tsir_vector);\n"
        "    if (row >= rows) {\n"
        "        return;\n"
        "    }\n"
        "    var acc: f32 = 0.0;\n"
        "    for (var k = 0u; k < cols; k = k + 1u) {\n"
        "        acc = acc + tsir_matrix[row * cols + k] * tsir_vector[k];\n"
        "    }\n"
        "    tsir_output[row] = acc;\n"
        "}\n");
    tsir_buf_free(&buf);
    /* MSL fused_gemv, exact full text. */
    tsir_buf_init(&buf);
    status = tsir_emit_kernel_body(&kb_func, TSIR_KERNEL_MSL,
        &buf);
    CHECK_STATUS(status, TSIR_OK);
    CHECK_STR(buf.data,
        "#include <metal_stdlib>\nusing namespace metal;\n"
        "constant uint tsir_M [[function_constant(0)]];\n"
        "constant uint tsir_K [[function_constant(1)]];\n"
        "kernel void main0(const device float* tsir_matrix [[buffer(0)]], const device float* tsir_vector [[buffer(1)]], device float* tsir_output [[buffer(2)]], uint row [[thread_position_in_grid]]) {\n"
        "    if (row >= tsir_M) return;\n"
        "    float acc = 0.0f;\n"
        "    for (uint k = 0; k < tsir_K; k++) acc += tsir_matrix[row * tsir_K + k] * tsir_vector[k];\n"
        "    tsir_output[row] = acc;\n"
        "}\n");
    tsir_buf_free(&buf);
    /* WGSL gather, exact full text. */
    kb_setup(TSIR_BODY_GATHER, gather_roles);
    kb_binding(0, 0, 0, TSIR_SCALAR_U32);
    kb_binding(1, 0, 1, TSIR_SCALAR_F32);
    kb_binding(2, 0, 2, TSIR_SCALAR_F32);
    tsir_buf_init(&buf);
    status = tsir_emit_kernel_body(&kb_func, TSIR_KERNEL_WEBGPU,
        &buf);
    CHECK_STATUS(status, TSIR_OK);
    CHECK_STR(buf.data,
        "@group(0) @binding(0) var<storage, read> tsir_indices: array<u32>;\n"
        "@group(0) @binding(1) var<storage, read> tsir_table: array<f32>;\n"
        "@group(0) @binding(2) var<storage, read_write> tsir_output: array<f32>;\n"
        "\n"
        "@compute @workgroup_size(1, 1, 1)\n"
        "fn main(@builtin(global_invocation_id) gid: vec3<u32>) {\n"
        "    let h = gid.x;\n"
        "    let token = gid.y;\n"
        "    let tokens = arrayLength(&tsir_indices);\n"
        "    let hidden = arrayLength(&tsir_output) / tokens;\n"
        "    if (token >= tokens || h >= hidden) {\n"
        "        return;\n"
        "    }\n"
        "    let row = tsir_indices[token];\n"
        "    let vocab = arrayLength(&tsir_table) / hidden;\n"
        "    if (row >= vocab) {\n"
        "        tsir_output[token * hidden + h] = 0.0;\n"
        "        return;\n"
        "    }\n"
        "    tsir_output[token * hidden + h] = tsir_table[row * hidden + h];\n"
        "}\n");
    tsir_buf_free(&buf);
    /* SPIR-V gather: ternary with 0.0 (not 0.0f). */
    tsir_buf_init(&buf);
    status = tsir_emit_kernel_body(&kb_func, TSIR_KERNEL_SPIR_V,
        &buf);
    CHECK_STATUS(status, TSIR_OK);
    CHECK(buf.data != NULL &&
        strstr(buf.data,
            "tsir_output[dst] = (row >= tsir_vocab) ? 0.0 : tsir_table[row * tsir_hidden + h];") != NULL);
    tsir_buf_free(&buf);
    /* RMSNorm literal epsilon: WGSL {d} form, DXIL {d}f form. */
    kb_setup(TSIR_BODY_RMS_NORM, rms_roles);
    kb_binding(0, 0, 0, TSIR_SCALAR_F32);
    kb_binding(1, 0, 1, TSIR_SCALAR_F32);
    kb_binding(2, 0, 2, TSIR_SCALAR_F32);
    kb_func.body.has_rms_norm = true;
    kb_func.body.rms_norm.formula =
        TSIR_RMSNORM_SUM_SQUARES_MEAN_EPSILON_RSQRT_SCALE;
    kb_func.body.rms_norm.epsilon.source =
        TSIR_EPSILON_LITERAL_F32;
    kb_func.body.rms_norm.epsilon.has_literal_f64 = true;
    kb_func.body.rms_norm.epsilon.literal_f64 = 0.00001;
    kb_func.body.rms_norm.reduction_target =
        TSIR_RMSNORM_INTERMEDIATE_SCALAR;
    tsir_buf_init(&buf);
    status = tsir_emit_kernel_body(&kb_func, TSIR_KERNEL_WEBGPU,
        &buf);
    CHECK_STATUS(status, TSIR_OK);
    CHECK(buf.data != NULL &&
        strstr(buf.data, "inverseSqrt(mean_sq + 0.00001)") != NULL);
    CHECK(buf.data != NULL &&
        strstr(buf.data, "TsirRmsNormUniforms") == NULL);
    tsir_buf_free(&buf);
    tsir_buf_init(&buf);
    status = tsir_emit_kernel_body(&kb_func, TSIR_KERNEL_DXIL,
        &buf);
    CHECK_STATUS(status, TSIR_OK);
    CHECK(buf.data != NULL &&
        strstr(buf.data,
            "rsqrt((sum_sq / float(tsir_hidden_size)) + 0.00001f)") != NULL);
    tsir_buf_free(&buf);
    /* RMSNorm uniform epsilon: WGSL struct and tsir_dims line. */
    kb_func.binding_count = 4;
    kb_binding(3, 0, 3, TSIR_SCALAR_F32);
    kb_func.body.rms_norm.epsilon.source =
        TSIR_EPSILON_UNIFORM_FIELD;
    kb_func.body.rms_norm.epsilon.binding_index.has = true;
    kb_func.body.rms_norm.epsilon.binding_index.value = 3;
    kb_func.body.rms_norm.epsilon.byte_offset.has = true;
    kb_func.body.rms_norm.epsilon.byte_offset.value = 4;
    tsir_buf_init(&buf);
    status = tsir_emit_kernel_body(&kb_func, TSIR_KERNEL_WEBGPU,
        &buf);
    CHECK_STATUS(status, TSIR_OK);
    CHECK(buf.data != NULL &&
        strstr(buf.data, "struct TsirRmsNormUniforms {") != NULL);
    CHECK(buf.data != NULL &&
        strstr(buf.data,
            "@group(0) @binding(3) var<uniform> tsir_dims: TsirRmsNormUniforms;") != NULL);
    CHECK(buf.data != NULL &&
        strstr(buf.data, "let hidden_size = tsir_dims.hidden_size;") != NULL);
    CHECK(buf.data != NULL &&
        strstr(buf.data, "inverseSqrt(mean_sq + tsir_dims.epsilon)") != NULL);
    tsir_buf_free(&buf);
    /* Uniform epsilon with a bad byte offset is invalid. */
    kb_func.body.rms_norm.epsilon.byte_offset.value = 8;
    tsir_buf_init(&buf);
    status = tsir_emit_kernel_body(&kb_func, TSIR_KERNEL_WEBGPU,
        &buf);
    CHECK_STATUS(status, TSIR_ERR_INVALID);
    tsir_buf_free(&buf);
    /* CSL is quarantined; unknown ops are unsupported. */
    kb_setup(TSIR_BODY_FUSED_GEMV, gemv_roles);
    kb_binding(0, 0, 0, TSIR_SCALAR_F32);
    kb_binding(1, 0, 1, TSIR_SCALAR_F32);
    kb_binding(2, 0, 2, TSIR_SCALAR_F32);
    tsir_buf_init(&buf);
    status = tsir_emit_kernel_body(&kb_func, TSIR_KERNEL_CSL,
        &buf);
    CHECK_STATUS(status, TSIR_ERR_UNSUPPORTED);
    tsir_buf_free(&buf);
    kb_setup(TSIR_BODY_ATTENTION_SCORES, gemv_roles);
    tsir_buf_init(&buf);
    status = tsir_emit_kernel_body(&kb_func, TSIR_KERNEL_WEBGPU,
        &buf);
    CHECK_STATUS(status, TSIR_ERR_UNSUPPORTED);
    tsir_buf_free(&buf);
    /* Semantic-aware DXIL emission: header, metadata, body. */
    kb_setup(TSIR_BODY_FUSED_GEMV, gemv_roles);
    kb_binding(0, 0, 0, TSIR_SCALAR_F32);
    kb_binding(1, 0, 1, TSIR_SCALAR_F32);
    kb_binding(2, 0, 2, TSIR_SCALAR_F32);
    {
        TsirRealizationFunction rfunc = {0};
        TsirRealization realization;
        uint8_t hash[32];
        char *text = NULL;
        size_t len = 0;
        memset(hash, 0xab, 32);
        memset(&rfunc, 0, sizeof(rfunc));
        rfunc.semantic_index = 0;
        rfunc.emitter_params_json = "{}";
        memcpy(rfunc.target_descriptor_hash, hash, 32);
        memset(&realization, 0, sizeof(realization));
        realization.functions = &rfunc;
        realization.function_count = 1;
        status = tsir_emit_semantic(&kb_semantic, &realization, 0,
            "test-target", hash, &tsir_spec_dxil, &text, &len);
        CHECK_STATUS(status, TSIR_OK);
        CHECK(text != NULL &&
            strstr(text, "// doe.tsir.dxil_skeleton.version = 1\n") == text);
        CHECK(text != NULL &&
            strstr(text,
                "StructuredBuffer<float> tsir_matrix : register(t0);") != NULL);
        free(text);
    }
}

static void test_bootstrap(void) {
    TsirBuffer buf;
    TsirStatus status = TSIR_OK;
    TsirManifestEntry entry = {0};
    char digest[65];
    size_t i = 0;
    for (i = 0; i < 32; i++) {
        digest[2 * i] = 'a';
        digest[2 * i + 1] = 'b';
    }
    digest[64] = '\0';
    CHECK_STR(tsir_bootstrap_canonical_kernel(
        "doe.tsir.bootstrap.rms-norm"), "rms_norm");
    CHECK_STR(tsir_bootstrap_canonical_kernel("fused-gemv"),
        "fused_gemv");
    CHECK_STR(tsir_bootstrap_canonical_kernel("gather"), "gather");
    status = tsir_bootstrap_manifest_template("fused_gemv",
        "webgpu-generic", digest, digest, digest, digest, &entry);
    CHECK_STATUS(status, TSIR_OK);
    tsir_buf_init(&buf);
    status = tsir_bootstrap_write_manifest_entry(&buf, &entry);
    CHECK_STATUS(status, TSIR_OK);
    CHECK(buf.data != NULL &&
        strstr(buf.data, "\"backend\": \"webgpu-generic\"") != NULL);
    CHECK(buf.data != NULL &&
        strstr(buf.data,
            "\"compilerVersion\": \"doe-tsir-bootstrap-2026-04-24\"") != NULL);
    CHECK(buf.data != NULL &&
        strstr(buf.data, "\"exactnessClass\": \"algorithm_exact\"") != NULL);
    CHECK(buf.data != NULL &&
        strstr(buf.data,
            "\"algorithmExactInvariants\": [\"reduction_order\", \"accum_dtype\"]") != NULL);
    CHECK(buf.data != NULL &&
        strstr(buf.data,
            "\"kernelRef\": \"doe.tsir.bootstrap.fused_gemv\"") != NULL);
    CHECK(buf.data != NULL &&
        strstr(buf.data,
            "\"frontendVersion\": \"frontend-bootstrap-pipeline-v1\"") != NULL);
    tsir_buf_free(&buf);
    entry.emitter_digest = "xyz";
    tsir_buf_init(&buf);
    status = tsir_bootstrap_write_manifest_entry(&buf, &entry);
    CHECK_STATUS(status, TSIR_ERR_INVALID);
    tsir_buf_free(&buf);
    status = tsir_bootstrap_manifest_template("nope",
        "webgpu-generic", digest, digest, digest, digest, &entry);
    CHECK_STATUS(status, TSIR_ERR_UNSUPPORTED);
    /* Oracle: fused_gemv values path. */
    {
        const char *json =
            "{\"kernel\":\"fused_gemv\",\"inputs\":{"
            "\"W\":{\"elem\":\"f32\",\"shape\":[2,2],"
            "\"values\":[1,2,3,4]},"
            "\"x\":{\"elem\":\"f32\",\"shape\":[2],"
            "\"values\":[5,6]}}}";
        char *text = NULL;
        size_t len = 0;
        status = tsir_bootstrap_oracle_run("fused_gemv", json,
            strlen(json), &text, &len);
        CHECK_STATUS(status, TSIR_OK);
        CHECK(text != NULL &&
            strstr(text, "\"status\":\"pass\"") != NULL);
        CHECK(text != NULL &&
            strstr(text, "\"referenceHash\":null") == NULL);
        if (text != NULL) {
            uint8_t expected[8];
            uint8_t hash[32];
            char hex[65];
            put_f32(expected, 0, 17.0f);
            put_f32(expected, 1, 39.0f);
            tsir_sha256(expected, 8, hash);
            hex_digest(hash, hex);
            CHECK(strstr(text, hex) != NULL);
        }
        free(text);
    }
    /* Oracle: rms_norm bytesHex path. */
    {
        const char *json =
            "{\"kernel\":\"rms_norm\",\"inputs\":{"
            "\"input\":{\"elem\":\"f32\",\"shape\":[2],"
            "\"values\":[3,4]},"
            "\"weight\":{\"elem\":\"f32\",\"shape\":[2],"
            "\"values\":[1,1]},"
            "\"u\":{\"elem\":\"u32\",\"shape\":[2],"
            "\"bytesHex\":\"0200000000000000\"}}}";
        char *text = NULL;
        size_t len = 0;
        status = tsir_bootstrap_oracle_run("rms_norm", json,
            strlen(json), &text, &len);
        CHECK_STATUS(status, TSIR_OK);
        CHECK(text != NULL &&
            strstr(text, "\"status\":\"pass\"") != NULL);
        free(text);
    }
    /* Oracle: gather array-of-buffers path. */
    {
        const char *json =
            "{\"kernel\":\"gather\",\"inputs\":["
            "{\"name\":\"indices\",\"elem\":\"u32\",\"shape\":[2],"
            "\"values\":[1,0]},"
            "{\"name\":\"table\",\"elem\":\"f32\",\"shape\":[2,2],"
            "\"values\":[10,20,30,40]}]}";
        char *text = NULL;
        size_t len = 0;
        status = tsir_bootstrap_oracle_run("gather", json,
            strlen(json), &text, &len);
        CHECK_STATUS(status, TSIR_OK);
        CHECK(text != NULL &&
            strstr(text, "\"status\":\"pass\"") != NULL);
        free(text);
    }
    /* Oracle: mismatched kernel, malformed JSON, bad elem. */
    {
        const char *json =
            "{\"kernel\":\"gather\",\"inputs\":{}}";
        char *text = NULL;
        size_t len = 0;
        status = tsir_bootstrap_oracle_run("fused_gemv", json,
            strlen(json), &text, &len);
        CHECK_STATUS(status, TSIR_OK);
        CHECK(text != NULL &&
            strstr(text, "\"status\":\"not_implemented\"") != NULL);
        CHECK(text != NULL &&
            strstr(text, "\"referenceHash\":null") != NULL);
        CHECK(text != NULL &&
            strstr(text,
                "bootstrap oracle cannot execute this input") != NULL);
        free(text);
        text = NULL;
        status = tsir_bootstrap_oracle_run("fused_gemv", "{bad",
            4, &text, &len);
        CHECK_STATUS(status, TSIR_OK);
        CHECK(text != NULL &&
            strstr(text,
                "bootstrap oracle input JSON is unsupported or missing kernel/inputs") != NULL);
        free(text);
        text = NULL;
        {
            const char *bad_elem_json =
                "{\"kernel\":\"fused_gemv\",\"inputs\":{"
                "\"W\":{\"elem\":\"i32\",\"shape\":[1],"
                "\"values\":[1]},\"x\":{\"elem\":\"f32\",\"shape\":[1],"
                "\"values\":[1]}}}";
            status = tsir_bootstrap_oracle_run("fused_gemv",
                bad_elem_json, strlen(bad_elem_json), &text, &len);
        }
        CHECK_STATUS(status, TSIR_OK);
        CHECK(text != NULL &&
            strstr(text, "\"status\":\"not_implemented\"") != NULL);
        free(text);
    }
}

static void test_family_hint_graphs(void) {
    const uint32_t depth = 4096u;
    TsirIrExpr *expressions = calloc((size_t)depth + 6u, sizeof(*expressions));
    TsirIrLocal locals[2] = {{.name = "row"}, {.name = "k"}};
    TsirIterationAxis axes[2] = {{.name = "row", .upper_bound = "dispatch.x"}, {.name = "k"}};
    TsirReductionRegion reduction = {.axis = 1u, .op = TSIR_REDUCTION_SUM};
    TsirIrFunction function = {.exprs = expressions, .expr_count = (size_t)depth + 6u,
        .locals = locals, .local_count = 2u};
    TsirKernelFamilyHint hint = TSIR_HINT_UNKNOWN;
    CHECK(expressions != NULL);
    if (expressions == NULL) return;
    expressions[0].kind = TSIR_IR_EXPR_GLOBAL_REF;
    expressions[1] = (TsirIrExpr){.kind = TSIR_IR_EXPR_LOCAL_REF, .ref_index = 0};
    expressions[2] = (TsirIrExpr){.kind = TSIR_IR_EXPR_LOCAL_REF, .ref_index = 1};
    for (uint32_t i = 0; i < depth; i++) {
        uint32_t previous = i == 0 ? 1u : i + 2u;
        expressions[i + 3u] = (TsirIrExpr){.kind = TSIR_IR_EXPR_BINARY,
            .binary_lhs = previous, .binary_rhs = previous};
    }
    expressions[depth + 3u] = (TsirIrExpr){.kind = TSIR_IR_EXPR_BINARY,
        .binary_lhs = depth + 2u, .binary_rhs = 2u};
    expressions[depth + 4u] = (TsirIrExpr){.kind = TSIR_IR_EXPR_INDEX,
        .index_base = 0, .index_index = depth + 3u};
    expressions[depth + 5u] = (TsirIrExpr){.kind = TSIR_IR_EXPR_LOAD, .inner = depth + 4u};
    /* The inner-axis search must traverse a deep shared DAG with a false
     * branch. Recursive truncation loses GEMV; no memoization is exponential. */
    CHECK_STATUS(tsir_family_hint_infer_ir(&function, axes, 2u, &reduction, 1u, &hint), TSIR_OK);
    CHECK(hint == TSIR_HINT_FUSED_GEMV);
    expressions[depth + 3u].binary_rhs = 1u;
    CHECK_STATUS(tsir_family_hint_infer_ir(&function, axes, 2u, &reduction, 1u, &hint), TSIR_OK);
    CHECK(hint == TSIR_HINT_REDUCTION);
    free(expressions);

    TsirIrExpr invalid[3] = {
        {.kind = TSIR_IR_EXPR_GLOBAL_REF},
        {.kind = TSIR_IR_EXPR_LOAD, .inner = 1u},
        {.kind = TSIR_IR_EXPR_INDEX, .index_base = 0, .index_index = 1u}
    };
    function = (TsirIrFunction){.exprs = invalid, .expr_count = 3u};
    hint = TSIR_HINT_ROPE;
    CHECK_STATUS(tsir_family_hint_infer_ir(&function, axes, 1u, NULL, 0, &hint), TSIR_ERR_INVALID);
    CHECK(hint == TSIR_HINT_ROPE);
    uint32_t global = 99u;
    CHECK(!tsir_ir_find_global_base(&function, 1u, &global));
    CHECK(global == 99u);
    invalid[1] = (TsirIrExpr){.kind = TSIR_IR_EXPR_CALL, .args_len = 1u};
    CHECK_STATUS(tsir_family_hint_infer_ir(&function, axes, 1u, NULL, 0, &hint), TSIR_ERR_INVALID);
    CHECK(hint == TSIR_HINT_ROPE);
    invalid[1] = (TsirIrExpr){.kind = TSIR_IR_EXPR_LOAD, .inner = 3u};
    CHECK_STATUS(tsir_family_hint_infer_ir(&function, axes, 1u, NULL, 0, &hint), TSIR_ERR_INVALID);
    CHECK(hint == TSIR_HINT_ROPE);
}

static void test_family_hint_alias_limit(void) {
    TsirIterationAxis axis = {.name = "i"};
    for (uint32_t count = 8u; count <= 9u; count++) {
        TsirIrExpr expressions[13] = {0};
        TsirIrStmt statements[9] = {0};
        expressions[0].kind = TSIR_IR_EXPR_GLOBAL_REF;
        expressions[1].kind = TSIR_IR_EXPR_INT_LIT;
        expressions[2] = (TsirIrExpr){.kind = TSIR_IR_EXPR_INDEX, .index_base = 0, .index_index = 1u};
        for (uint32_t i = 0; i < count; i++) {
            expressions[i + 3u] = (TsirIrExpr){.kind = TSIR_IR_EXPR_LOCAL_REF, .ref_index = i};
            statements[i] = (TsirIrStmt){.kind = TSIR_IR_STMT_LOCAL_DECL, .local = i,
                .has_initializer = true, .initializer = i + 1u == count ? 2u : i + 4u};
        }
        expressions[count + 3u] = (TsirIrExpr){.kind = TSIR_IR_EXPR_INDEX,
            .index_base = 0, .index_index = 3u};
        TsirIrFunction function = {.exprs = expressions, .expr_count = count + 4u,
            .stmts = statements, .stmt_count = count};
        TsirKernelFamilyHint hint = TSIR_HINT_UNKNOWN;
        CHECK_STATUS(tsir_family_hint_infer_ir(&function, &axis, 1u, NULL, 0, &hint), TSIR_OK);
        CHECK(hint == (count == 8u ? TSIR_HINT_GATHER : TSIR_HINT_ELEMENTWISE));
    }
}

int main(void) {
    test_kernel_bodies();
    test_bootstrap();
    test_family_hint_graphs();
    test_family_hint_alias_limit();
    return finish_tests("test_new2");
}
