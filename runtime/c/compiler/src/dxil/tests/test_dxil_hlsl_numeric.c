/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_util.h"
#include "dxil_hlsl_numeric.h"
#include <stdlib.h>

static DxilIrModule module;
static char output[65536];
static DxilIrExprId call_id;
static DxilIrFunction *function;

static DxilIrTypeId type(DxilIrType value) {
    DxilIrTypeId id = DXIL_IR_INVALID_ID;
    CHECK_STATUS(dxil_ir_intern_type(&module, &value, &id), DXIL_OK);
    return id;
}
static void fixture(unsigned which) {
    static const char *const names[] = {"a", "b", "c", "d"};
    static const char *const builtins[] = {"extractBits", "insertBits", "degrees", "radians"};
    dxil_ir_init(&module);
    DxilIrTypeId scalar = type((DxilIrType){.kind = DXIL_IR_TYPE_SCALAR,
        .scalar = which < 2u || (which >= 4u && which < 7u) ? DXIL_IR_SCALAR_U32 :
            which >= 7u && which < 10u ? DXIL_IR_SCALAR_I32 : DXIL_IR_SCALAR_F32});
    DxilIrTypeId argument_type = scalar;
    if (which >= 4u) argument_type = type((DxilIrType){.kind = DXIL_IR_TYPE_VECTOR,
        .elem = scalar, .len = (uint8_t)(which == 10u ? 2u : (which - 4u) % 3u + 2u)});
    const uint32_t count = which == 0u ? 3u : which == 1u ? 4u : which < 4u ? 1u : 2u;
    CHECK_STATUS(dxil_ir_add_function(&module, "numeric", scalar, &function), DXIL_OK);
    for (uint32_t i = 0u; i < count; ++i) {
        function->params[i] = (DxilIrParam){.name = names[i], .ty = argument_type};
        function->param_count++;
        DxilIrExprId ref, value;
        const DxilIrExpr binding = {.kind = DXIL_IR_EXPR_PARAM_REF, .ty = argument_type,
            .category = DXIL_IR_EXPR_REF, .index = i};
        CHECK_STATUS(dxil_ir_append_expr(function, &binding, &ref), DXIL_OK);
        const DxilIrExpr load = {.kind = DXIL_IR_EXPR_LOAD, .ty = argument_type,
            .category = DXIL_IR_EXPR_VALUE, .inner = ref};
        CHECK_STATUS(dxil_ir_append_expr(function, &load, &value), DXIL_OK);
        CHECK_STATUS(dxil_ir_append_expr_arg(function, value), DXIL_OK);
    }
    const DxilIrExpr call = {.kind = DXIL_IR_EXPR_CALL, .ty = scalar,
        .category = DXIL_IR_EXPR_VALUE, .call_kind = DXIL_IR_CALL_BUILTIN,
        .name = which < 4u ? builtins[which] : "dot", .args = {0u, count}};
    CHECK_STATUS(dxil_ir_append_expr(function, &call, &call_id), DXIL_OK);
    DxilIrStmtId statement;
    const DxilIrStmt result = {.kind = DXIL_IR_STMT_RETURN, .has_return_value = true, .return_value = call_id};
    CHECK_STATUS(dxil_ir_append_stmt(function, &result, &statement), DXIL_OK);
    CHECK_STATUS(dxil_ir_append_stmt_child(function, statement), DXIL_OK);
    const DxilIrStmt block = {.kind = DXIL_IR_STMT_BLOCK, .range = {0u, 1u}};
    CHECK_STATUS(dxil_ir_append_stmt(function, &block, &function->root_stmt), DXIL_OK);
}
static void check_output(unsigned which) {
    static const char *const expected[] = {
        "return ((a >> b) & ((1u << c) - 1u));",
        "return ((a & ~(((1u << d) - 1u) << c)) | ((b & ((1u << d) - 1u)) << c));",
        "return (a * 57.29577951308232);",
        "return (a * 0.017453292519943295);",
        "return __doe_integer_dot_u2(a, b);", "return __doe_integer_dot_u3(a, b);",
        "return __doe_integer_dot_u4(a, b);", "return __doe_integer_dot_i2(a, b);",
        "return __doe_integer_dot_i3(a, b);", "return __doe_integer_dot_i4(a, b);",
        "return dot(a, b);"
    };
    fixture(which);
    size_t written = SIZE_MAX;
    CHECK_STATUS(dxil_hlsl_emit(&module, output, sizeof(output), &written), DXIL_OK);
    CHECK(written < sizeof(output) && written == strlen(output));
    CHECK(strstr(output, expected[which]) != NULL);
    if (which >= 4u && which < 10u) {
        CHECK(strstr(output, "uint sum = uint(a.x) * uint(b.x) + uint(a.y) * uint(b.y)") != NULL);
        CHECK(strstr(output, which < 7u ? "; return sum; }" : "; return asint(sum); }") != NULL);
        CHECK(strstr(output, "return dot(") == NULL);
    } else CHECK(strstr(output, "__doe_integer_dot_") == NULL);
    written = 123u;
    CHECK_STATUS(dxil_hlsl_emit(&module, output, 1u, &written), DXIL_OUTPUT_TOO_LARGE);
    CHECK(written == 123u);
}
static void check_errors(void) {
    fixture(0u);
    const DxilIrExpr *call = &function->exprs[call_id];
    DxilHlslEmitter emitter = {.module = &module, .out = output, .out_len = sizeof(output)};
    bool handled = false;
    CHECK_STATUS(dxil_hlsl_emit_numeric_builtin(&emitter, function, call->ty,
        "extractBits", (DxilIrRange){0u, 2u}, &handled), DXIL_INVALID_IR);
    CHECK(handled && emitter.pos == 0u);
    CHECK_STATUS(dxil_hlsl_emit_numeric_builtin(&emitter, function, call->ty,
        "insertBits", (DxilIrRange){UINT32_MAX, 4u}, &handled), DXIL_INVALID_IR);
    CHECK(emitter.pos == 0u);
    CHECK_STATUS(dxil_hlsl_emit_numeric_builtin(&emitter, function, call->ty,
        "unrelated", (DxilIrRange){0u, 0u}, &handled), DXIL_OK);
    CHECK(!handled && emitter.pos == 0u);
    fixture(4u);
    function->expr_args[1] = DXIL_IR_INVALID_ID;
    CHECK_STATUS(dxil_hlsl_emit_numeric_helpers(&emitter), DXIL_INVALID_IR);
    CHECK(emitter.pos == 0u);
    fixture(4u);
    module.types[function->params[0].ty].len = 1u;
    CHECK_STATUS(dxil_hlsl_emit_numeric_helpers(&emitter), DXIL_INVALID_IR);
    CHECK(emitter.pos == 0u);
    fixture(4u);
    emitter.out_len = 1u;
    CHECK_STATUS(dxil_hlsl_emit_numeric_helpers(&emitter), DXIL_OUTPUT_TOO_LARGE);
}
int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--case") == 0) {
        char *end = NULL;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || value > 10u) return 2;
        fixture((unsigned)value);
        size_t written = 0u;
        if (dxil_hlsl_emit(&module, output, sizeof(output), &written) != DXIL_OK) return 1;
        return fwrite(output, 1u, written, stdout) == written ? 0 : 1;
    }
    for (unsigned i = 0u; i < 11u; ++i) check_output(i);
    check_errors();
    TEST_SUMMARY("dxil_hlsl_numeric");
}
