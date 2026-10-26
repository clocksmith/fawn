/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_common.h"

#include "tsir_scalar.h"
#include "tsir_sha256.h"

static void test_sha(void) {
    uint8_t out[32];
    char hex[65];
    TsirSha256 ctx;
    size_t i = 0;
    uint8_t chunk[1000];

    tsir_sha256(NULL, 0, out);
    hex_digest(out, hex);
    CHECK_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    tsir_sha256((const uint8_t *)"abc", 3, out);
    hex_digest(out, hex);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    memset(chunk, 'a', sizeof(chunk));
    tsir_sha256_init(&ctx);
    for (i = 0; i < 1000; i++) {
        tsir_sha256_update(&ctx, chunk, sizeof(chunk));
    }
    tsir_sha256_final(&ctx, out);
    hex_digest(out, hex);
    CHECK_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    /* Streaming split at odd boundaries equals single-shot. */
    {
        uint8_t single[32];
        uint8_t split[32];
        TsirSha256 c2;
        tsir_sha256((const uint8_t *)"abcdefghij", 10, single);
        tsir_sha256_init(&c2);
        tsir_sha256_update(&c2, (const uint8_t *)"abc", 3);
        tsir_sha256_update(&c2, (const uint8_t *)"defghij", 7);
        tsir_sha256_final(&c2, split);
        CHECK(memcmp(single, split, 32) == 0);
    }
}

static void test_scalar_names_and_sizes(void) {
    CHECK(tsir_scalar_byte_size(TSIR_SCALAR_F32) == 4);
    CHECK(tsir_scalar_byte_size(TSIR_SCALAR_F16) == 2);
    CHECK(tsir_scalar_byte_size(TSIR_SCALAR_BF16) == 2);
    CHECK(tsir_scalar_byte_size(TSIR_SCALAR_I32) == 4);
    CHECK(tsir_scalar_byte_size(TSIR_SCALAR_U32) == 4);
    CHECK_STR(tsir_scalar_name(TSIR_SCALAR_F32), "f32");
    CHECK_STR(tsir_scalar_name(TSIR_SCALAR_BF16), "bf16");
    CHECK(tsir_scalar_name((TsirScalarKind)99) == NULL);
    CHECK_STR(tsir_rejection_reason_name(TSIR_REJECT_PE_BUDGET_EXHAUSTED),
        "tsir_pe_budget_exhausted");
    CHECK_STR(tsir_body_op_name(TSIR_BODY_LINEAR_ATTENTION),
        "linear_attention");
    CHECK_STR(tsir_binding_role_name(TSIR_ROLE_SUMMAND_B), "summand_b");
    CHECK_STR(tsir_axis_role_name(TSIR_AXIS_KEY_SEQUENCE), "key_sequence");
    CHECK_STR(tsir_tree_shape_name(TSIR_TREE_BINOMIAL), "binomial");
    CHECK_STR(tsir_exactness_class_name(TSIR_EXACTNESS_TOLERANCE_BOUNDED),
        "tolerance_bounded");
    CHECK_STR(tsir_family_hint_name(TSIR_HINT_PREFILL_Q4K_GEMV),
        "prefill_q4k_gemv");
    CHECK_STR(tsir_collective_kind_name(TSIR_COLLECTIVE_FABRIC_ALLREDUCE),
        "fabric_allreduce");
    CHECK_STR(tsir_status_name(TSIR_ERR_UNSUPPORTED), "unsupported");
    CHECK(tsir_status_name((TsirStatus)99) == NULL);
}

static void test_exactness_well_formed(void) {
    TsirExactness e;
    TsirAlgorithmExactInvariant inv = TSIR_INVARIANT_REDUCTION_ORDER;
    memset(&e, 0, sizeof(e));
    e.class = TSIR_EXACTNESS_BIT_EXACT_SOLO;
    e.tolerance_metric = "";
    e.tolerance_epsilon = 0.0;
    CHECK(tsir_exactness_is_well_formed(&e));
    e.class = TSIR_EXACTNESS_ALGORITHM_EXACT;
    e.invariants = &inv;
    e.invariant_count = 1;
    CHECK(tsir_exactness_is_well_formed(&e));
    e.tolerance_metric = "ulp";
    CHECK(!tsir_exactness_is_well_formed(&e));
    e.tolerance_metric = "";
    e.class = TSIR_EXACTNESS_TOLERANCE_BOUNDED;
    e.invariant_count = 0;
    e.tolerance_metric = "max_abs";
    e.tolerance_epsilon = 0.5;
    CHECK(tsir_exactness_is_well_formed(&e));
    e.tolerance_metric = "";
    CHECK(!tsir_exactness_is_well_formed(&e));
    e.tolerance_metric = "max_abs";
    e.tolerance_epsilon = NAN;
    CHECK(!tsir_exactness_is_well_formed(&e));
    CHECK(!tsir_exactness_is_well_formed(NULL));
}

static void test_f16(void) {
    CHECK(tsir_f32_to_f16_bits(1.0f) == 0x3c00u);
    CHECK(tsir_f32_to_f16_bits(-1.0f) == 0xbc00u);
    CHECK(tsir_f32_to_f16_bits(0.5f) == 0x3800u);
    CHECK(tsir_f32_to_f16_bits(65504.0f) == 0x7bffu);
    CHECK(tsir_f32_to_f16_bits(65520.0f) == 0x7c00u);
    /* Midpoint between 1.0 (0x3c00) and next f16 (0x3c01): ties to
     * even, so 0x3c00. */
    CHECK(tsir_f32_to_f16_bits(1.00048828125f) == 0x3c00u);
    CHECK(tsir_f16_bits_to_f32(0x3c00u) == 1.0f);
    CHECK(tsir_f16_bits_to_f32(0xbc00u) == -1.0f);
    CHECK(tsir_f16_bits_to_f32(0x0000u) == 0.0f);
    CHECK(isinf(tsir_f16_bits_to_f32(0x7c00u)));
    CHECK(isnan(tsir_f16_bits_to_f32(0x7e00u)));
    /* Smallest subnormal f16, 2^-24. */
    CHECK(tsir_f16_bits_to_f32(0x0001u) == 0.000000059604644775390625f);
    CHECK(tsir_f32_to_f16_bits(0.000000059604644775390625f) == 0x0001u);
}

static void test_bf16(void) {
    CHECK(tsir_f32_to_bf16_rne(1.0f) == 0x3f80u);
    /* bf16 step near 1.0 is 2^-7: 1.00390625 is the tie between 1.0
     * and 1.0078125 and rounds to even (1.0); 1.01171875 is the tie
     * between 1.0078125 (odd) and 1.015625 (even) and rounds up. */
    CHECK(tsir_f32_to_bf16_rne(1.00390625f) == 0x3f80u);
    CHECK(tsir_f32_to_bf16_rne(1.0078125f) == 0x3f81u);
    CHECK(tsir_f32_to_bf16_rne(1.01171875f) == 0x3f82u);
    CHECK(tsir_bf16_to_f32(0x3f80u) == 1.0f);
    {
        uint16_t nan_bits = tsir_f32_to_bf16_rne(NAN);
        CHECK((nan_bits & 0x7f80u) == 0x7f80u);
        CHECK((nan_bits & 0x0040u) != 0);
    }
}

static void test_scalar_io(void) {
    uint8_t buf[64];
    float value = 0.0f;
    TsirStatus st = TSIR_OK;
    memset(buf, 0, sizeof(buf));
    put_f32(buf, 0, 1.5f);
    put_f32(buf, 1, -2.25f);
    CHECK(get_f32(buf, 0) == 1.5f);
    st = tsir_read_f32(buf, sizeof(buf), TSIR_SCALAR_F32, 1, &value);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(value == -2.25f);
    st = tsir_write_f32(buf, sizeof(buf), TSIR_SCALAR_F16, 2, 1.0f);
    CHECK_STATUS(st, TSIR_OK);
    st = tsir_read_f32(buf, sizeof(buf), TSIR_SCALAR_F16, 2, &value);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(value == 1.0f);
    st = tsir_write_f32(buf, sizeof(buf), TSIR_SCALAR_BF16, 3, 1.0f);
    CHECK_STATUS(st, TSIR_OK);
    st = tsir_read_f32(buf, sizeof(buf), TSIR_SCALAR_BF16, 3, &value);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(value == 1.0f);
    st = tsir_read_f32(buf, sizeof(buf), TSIR_SCALAR_I32, 0, &value);
    CHECK_STATUS(st, TSIR_ERR_UNSUPPORTED);
    st = tsir_write_f32(buf, sizeof(buf), TSIR_SCALAR_U32, 0, 1.0f);
    CHECK_STATUS(st, TSIR_ERR_UNSUPPORTED);
    st = tsir_read_f32(buf, 4, TSIR_SCALAR_F32, 1, &value);
    CHECK_STATUS(st, TSIR_ERR_RANGE);
}

static void test_expected_bytes(void) {
    uint64_t shape1[2] = { 2, 3 };
    uint64_t shape0[2] = { 0, 5 };
    uint64_t shape_big[2] = { 4294967296ull, 4294967296ull };
    TsirBufferBinding binding;
    uint64_t out = 0;
    TsirStatus st = TSIR_OK;
    memset(&binding, 0, sizeof(binding));
    binding.elem = TSIR_SCALAR_F32;
    binding.logical_shape = shape1;
    binding.logical_shape_len = 2;
    st = tsir_expected_bytes(&binding, &out);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(out == 24);
    binding.logical_shape = shape0;
    st = tsir_expected_bytes(&binding, &out);
    CHECK_STATUS(st, TSIR_OK);
    CHECK(out == 0);
    binding.logical_shape = shape_big;
    st = tsir_expected_bytes(&binding, &out);
    CHECK_STATUS(st, TSIR_ERR_OVERFLOW);
}

static void test_reduction_helpers(void) {
    float out = 0.0f;
    CHECK_STATUS(tsir_reduction_identity_f32(TSIR_REDUCTION_SUM, &out),
        TSIR_OK);
    CHECK(out == 0.0f);
    CHECK_STATUS(tsir_reduction_identity_f32(TSIR_REDUCTION_PRODUCT, &out),
        TSIR_OK);
    CHECK(out == 1.0f);
    CHECK_STATUS(tsir_reduction_identity_f32(TSIR_REDUCTION_MIN, &out),
        TSIR_OK);
    CHECK(isinf(out) && out > 0);
    CHECK_STATUS(tsir_reduction_identity_f32(TSIR_REDUCTION_MAX, &out),
        TSIR_OK);
    CHECK(isinf(out) && out < 0);
    CHECK_STATUS(
        tsir_reduction_identity_f32(TSIR_REDUCTION_SOFTMAX_STABLE, &out),
        TSIR_ERR_INVALID);
    CHECK_STATUS(tsir_combine_f32(TSIR_REDUCTION_SUM, 2.0f, 3.0f, &out),
        TSIR_OK);
    CHECK(out == 5.0f);
    CHECK_STATUS(tsir_combine_f32(TSIR_REDUCTION_MIN, NAN, 1.0f, &out),
        TSIR_OK);
    CHECK(isnan(out));
    CHECK_STATUS(tsir_combine_f32(TSIR_REDUCTION_MAX, 2.0f, NAN, &out),
        TSIR_OK);
    CHECK(isnan(out));
}

int main(void) {
    test_sha();
    test_scalar_names_and_sizes();
    test_exactness_well_formed();
    test_f16();
    test_bf16();
    test_scalar_io();
    test_expected_bytes();
    test_reduction_helpers();
    return finish_tests("test_sha_scalar");
}
