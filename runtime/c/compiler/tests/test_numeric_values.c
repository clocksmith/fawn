/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _GNU_SOURCE
#include "../src/numeric.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>

/* Focused private-owner test: link numeric.c and this file, not compile.c.
 * Public pipeline/diagnostic publication is verified by separate adapter tests. */
bool source_error(Source *source, DoeCompilerStatus status, size_t offset, const char *message) {
    source->diagnostic->status = status;
    source->diagnostic->offset = offset;
    (void)snprintf(source->diagnostic->message, sizeof(source->diagnostic->message), "%s", message);
    return false;
}
static Source test_source(const char *text, DoeCompilerDiagnostic *diagnostic) {
    *diagnostic = (DoeCompilerDiagnostic){ 0 };
    return (Source){ .source = text, .length = strlen(text), .diagnostic = diagnostic };
}
static NumericValue value(IrType type, uint64_t bits) {
    return (NumericValue){ .bits = bits, .type = type };
}
static NumericValue literal(const char *text) {
    DoeCompilerDiagnostic diagnostic;
    Source source = test_source(text, &diagnostic);
    NumericValue result;
    if (!numeric_parse_literal(&source, (NameSpan){ .length = (uint32_t)source.length }, &result)) {
        (void)fprintf(stderr, "literal %s: %s\n", text, diagnostic.message);
        abort();
    }
    return result;
}
static void check_literal(const char *text, IrType type, uint64_t bits) {
    const NumericValue result = literal(text);
    if (result.type != type || result.bits != bits) {
        (void)fprintf(stderr, "literal %s: type %d bits %016llx expected %016llx\n", text,
                      (int)result.type, (unsigned long long)result.bits, (unsigned long long)bits);
        abort();
    }
}
static void reject_literal(const char *text, DoeCompilerStatus status) {
    DoeCompilerDiagnostic diagnostic;
    Source source = test_source(text, &diagnostic);
    NumericValue result = value(IR_BOOL, 99u);
    assert(!numeric_parse_literal(&source, (NameSpan){ .length = (uint32_t)source.length }, &result));
    assert(diagnostic.status == status);
    assert(result.type == IR_BOOL && result.bits == 99u);
}
static void test_literals(void) {
    check_literal("0", IR_ABSTRACT_INT, 0u);
    check_literal("0i", IR_I32, 0u);
    check_literal("2147483647i", IR_I32, UINT64_C(0x7fffffff));
    check_literal("4294967295u", IR_U32, UINT64_C(0xffffffff));
    check_literal("9223372036854775807", IR_ABSTRACT_INT, UINT64_C(0x7fffffffffffffff));
    check_literal("0X7FFFFFFFFFFFFFFF", IR_ABSTRACT_INT, UINT64_C(0x7fffffffffffffff));
    check_literal("0xffu", IR_U32, 255u);
    check_literal("0x1f", IR_ABSTRACT_INT, 31u);
    check_literal("0x00000000000000000000000000000000000000000000001", IR_ABSTRACT_INT, 1u);
    check_literal("true", IR_BOOL, 1u);
    check_literal("false", IR_BOOL, 0u);
    check_literal(".5", IR_ABSTRACT_FLOAT, UINT64_C(0x3fe0000000000000));
    check_literal("1.", IR_ABSTRACT_FLOAT, UINT64_C(0x3ff0000000000000));
    check_literal("00.5f", IR_F32, UINT64_C(0x3f000000));
    check_literal("01e0", IR_ABSTRACT_FLOAT, UINT64_C(0x3ff0000000000000));
}
static void test_float_literals(void) {
    check_literal("1E+2f", IR_F32, UINT64_C(0x42c80000));
    check_literal("42f", IR_F32, UINT64_C(0x42280000));
    check_literal("0e999999999999999999999999999999", IR_ABSTRACT_FLOAT, 0u);
    check_literal("0X.8", IR_ABSTRACT_FLOAT, UINT64_C(0x3fe0000000000000));
    check_literal("0x1.fp0", IR_ABSTRACT_FLOAT, UINT64_C(0x3fff000000000000));
    check_literal("0x1.fp0f", IR_F32, UINT64_C(0x3ff80000));
    check_literal("0x1.0000000001p0", IR_ABSTRACT_FLOAT, UINT64_C(0x3ff0000000001000));
    check_literal("0x1p-149f", IR_F32, 1u);
    check_literal("0x1p-126f", IR_F32, UINT64_C(0x00800000));
    check_literal("0x1.fffffep127f", IR_F32, UINT64_C(0x7f7fffff));
    check_literal("0x1p-1074", IR_ABSTRACT_FLOAT, 1u);
    check_literal("0x1p-1075", IR_ABSTRACT_FLOAT, 0u);
    check_literal("0x1.8p-1074", IR_ABSTRACT_FLOAT, 2u);
    check_literal("0x1.fffffffffffffp1023", IR_ABSTRACT_FLOAT, UINT64_C(0x7fefffffffffffff));
    /* Decimal values just above MAX still round to its finite encoding. */
    check_literal("3.4028234663852886e38f", IR_F32, UINT64_C(0x7f7fffff));
    check_literal("3.4028234663852885982e38f", IR_F32, UINT64_C(0x7f7fffff));
    check_literal("3.4028235677973366163e38f", IR_F32, UINT64_C(0x7f7fffff));
    check_literal("1.7976931348623158e308", IR_ABSTRACT_FLOAT, UINT64_C(0x7fefffffffffffff));
    check_literal("1.7976931348623158079e308", IR_ABSTRACT_FLOAT, UINT64_C(0x7fefffffffffffff));
    check_literal("0x1.fffffffffffff7p1023", IR_ABSTRACT_FLOAT, UINT64_C(0x7fefffffffffffff));
    check_literal("1.000000059604644775390625f", IR_F32, UINT64_C(0x3f800000));
    check_literal("1.0000000596046447754f", IR_F32, UINT64_C(0x3f800001));
    check_literal("1.000000178813934326171875f", IR_F32, UINT64_C(0x3f800001));
    check_literal("1.0000001788139343262f", IR_F32, UINT64_C(0x3f800002));
    check_literal("1.40129846432481707092e-45f", IR_F32, 1u);
    check_literal("7.0064923216240853546e-46f", IR_F32, 0u);
    check_literal("7.006492321624085355e-46f", IR_F32, 1u);
    check_literal("4.94065645841246544176e-324", IR_ABSTRACT_FLOAT, 1u);
    check_literal("1e-400", IR_ABSTRACT_FLOAT, 0u);
    check_literal("1e-999999999999999999999999999999999", IR_ABSTRACT_FLOAT, 0u);
    check_literal("0x1p-999999999999999999999999999999999", IR_ABSTRACT_FLOAT, 0u);
    check_literal("0x1.000000000000000fffffffffffffp0", IR_ABSTRACT_FLOAT, UINT64_C(0x3ff0000000000000));
    reject_literal("2147483648i", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("4294967296u", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("9223372036854775808", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("184467440737095516160000000000000000000", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("0xffffffffffffffff", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("0x1.000001p0f", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("0x1p-150f", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("0x1p-999999999999999999999999999999999f", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("0x1p128f", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("0x1p1024", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("1e999999999999999999999999999999999999", DOE_COMPILER_SEMANTIC_ERROR);
    /* Nearest-even rounding reaches infinity at the overflow midpoint. */
    reject_literal("3.4028235677973366164e38f", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("1.7976931348623158080e308", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("0x1.fffffffffffff8p1023", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("0x1.fffffffffffff9p1023", DOE_COMPILER_SEMANTIC_ERROR);
    /* Concrete hexadecimal literals must still be exactly representable. */
    reject_literal("0x1.fffffe1p127f", DOE_COMPILER_SEMANTIC_ERROR);
    reject_literal("1h", DOE_COMPILER_UNSUPPORTED);
    reject_literal("0x1p0h", DOE_COMPILER_UNSUPPORTED);
    const char *const malformed[] = {
        "00", "01u", "00f", ".", ".f", "0x", "0x.p1", "0x1p", "0x1p+", "0x1.0h",
        "0x1p0i", "1.0u", "1e", "1e-", "1E+2F", "1ff", "1ii", "0x12g", "+1", "-1", "1_0"
    };
    for (size_t i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i)
        reject_literal(malformed[i], DOE_COMPILER_PARSE_ERROR);
    const size_t zeros = 100000u;
    char *long_literal = malloc(zeros + 40u);
    assert(long_literal != NULL);
    long_literal[0] = '1';
    memset(long_literal + 1u, '0', zeros);
    (void)snprintf(long_literal + zeros + 1u, 31u, "e-%zu", zeros);
    check_literal(long_literal, IR_ABSTRACT_FLOAT, UINT64_C(0x3ff0000000000000));
    long_literal[0] = '.';
    memset(long_literal + 1u, '0', zeros);
    (void)snprintf(long_literal + zeros + 1u, 31u, "1e%zu", zeros + 1u);
    check_literal(long_literal, IR_ABSTRACT_FLOAT, UINT64_C(0x3ff0000000000000));
    memcpy(long_literal, "0x1", 3u);
    memset(long_literal + 3u, '0', zeros);
    (void)snprintf(long_literal + zeros + 3u, 31u, "p-%zu", zeros * 4u);
    check_literal(long_literal, IR_ABSTRACT_FLOAT, UINT64_C(0x3ff0000000000000));
    free(long_literal);
    DoeCompilerDiagnostic diagnostic;
    Source source = test_source("__1.25f__", &diagnostic);
    NumericValue result;
    assert(numeric_parse_literal(&source, (NameSpan){ .offset = 2u, .length = 5u }, &result));
    assert(result.bits == UINT64_C(0x3fa00000));
    assert(!numeric_parse_literal(&source, (NameSpan){ .offset = 10u, .length = 1u }, &result));
    assert(diagnostic.status == DOE_COMPILER_INPUT_ERROR);
}
static NumericValue binary(ExpressionKind operation, NumericValue left, NumericValue right) {
    DoeCompilerDiagnostic diagnostic;
    Source source = test_source("operation", &diagnostic);
    NumericValue result;
    if (!numeric_binary(&source, 0u, operation, COMPARE_EQUAL, left, right, &result)) {
        (void)fprintf(stderr, "binary op %d: %s\n", (int)operation, diagnostic.message);
        abort();
    }
    return result;
}
static void reject_binary(ExpressionKind operation, NumericValue left, NumericValue right) {
    DoeCompilerDiagnostic diagnostic;
    Source source = test_source("operation", &diagnostic);
    NumericValue result = value(IR_BOOL, 99u);
    assert(!numeric_binary(&source, 3u, operation, COMPARE_EQUAL, left, right, &result));
    assert(diagnostic.status == DOE_COMPILER_SEMANTIC_ERROR && diagnostic.offset == 3u);
    assert(result.type == IR_BOOL && result.bits == 99u);
}
static void test_operations(void) {
    const NumericValue i_min = value(IR_I32, UINT64_C(0x80000000));
    const NumericValue ai_min = value(IR_ABSTRACT_INT, UINT64_C(0x8000000000000000));
    const NumericValue ai_max = value(IR_ABSTRACT_INT, UINT64_C(0x7fffffffffffffff));
    const NumericValue i_negative_one = value(IR_I32, UINT64_C(0xffffffff));
    const NumericValue ai_negative_one = value(IR_ABSTRACT_INT, UINT64_MAX);
    DoeCompilerDiagnostic diagnostic;
    Source source = test_source("operations", &diagnostic);
    NumericValue result;
    assert(numeric_unary(&source, 0u, EXPRESSION_NEGATE, i_min, &result) && result.bits == i_min.bits);
    assert(numeric_unary(&source, 0u, EXPRESSION_NEGATE, ai_min, &result) && result.bits == ai_min.bits);
    assert(numeric_unary(&source, 0u, EXPRESSION_NEGATE, literal("0f"), &result) && result.bits == UINT64_C(0x80000000));
    assert(numeric_unary(&source, 0u, EXPRESSION_NEGATE, literal("0.0"), &result) && result.bits == UINT64_C(0x8000000000000000));
    assert(!numeric_unary(&source, 0u, EXPRESSION_NEGATE, literal("1u"), &result));
    assert(numeric_unary(&source, 0u, EXPRESSION_COMPLEMENT, literal("0i"), &result) && result.bits == UINT64_C(0xffffffff));
    assert(numeric_unary(&source, 0u, EXPRESSION_NOT, literal("false"), &result) && result.bits == 1u);
    assert(binary(EXPRESSION_ADD, literal("2147483647i"), literal("1i")).bits == i_min.bits);
    assert(binary(EXPRESSION_MULTIPLY, i_min, i_negative_one).bits == i_min.bits);
    assert(binary(EXPRESSION_ADD, literal("4294967295u"), literal("1u")).bits == 0u);
    assert(binary(EXPRESSION_MULTIPLY, literal("4294967295u"), literal("4294967295u")).bits == 1u);
    assert(binary(EXPRESSION_SUBTRACT, literal("0i"), literal("1i")).bits == i_negative_one.bits);
    reject_binary(EXPRESSION_ADD, ai_max, literal("1"));
    reject_binary(EXPRESSION_SUBTRACT, ai_min, literal("1"));
    reject_binary(EXPRESSION_MULTIPLY, ai_min, ai_negative_one);
    reject_binary(EXPRESSION_MULTIPLY, ai_max, literal("2"));
    assert(binary(EXPRESSION_MULTIPLY, ai_min, literal("0")).bits == 0u);
    assert(binary(EXPRESSION_MULTIPLY, ai_min, literal("1")).bits == ai_min.bits);
    reject_binary(EXPRESSION_DIVIDE, i_min, i_negative_one);
    reject_binary(EXPRESSION_REMAINDER, i_min, i_negative_one);
    reject_binary(EXPRESSION_DIVIDE, ai_min, ai_negative_one);
    reject_binary(EXPRESSION_REMAINDER, ai_min, ai_negative_one);
    reject_binary(EXPRESSION_DIVIDE, literal("1u"), literal("0u"));
    assert(binary(EXPRESSION_DIVIDE, value(IR_I32, UINT64_C(0xfffffff9)), literal("3i")).bits == UINT64_C(0xfffffffe));
    assert(binary(EXPRESSION_REMAINDER, value(IR_I32, UINT64_C(0xfffffff9)), literal("3i")).bits == UINT64_C(0xffffffff));
    assert(binary(EXPRESSION_SHIFT_LEFT, ai_negative_one, literal("63u")).bits == ai_min.bits);
    assert(binary(EXPRESSION_SHIFT_RIGHT, ai_negative_one, literal("4294967295u")).bits == UINT64_MAX);
    assert(binary(EXPRESSION_SHIFT_RIGHT, ai_max, literal("64u")).bits == 0u);
    assert(binary(EXPRESSION_SHIFT_LEFT, literal("0"), literal("4294967295u")).bits == 0u);
    reject_binary(EXPRESSION_SHIFT_LEFT, ai_negative_one, literal("64u"));
    reject_binary(EXPRESSION_SHIFT_LEFT, literal("1"), literal("63u"));
    reject_binary(EXPRESSION_SHIFT_LEFT, literal("1u"), literal("32u"));
    reject_binary(EXPRESSION_SHIFT_RIGHT, literal("0u"), literal("32u"));
    reject_binary(EXPRESSION_SHIFT_LEFT, literal("2147483648u"), literal("1u"));
    reject_binary(EXPRESSION_SHIFT_LEFT, literal("1073741824i"), literal("1u"));
    assert(binary(EXPRESSION_SHIFT_LEFT, i_negative_one, literal("31u")).bits == i_min.bits);
    assert(binary(EXPRESSION_SHIFT_RIGHT, i_min, literal("31u")).bits == UINT64_C(0xffffffff));
    assert(binary(EXPRESSION_BIT_AND, literal("true"), literal("false")).bits == 0u);
    assert(binary(EXPRESSION_BIT_OR, literal("true"), literal("false")).bits == 1u);
    reject_binary(EXPRESSION_BIT_XOR, literal("true"), literal("false"));
    assert(binary(EXPRESSION_BIT_XOR, literal("0xffu"), literal("0x55u")).bits == UINT64_C(0xaa));
    assert(binary(EXPRESSION_MAX, i_min, literal("1i")).bits == 1u);
    assert(numeric_binary(&source, 0u, EXPRESSION_COMPARE, COMPARE_LESS, i_min, literal("1i"), &result));
    assert(result.type == IR_BOOL && result.bits == 1u);
    assert(binary(EXPRESSION_ADD, literal("16777216f"), literal("1f")).bits == UINT64_C(0x4b800000));
    assert(binary(EXPRESSION_SUBTRACT,
                  binary(EXPRESSION_ADD, literal("16777216.0"), literal("1.0")), literal("16777216.0")).bits ==
           UINT64_C(0x3ff0000000000000));
    assert(binary(EXPRESSION_MULTIPLY, literal("0x1p-149f"), literal("0.5f")).bits == 0u);
    assert(binary(EXPRESSION_REMAINDER, value(IR_F32, UINT64_C(0xc0b00000)), literal("2f")).bits == UINT64_C(0xbfc00000));
    assert(binary(EXPRESSION_REMAINDER, literal("0x1.fffffep127f"), literal("0x1p-149f")).bits == 0u);
    reject_binary(EXPRESSION_MULTIPLY, literal("0x1.fffffep127f"), literal("2f"));
    reject_binary(EXPRESSION_DIVIDE, literal("1f"), literal("0f"));
    reject_binary(EXPRESSION_REMAINDER, literal("1.0"), literal("0.0"));
}
static void test_conversions(void) {
    DoeCompilerDiagnostic diagnostic;
    Source source = test_source("conversions", &diagnostic);
    NumericValue result;
    assert(!numeric_materialize(&source, 0u, literal("4294967295"), IR_I32, &result));
    assert(numeric_materialize(&source, 0u, literal("4294967295"), IR_F32, &result));
    assert(result.bits == UINT64_C(0x4f800000));
    assert(!numeric_materialize(&source, 0u, literal("1u"), IR_F32, &result));
    assert(!numeric_materialize(&source, 0u, literal("1.0"), IR_I32, &result));
    assert(numeric_materialize(&source, 0u, literal("0x1.0000000001p0"), IR_F32, &result));
    assert(result.bits == UINT64_C(0x3f800000));
    assert(!numeric_materialize(&source, 0u, literal("0x1p128"), IR_F32, &result));
    assert(numeric_convert(&source, 0u, literal("4294967296.0"), IR_U32, &result));
    assert(result.bits == UINT64_C(0xffffffff));
    assert(numeric_convert(&source, 0u, literal("4294967296f"), IR_U32, &result));
    assert(result.bits == UINT64_C(0xffffff00));
    assert(numeric_convert(&source, 0u, literal("2147483648.0"), IR_I32, &result));
    assert(result.bits == UINT64_C(0x7fffffff));
    assert(numeric_convert(&source, 0u, literal("2147483648f"), IR_I32, &result));
    assert(result.bits == UINT64_C(0x7fffff80));
    assert(numeric_convert(&source, 0u, value(IR_F32, UINT64_C(0xcf800000)), IR_I32, &result));
    assert(result.bits == UINT64_C(0x80000000));
    assert(numeric_convert(&source, 0u, value(IR_F32, UINT64_C(0xbf800000)), IR_U32, &result));
    assert(result.bits == 0u);
    assert(numeric_convert(&source, 0u, literal("3.9f"), IR_U32, &result) && result.bits == 3u);
    assert(numeric_convert(&source, 0u, value(IR_I32, UINT64_C(0xffffffff)), IR_U32, &result));
    assert(result.bits == UINT64_C(0xffffffff));
    assert(!numeric_convert(&source, 0u, value(IR_ABSTRACT_INT, UINT64_MAX), IR_U32, &result));
    assert(numeric_convert(&source, 0u, literal("true"), IR_F32, &result) && result.bits == UINT64_C(0x3f800000));
    assert(numeric_convert(&source, 0u, value(IR_F32, UINT64_C(0x80000000)), IR_BOOL, &result) && result.bits == 0u);
    assert(numeric_convert(&source, 0u, literal("0x1p-149f"), IR_BOOL, &result) && result.bits == 1u);
    assert(numeric_bitcast(&source, 0u, value(IR_U32, UINT64_C(0x80000000)), IR_F32, &result));
    assert(result.bits == UINT64_C(0x80000000));
    assert(numeric_bitcast(&source, 0u, result, IR_I32, &result) && result.bits == UINT64_C(0x80000000));
    assert(!numeric_bitcast(&source, 0u, value(IR_U32, UINT64_C(0x7f800000)), IR_F32, &result));
    assert(!numeric_bitcast(&source, 0u, value(IR_U32, UINT64_C(0x7fc00001)), IR_F32, &result));
    assert(!numeric_bitcast(&source, 0u, literal("1"), IR_U32, &result));
    assert(!numeric_bitcast(&source, 0u, literal("true"), IR_U32, &result));
}
static void check_environment(int rounding) {
    fenv_t original;
    assert(fegetenv(&original) == 0);
    const uint32_t original_mxcsr = _mm_getcsr();
    assert(fedisableexcept(FE_ALL_EXCEPT) >= 0);
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(fesetround(rounding) == 0);
    assert(feraiseexcept(FE_INEXACT | FE_UNDERFLOW) == 0);
    assert(feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW) >= 0);
    /* Denormal-operand sticky status is not part of ISO FE_ALL_EXCEPT. */
    _mm_setcsr(_mm_getcsr() | UINT32_C(0x8042));
    const uint32_t expected_mxcsr = _mm_getcsr();
    const int expected_exceptions = fetestexcept(FE_ALL_EXCEPT);
    const int expected_traps = fegetexcept();
    DoeCompilerDiagnostic diagnostic;
    Source source = test_source("environment", &diagnostic);
    NumericEnvironment environment;
    assert(numeric_environment_begin(&source, &environment));
    assert(fegetround() == FE_TONEAREST && fegetexcept() == 0);
    assert(fetestexcept(FE_ALL_EXCEPT) == 0);
    assert((_mm_getcsr() & UINT32_C(0x8040)) == 0u);
    assert(binary(EXPRESSION_MULTIPLY, literal("0x1p-149f"), literal("1f")).bits == 1u);
    assert(binary(EXPRESSION_ADD, literal("16777216f"), literal("1f")).bits == UINT64_C(0x4b800000));
    reject_binary(EXPRESSION_DIVIDE, literal("1f"), literal("0f"));
    reject_binary(EXPRESSION_MULTIPLY, literal("0x1.fffffep127f"), literal("2f"));
    assert(numeric_environment_end(&source, &environment));
    assert(fegetround() == rounding);
    assert(fetestexcept(FE_ALL_EXCEPT) == expected_exceptions);
    assert(fegetexcept() == expected_traps);
    assert(_mm_getcsr() == expected_mxcsr);
    assert(numeric_environment_end(&source, &environment));
    assert(fesetenv(&original) == 0);
    _mm_setcsr(original_mxcsr);
}
static void *environment_thread(void *argument) {
    const int rounding = *(const int *)argument;
    for (unsigned i = 0u; i < 100u; ++i) check_environment(rounding);
    return NULL;
}
int main(void) {
    DoeCompilerDiagnostic diagnostic;
    Source source = test_source("", &diagnostic);
    NumericEnvironment environment;
    assert(numeric_environment_begin(&source, &environment));
    test_literals();
    test_float_literals();
    test_operations();
    test_conversions();
    assert(numeric_environment_end(&source, &environment));
    const int modes[] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    pthread_t threads[4];
    for (size_t i = 0u; i < 4u; ++i) {
        check_environment(modes[i]);
        assert(pthread_create(&threads[i], NULL, environment_thread, (void *)&modes[i]) == 0);
    }
    for (size_t i = 0u; i < 4u; ++i) assert(pthread_join(threads[i], NULL) == 0);
    (void)puts("numeric values: literals, scalar operations, conversions, fenv and concurrency passed");
    return 0;
}
