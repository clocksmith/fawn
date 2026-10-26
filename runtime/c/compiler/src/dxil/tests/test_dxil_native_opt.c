/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_util.h"
#include "dxil_source_native_opt.h"
#include "../numeric.h"
#include <stdlib.h>

static DxilIrModule module;
static DxilSourceBridge bridge;
static DxilSourceFunction function;
static DoeCompilerDiagnostic source_diagnostic;

static void fixture(unsigned which) {
    memset(&module, 0, sizeof(module));
    memset(&bridge, 0, sizeof(bridge));
    memset(&function, 0, sizeof(function));
    memset(&source_diagnostic, 0, sizeof(source_diagnostic));
    bridge.source.diagnostic = &source_diagnostic;
    bridge.module = &module;
    function.view = &module.functions[0];
    module.function_count = 1u;
    module.type_count = 3u;
    module.types[0] = (DxilIrType){.kind = DXIL_IR_TYPE_SCALAR, .scalar = DXIL_IR_SCALAR_U32};
    module.types[1] = (DxilIrType){.kind = DXIL_IR_TYPE_SCALAR, .scalar = DXIL_IR_SCALAR_I32};
    module.types[2] = (DxilIrType){.kind = DXIL_IR_TYPE_SCALAR, .scalar = DXIL_IR_SCALAR_BOOL};
    const uint32_t ty = which == 4u || which == 5u ? 2u : which == 10u || which == 12u || which == 14u ? 1u : 0u;
    DxilIrFunction *f = function.view;
    f->expr_count = 3u;
    f->exprs[0] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = ty, .uint_value = 64u};
    f->exprs[1] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = ty, .uint_value = 2u};
    f->exprs[2] = (DxilIrExpr){.kind = DXIL_IR_EXPR_BINARY, .ty = ty,
        .binary_op = DXIL_IR_BINARY_MUL, .lhs = 0u, .rhs = 1u};
    if (which == 0u || which == 7u) {
        f->local_count = 1u;
        f->locals[0] = (DxilIrLocal){.name = "alias", .ty = ty, .mutable = which == 7u};
        f->stmts[f->stmt_count++] = (DxilIrStmt){.kind = DXIL_IR_STMT_LOCAL_DECL,
            .local = 0u, .is_const = which == 0u, .has_initializer = true, .initializer = 0u};
        f->exprs[3] = (DxilIrExpr){.kind = DXIL_IR_EXPR_LOCAL_REF, .ty = ty, .index = 0u};
        f->expr_count = 4u;
        f->exprs[2].lhs = 3u;
    } else if (which == 1u) {
        module.global_count = 1u;
        module.globals[0] = (DxilIrGlobal){.class = DXIL_IR_GLOBAL_CONST, .has_initializer = true,
            .init_kind = DXIL_IR_CONST_INT, .init_int = 7u};
        f->exprs[0].kind = DXIL_IR_EXPR_GLOBAL_REF;
        f->exprs[1].uint_value = 4u;
        f->exprs[2].binary_op = DXIL_IR_BINARY_ADD;
    } else if (which >= 2u && which <= 6u) {
        f->exprs[0].kind = which == 6u ? DXIL_IR_EXPR_CALL : DXIL_IR_EXPR_PARAM_REF;
        f->exprs[0].name = "effect";
        f->exprs[1].uint_value = which == 3u ? 1u : 0u;
        f->exprs[2].binary_op = which == 2u ? DXIL_IR_BINARY_ADD : DXIL_IR_BINARY_MUL;
        if (which == 4u || which == 5u) {
            f->exprs[1].kind = DXIL_IR_EXPR_BOOL_LIT;
            f->exprs[1].bool_value = which == 4u;
            f->exprs[2].binary_op = which == 4u ? DXIL_IR_BINARY_LOGICAL_AND : DXIL_IR_BINARY_LOGICAL_OR;
        }
    } else if (which == 8u) {
        f->exprs[0].kind = DXIL_IR_EXPR_PARAM_REF;
        f->exprs[0].category = DXIL_IR_EXPR_REF;
        f->exprs[1].uint_value = 0u;
        f->exprs[2].binary_op = DXIL_IR_BINARY_ADD;
    } else if (which == 9u || which == 10u) {
        f->exprs[0].uint_value = which == 10u ? UINT32_C(0x80000000) : 7u;
        f->exprs[1].uint_value = which == 10u ? UINT32_MAX : 0u;
        f->exprs[2].binary_op = DXIL_IR_BINARY_DIV;
    } else if (which == 11u) {
        f->exprs[0].uint_value = UINT32_MAX;
        f->exprs[2].binary_op = DXIL_IR_BINARY_ADD;
    } else if (which == 12u) {
        f->exprs[0].uint_value = UINT32_C(0x80000000);
        f->exprs[2].kind = DXIL_IR_EXPR_UNARY;
        f->exprs[2].unary_op = DXIL_IR_UNARY_NEG;
        f->exprs[2].operand = 0u;
    } else if (which >= 13u) {
        f->exprs[0].uint_value = which == 13u ? 7u : UINT32_C(0x80000000);
        f->exprs[1].ty = 0u;
        f->exprs[1].uint_value = which == 13u ? 32u : which == 14u ? 33u : 1u;
        f->exprs[2].binary_op = which == 14u ? DXIL_IR_BINARY_SHIFT_RIGHT : DXIL_IR_BINARY_SHIFT_LEFT;
    }
    f->root_stmt = f->stmt_count;
    f->stmts[f->stmt_count++] = (DxilIrStmt){.kind = DXIL_IR_STMT_RETURN,
        .has_return_value = true, .return_value = 2u};
}
static const DxilIrExpr *result(void) {
    return &function.view->exprs[function.view->stmts[function.view->root_stmt].return_value];
}
static void cases(void) {
    const DxilIrExprKind kinds[] = {DXIL_IR_EXPR_INT_LIT, DXIL_IR_EXPR_INT_LIT,
        DXIL_IR_EXPR_PARAM_REF, DXIL_IR_EXPR_PARAM_REF, DXIL_IR_EXPR_PARAM_REF, DXIL_IR_EXPR_PARAM_REF,
        DXIL_IR_EXPR_BINARY, DXIL_IR_EXPR_BINARY, DXIL_IR_EXPR_BINARY, DXIL_IR_EXPR_BINARY,
        DXIL_IR_EXPR_BINARY, DXIL_IR_EXPR_INT_LIT, DXIL_IR_EXPR_INT_LIT, DXIL_IR_EXPR_INT_LIT,
        DXIL_IR_EXPR_INT_LIT, DXIL_IR_EXPR_INT_LIT};
    const uint64_t values[] = {128u, 11u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u,
        UINT32_C(0x80000000), 7u, UINT32_C(0xc0000000), 0u};
    for (unsigned i = 0u; i < 16u; ++i) {
        fixture(i);
        CHECK(dxil_source_native_optimize(&bridge, &function));
        CHECK(result()->kind == kinds[i]);
        if (kinds[i] == DXIL_IR_EXPR_INT_LIT) CHECK(result()->uint_value == values[i]);
    }
}
static void numeric_contract(void) {
    DoeCompilerDiagnostic diagnostic = {0};
    Source source = {.diagnostic = &diagnostic};
    const NumericValue sentinel = {.bits = 123u, .type = IR_F32};
    NumericValue out = sentinel;
    CHECK(!numeric_runtime_integer_binary(&source, EXPRESSION_DIVIDE,
        (NumericValue){7u, IR_U32}, (NumericValue){0u, IR_U32}, &out));
    CHECK(out.bits == sentinel.bits && out.type == sentinel.type);
    CHECK(!numeric_runtime_integer_binary(&source, EXPRESSION_ADD,
        (NumericValue){7u, IR_U32}, (NumericValue){1u, IR_I32}, &out));
    CHECK(out.bits == sentinel.bits && out.type == sentinel.type);
    CHECK(!numeric_runtime_integer_binary(&source, EXPRESSION_COMPARE,
        (NumericValue){7u, IR_U32}, (NumericValue){1u, IR_U32}, &out));
    CHECK(out.bits == sentinel.bits && out.type == sentinel.type);
    const Source original = source;
    CHECK(numeric_runtime_integer_binary(&source, EXPRESSION_SHIFT_LEFT,
        (NumericValue){7u, IR_U32}, (NumericValue){32u, IR_U32}, &out));
    CHECK(out.bits == 7u && memcmp(&source, &original, sizeof(source)) == 0);
    out = sentinel;
    CHECK(!numeric_binary(&source, 0u, EXPRESSION_SHIFT_LEFT, COMPARE_EQUAL,
        (NumericValue){7u, IR_U32}, (NumericValue){32u, IR_U32}, &out));
    CHECK(out.bits == sentinel.bits && out.type == sentinel.type);
}
static void nested_effects(void) {
    for (unsigned op = 0u; op < 2u; ++op) for (unsigned side = 0u; side < 2u; ++side) {
        fixture(6u);
        DxilIrFunction *f = function.view;
        f->exprs[3] = f->exprs[2];
        f->exprs[2] = f->exprs[1];
        f->exprs[1] = (DxilIrExpr){.kind = DXIL_IR_EXPR_CONSTRUCT, .ty = 0u, .args = {0u, 1u}};
        f->expr_args[0] = 0u;
        f->expr_arg_count = 1u;
        f->expr_count = 4u;
        f->exprs[3].binary_op = op == 0u ? DXIL_IR_BINARY_MUL : DXIL_IR_BINARY_BIT_AND;
        f->exprs[3].lhs = side == 0u ? 1u : 2u;
        f->exprs[3].rhs = side == 0u ? 2u : 1u;
        f->stmts[f->root_stmt].return_value = 3u;
        CHECK(dxil_source_native_optimize(&bridge, &function));
        CHECK(f->stmts[f->root_stmt].return_value == 3u);
        CHECK(result()->kind == DXIL_IR_EXPR_BINARY);
    }
}
static void references_and_failures(void) {
    fixture(2u);
    DxilIrFunction *f = function.view;
    f->expr_args[0] = 2u;
    f->expr_arg_count = 1u;
    f->exprs[3] = (DxilIrExpr){.kind = DXIL_IR_EXPR_CONSTRUCT, .ty = 0u, .args = {0u, 1u}};
    f->exprs[4] = (DxilIrExpr){.kind = DXIL_IR_EXPR_UNARY, .ty = 0u,
        .unary_op = DXIL_IR_UNARY_BIT_NOT, .operand = 2u};
    f->expr_count = 5u;
    f->switch_selectors[0] = 2u;
    f->switch_selector_count = 1u;
    f->stmts[f->stmt_count++] = (DxilIrStmt){.kind = DXIL_IR_STMT_IF, .cond = 2u};
    f->stmts[f->stmt_count++] = (DxilIrStmt){.kind = DXIL_IR_STMT_LOOP, .has_cond = true, .cond = 2u};
    f->stmts[f->stmt_count++] = (DxilIrStmt){.kind = DXIL_IR_STMT_SWITCH, .switch_expr = 2u};
    f->stmts[f->stmt_count++] = (DxilIrStmt){.kind = DXIL_IR_STMT_ASSIGN, .assign_lhs = 0u, .assign_rhs = 2u};
    CHECK(dxil_source_native_optimize(&bridge, &function));
    CHECK(f->expr_args[0] == 0u && f->exprs[4].operand == 0u && f->switch_selectors[0] == 0u);
    CHECK(f->stmts[1].cond == 0u && f->stmts[2].cond == 0u &&
        f->stmts[3].switch_expr == 0u && f->stmts[4].assign_rhs == 0u);
    fixture(2u);
    function.view->exprs[2].lhs = DXIL_IR_MAX_EXPRS;
    CHECK(!dxil_source_native_optimize(&bridge, &function));
    CHECK(bridge.status == DXIL_INVALID_IR);
    fixture(2u);
    function.view->exprs[2].kind = DXIL_IR_EXPR_CONSTRUCT;
    function.view->exprs[2].args = (DxilIrRange){UINT32_MAX, 1u};
    CHECK(!dxil_source_native_optimize(&bridge, &function));
    CHECK(bridge.status == DXIL_INVALID_IR);
    fixture(2u);
    bridge.status = DXIL_UNSUPPORTED_CONSTRUCT;
    const DxilIrExpr before = function.view->exprs[2];
    CHECK(!dxil_source_native_optimize(&bridge, &function));
    CHECK(memcmp(&before, &function.view->exprs[2], sizeof(before)) == 0);
}
static void real_source(void) {
    const char source[] = "@compute @workgroup_size(1) fn main(){let base=64u;let second=base;let result=second*2u;}";
    char names[4096]; DxilDiagnostic diagnostic; DxilDiagKind error;
    CHECK_MSG(dxil_analyze(source, strlen(source), &module, names, sizeof(names), &diagnostic, &error), "%s", diagnostic.message);
    const DxilIrFunction *f = &module.functions[0];
    bool found = false;
    for (uint32_t i = 0u; i < f->stmt_count; ++i) {
        const DxilIrStmt *s = &f->stmts[i];
        if (s->kind == DXIL_IR_STMT_LOCAL_DECL && s->has_initializer &&
            strcmp(f->locals[s->local].name, "result") == 0) {
            const DxilIrExpr *e = &f->exprs[s->initializer];
            CHECK(e->kind == DXIL_IR_EXPR_INT_LIT && e->uint_value == 128u);
            found = true;
        }
    }
    CHECK(found);
}
int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--case") == 0) {
        unsigned which = (unsigned)strtoul(argv[2], NULL, 10);
        if (which >= 16u) return 2;
        fixture(which);
        if (!dxil_source_native_optimize(&bridge, &function)) return 1;
        const DxilIrExpr *e = result();
        printf("%s %llu\n", e->kind == DXIL_IR_EXPR_INT_LIT ? "int_lit" :
            e->kind == DXIL_IR_EXPR_PARAM_REF ? "param_ref" : "binary",
            (unsigned long long)(e->kind == DXIL_IR_EXPR_INT_LIT ? e->uint_value : 0u));
        return 0;
    }
    if (argc != 1) return 2;
    cases(); numeric_contract(); nested_effects(); references_and_failures(); real_source();
    TEST_SUMMARY("dxil_native_opt");
}
