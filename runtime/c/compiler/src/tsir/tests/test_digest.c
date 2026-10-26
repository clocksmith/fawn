/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_common.h"

#include "tsir_digest.h"
#include "tsir_sha256.h"

static void test_empty_semantic(void) {
    TsirSemantic semantic;
    char *text = NULL;
    size_t len = 0;
    TsirStatus st = TSIR_OK;
    memset(&semantic, 0, sizeof(semantic));
    semantic.contract_version = 1;
    semantic.frontend_version = "";
    st = tsir_canonicalize_semantic(&semantic, &text, &len);
    CHECK_STATUS(st, TSIR_OK);
    CHECK_STR(text,
        "{\"contractVersion\":1,\"frontendVersion\":\"\",\"functions\":[],\"rejections\":[]}");
    free(text);
}

static void test_one_function_exact(void) {
    TsirIterationAxis axis;
    TsirBufferBinding binding;
    TsirSemanticBody body;
    TsirSemanticFunction func;
    TsirSemantic semantic;
    uint64_t shape[1] = { 4 };
    char *text = NULL;
    size_t len = 0;
    TsirStatus st = TSIR_OK;
    char zeros[65];
    char want[2048];
    memset(&axis, 0, sizeof(axis));
    axis.name = "i";
    axis.lower_bound = "0";
    axis.upper_bound = "4";
    axis.step = "1";
    memset(&binding, 0, sizeof(binding));
    binding.name = "x";
    binding.group = 0;
    binding.binding = 0;
    binding.logical_shape = shape;
    binding.logical_shape_len = 1;
    binding.elem = TSIR_SCALAR_F32;
    binding.read_write = false;
    memset(&body, 0, sizeof(body));
    body.op = TSIR_BODY_UNKNOWN;
    memset(&func, 0, sizeof(func));
    func.name = "k";
    func.family_hint = TSIR_HINT_ELEMENTWISE;
    func.axes = &axis;
    func.axis_count = 1;
    func.bindings = &binding;
    func.binding_count = 1;
    func.body = body;
    memset(&semantic, 0, sizeof(semantic));
    semantic.contract_version = 1;
    semantic.frontend_version = "";
    semantic.functions = &func;
    semantic.function_count = 1;
    memset(zeros, '0', 64);
    zeros[64] = '\0';
    snprintf(want, sizeof(want),
        "{\"contractVersion\":1,\"frontendVersion\":\"\",\"functions\":["
        "{\"axes\":[{\"lowerBound\":\"0\",\"name\":\"i\",\"step\":\"1\",\"upperBound\":\"4\"}],"
        "\"bindings\":[{\"binding\":0,\"elem\":\"f32\",\"group\":0,\"logicalShape\":[4],\"name\":\"x\",\"readWrite\":false}],"
        "\"body\":{\"axisRoles\":[],\"bindingRoles\":[],\"op\":\"unknown\"},"
        "\"collectives\":[],\"familyHint\":\"elementwise\",\"name\":\"k\",\"reductions\":[],"
        "\"sourceDigest\":\"%s\"}],\"rejections\":[]}",
        zeros);
    st = tsir_canonicalize_semantic(&semantic, &text, &len);
    CHECK_STATUS(st, TSIR_OK);
    CHECK_STR(text, want);
    free(text);
}

static void test_escaping(void) {
    TsirBufferBinding binding;
    TsirSemanticFunction func;
    TsirSemantic semantic;
    char *text = NULL;
    size_t len = 0;
    TsirStatus st = TSIR_OK;
    memset(&binding, 0, sizeof(binding));
    binding.name = "a\"b\\c\nd\te\x01" "f";
    binding.elem = TSIR_SCALAR_F32;
    memset(&func, 0, sizeof(func));
    func.name = "k";
    func.bindings = &binding;
    func.binding_count = 1;
    memset(&semantic, 0, sizeof(semantic));
    semantic.contract_version = 1;
    semantic.frontend_version = "";
    semantic.functions = &func;
    semantic.function_count = 1;
    st = tsir_canonicalize_semantic(&semantic, &text, &len);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(text != NULL &&
        strstr(text, "\"name\":\"a\\\"b\\\\c\\nd\\te\\u0001f\"") != NULL);
    free(text);
}

static void test_optional_and_nan(void) {
    TsirResidencyDecision decision;
    TsirRealizationFunction func;
    TsirRealization realization;
    char *text = NULL;
    size_t len = 0;
    TsirStatus st = TSIR_OK;
    memset(&decision, 0, sizeof(decision));
    decision.binding_index = 0;
    decision.class = TSIR_RESIDENCY_PE_REPLICATED;
    memset(&func, 0, sizeof(func));
    func.semantic_index = 0;
    func.residency = &decision;
    func.residency_count = 1;
    func.emitter_params_json = "{}";
    memset(&realization, 0, sizeof(realization));
    realization.contract_version = 1;
    realization.functions = &func;
    realization.function_count = 1;
    st = tsir_canonicalize_realization(&realization, &text, &len);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(text != NULL &&
        strstr(text,
            "\"axis\":null,\"bindingIndex\":0,\"chunkBytes\":null,\"class\":\"pe_replicated\",\"fabricColor\":null,\"shards\":null") != NULL);
    free(text);
    text = NULL;
    /* NaN tolerance epsilon is invalid, never serialized. */
    {
        TsirCollectiveSemanticNode node;
        TsirSemanticFunction sfunc;
        TsirSemantic semantic2;
        memset(&node, 0, sizeof(node));
        node.kind = TSIR_COLLECTIVE_SUBGROUP_ADD;
        node.axis = -1;
        node.exactness.class = TSIR_EXACTNESS_TOLERANCE_BOUNDED;
        node.exactness.tolerance_metric = "max_abs";
        node.exactness.tolerance_epsilon = NAN;
        node.dtype = TSIR_SCALAR_F32;
        memset(&sfunc, 0, sizeof(sfunc));
        sfunc.name = "k";
        sfunc.collectives = &node;
        sfunc.collective_count = 1;
        memset(&semantic2, 0, sizeof(semantic2));
        semantic2.contract_version = 1;
        semantic2.frontend_version = "";
        semantic2.functions = &sfunc;
        semantic2.function_count = 1;
        st = tsir_canonicalize_semantic(&semantic2, &text, &len);
        CHECK_STATUS(st, TSIR_ERR_INVALID);
        CHECK(text == NULL);
    }
}

static void test_f64_plain_and_determinism(void) {
    TsirManifestLoweringEntry entry;
    char *text1 = NULL;
    char *text2 = NULL;
    size_t len1 = 0;
    size_t len2 = 0;
    TsirStatus st = TSIR_OK;
    uint8_t digest1[32];
    uint8_t digest2[32];
    memset(&entry, 0, sizeof(entry));
    entry.kernel_ref = "k";
    entry.backend = "webgpu-generic";
    entry.frontend_version = "v1";
    entry.compiler_version = "doe-test";
    entry.exactness.class = TSIR_EXACTNESS_TOLERANCE_BOUNDED;
    entry.exactness.tolerance_metric = "max_abs";
    entry.exactness.tolerance_epsilon = 1.5;
    st = tsir_canonicalize_manifest_entry(&entry, &text1, &len1);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(text1 != NULL && strstr(text1, "\"toleranceEpsilon\":1.5") != NULL);
    CHECK(text1 != NULL &&
        strstr(text1, "\"tsirSemanticDigest\":\"") != NULL);
    st = tsir_manifest_entry_digest(&entry, digest1);
    CHECK_STATUS(st, TSIR_OK);
    entry.exactness.tolerance_epsilon = 0.000001;
    st = tsir_canonicalize_manifest_entry(&entry, &text2, &len2);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(text2 != NULL &&
        strstr(text2, "\"toleranceEpsilon\":0.000001") != NULL);
    st = tsir_manifest_entry_digest(&entry, digest2);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(memcmp(digest1, digest2, 32) != 0);
    free(text1);
    free(text2);
}

static void test_split_digests(void) {
    TsirSemantic semantic;
    TsirSemantic semantic2;
    TsirRealization realization;
    TsirRealizationFunction rfunc;
    TsirDigests digests1;
    TsirDigests digests2;
    TsirStatus st = TSIR_OK;
    uint8_t emitter[32];
    memset(&semantic, 0, sizeof(semantic));
    semantic.contract_version = 1;
    semantic.frontend_version = "";
    semantic2 = semantic;
    memset(&rfunc, 0, sizeof(rfunc));
    rfunc.emitter_params_json = "{}";
    memset(&realization, 0, sizeof(realization));
    realization.contract_version = 1;
    realization.functions = &rfunc;
    realization.function_count = 1;
    memset(emitter, 0, 32);
    st = tsir_digests_compute(&semantic, &realization, emitter, &digests1);
    CHECK_STATUS(st, TSIR_OK);
    /* Changing only emitter params changes realization, not semantic. */
    rfunc.emitter_params_json = "{\"planner\":\"x\"}";
    st = tsir_digests_compute(&semantic2, &realization, emitter, &digests2);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(memcmp(digests1.semantic, digests2.semantic, 32) == 0);
    CHECK(memcmp(digests1.realization, digests2.realization, 32) != 0);
    CHECK(memcmp(digests1.emitter, emitter, 32) == 0);
    /* Version form hashes the version string for the emitter slot. */
    {
        TsirDigests digests3;
        uint8_t want[32];
        st = tsir_digests_compute_version(&semantic, &realization, "v1",
            &digests3);
        CHECK_STATUS(st, TSIR_OK);
        tsir_sha256((const uint8_t *)"v1", 2, want);
        CHECK(memcmp(digests3.emitter, want, 32) == 0);
    }
}

static void test_digest_drops_other_bodies(void) {
    TsirSemanticBody body;
    TsirSemanticFunction func;
    TsirSemantic semantic;
    char *text = NULL;
    size_t len = 0;
    TsirStatus st = TSIR_OK;
    memset(&body, 0, sizeof(body));
    body.op = TSIR_BODY_RMS_NORM;
    body.has_rms_norm = true;
    body.rms_norm.formula =
        TSIR_RMSNORM_SUM_SQUARES_MEAN_EPSILON_RSQRT_SCALE;
    body.rms_norm.epsilon.source = TSIR_EPSILON_LITERAL_F32;
    body.rms_norm.epsilon.path = "";
    body.rms_norm.epsilon.has_literal_f64 = true;
    body.rms_norm.epsilon.literal_f64 = 0.5;
    body.rms_norm.hidden_extent_axis = 0;
    body.rms_norm.reduction_target = TSIR_RMSNORM_INTERMEDIATE_SCALAR;
    body.has_attention_scores = true;
    body.attention_scores.head_dim = 64;
    memset(&func, 0, sizeof(func));
    func.name = "k";
    func.body = body;
    memset(&semantic, 0, sizeof(semantic));
    semantic.contract_version = 1;
    semantic.frontend_version = "";
    semantic.functions = &func;
    semantic.function_count = 1;
    st = tsir_canonicalize_semantic(&semantic, &text, &len);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(text != NULL && strstr(text, "\"rmsNorm\"") != NULL);
    CHECK(text != NULL && strstr(text, "attention") == NULL);
    CHECK(text != NULL &&
        strstr(text, "\"literalF32\":0.5") != NULL);
    free(text);
}

int main(void) {
    test_empty_semantic();
    test_one_function_exact();
    test_escaping();
    test_optional_and_nan();
    test_f64_plain_and_determinism();
    test_split_digests();
    test_digest_drops_other_bodies();
    return finish_tests("test_digest");
}
