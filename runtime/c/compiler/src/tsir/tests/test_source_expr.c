/* SPDX-License-Identifier: MIT */
#include "../tsir_source_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void packed_controls(void) {
    const NumericBuiltinKind kinds[] = {BUILTIN_PACK4X_I8, BUILTIN_PACK4X_U8,
        BUILTIN_PACK4X_I8_CLAMP, BUILTIN_PACK4X_U8_CLAMP, BUILTIN_UNPACK4X_I8,
        BUILTIN_UNPACK4X_U8, BUILTIN_DOT4_I8_PACKED, BUILTIN_DOT4_U8_PACKED};
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k) {
        TsirIrExpr expressions[128] = {0};
        uint32_t edges[128] = {0};
        TsirSourceBridge b = {.types = calloc(IR_PRIMITIVE_COUNT, sizeof(TsirIrType)),
            .module.type_count = IR_PRIMITIVE_COUNT};
        assert(b.types != NULL);
        b.module.types = b.types;
        TsirSourceFunction f = {.exprs = expressions, .args = edges, .expr_cap = 128u};
        f.view.exprs = expressions;
        f.view.expr_args = edges;
        /* A nested indexed argument makes the constructor boundary observable
         * to the real family-hint walker, unlike a constant-only shape test. */
        expressions[0] = (TsirIrExpr){.kind = TSIR_IR_EXPR_GLOBAL_REF, .ty = IR_U32};
        expressions[1] = (TsirIrExpr){.kind = TSIR_IR_EXPR_INT_LIT, .ty = IR_U32};
        expressions[2] = (TsirIrExpr){.kind = TSIR_IR_EXPR_INDEX, .ty = IR_U32,
            .index_base = 0u, .index_index = 1u};
        f.view.expr_count = 3u;
        edges[0] = 2u;
        edges[1] = 2u;
        f.view.expr_arg_count = 2u;
        TsirIrExpr call = {.ty = IR_U32, .args_start = 0u, .args_len = packed_dot(kinds[k]) ? 2u : 1u};
        uint32_t root = tsir_source_packed(&b, &f, kinds[k], &call);
        assert(root != TSIR_IR_NONE && b.status == TSIR_OK);
        assert(b.module.type_count == IR_PRIMITIVE_COUNT + 2u);
        if (packed_pack(kinds[k]) || packed_dot(kinds[k]))
            assert(expressions[root].kind == TSIR_IR_EXPR_CALL && !strcmp(expressions[root].call_name, "dot"));
        else {
            assert(expressions[root].kind == TSIR_IR_EXPR_BINARY);
            assert(expressions[root].binary_op == (packed_signed(kinds[k]) ? TSIR_IR_BIN_SHIFT_RIGHT : TSIR_IR_BIN_BIT_AND));
        }
        if (!packed_pack(kinds[k])) {
            (void)tsir_source_push_expr(&b, &f, (TsirIrExpr){.kind = TSIR_IR_EXPR_INDEX,
                .index_base = 0u, .index_index = root, .ty = IR_U32});
            TsirIterationAxis axis = {.name = "i", .lower_bound = "0", .upper_bound = "4", .step = "1"};
            assert(tsir_family_hint_infer_ir(&f.view, &axis, 1u, NULL, 0u) == TSIR_HINT_ELEMENTWISE);
        }
        free(b.types);
    }
    puts("TSIR packed projection: eight builtin trees and indirect-index constructor boundaries passed");
}

int main(void) {
    static const char source[] = "local global rgba vec4f bool atomicLoad arrayLength bitcast";
    CompilerWorkspace compiler = {.module_flags = MODULE_F16};
    DoeCompilerDiagnostic diagnostic = {0};
    Expression syntax[8] = {0};
    ArgumentSyntax arguments[2] = {{0u, NO_ID}};
    compiler.expressions = syntax;
    compiler.expression_count = 8u;
    compiler.arguments = arguments;
    compiler.argument_count = 1u;
    TsirIrExpr expressions[128] = {0};
    uint32_t edges[128] = {0}, scope[1] = {0};
    NameSpan local_name[1] = {{0u, 5u}};
    TsirIrLocal locals[1] = {{.name = "local", .ty = IR_U32}};
    TsirIrGlobal globals[1] = {{.name = "global", .ty = IR_U32, .class = TSIR_IR_GLOBAL_CONST}};
    char names[256];
    TsirSourceBridge b = {.compiler = &compiler, .names = names, .names_cap = sizeof(names),
        .globals = globals, .module.global_count = 1u,
        .source = {.source = source, .length = sizeof(source) - 1u, .compiler = &compiler, .diagnostic = &diagnostic}};
    TsirSourceFunction f = {.exprs = expressions, .args = edges, .expr_cap = 128u,
        .locals = locals, .scope = scope, .scope_count = 1u, .local_names = local_name};
    syntax[0] = (Expression){.kind = EXPRESSION_NAME, .resolved_type = IR_U32, .value.name = {0u, 5u}};
    uint32_t id = tsir_source_expr(&b, &f, 0u, false, 0u);
    assert(expressions[id].kind == TSIR_IR_EXPR_LOAD);
    locals[0].mutable = true;
    id = tsir_source_expr(&b, &f, 0u, false, 0u);
    assert(expressions[id].kind == TSIR_IR_EXPR_LOAD);
    id = tsir_source_root(&b, &f, (NameSpan){6u, 6u}, false);
    assert(expressions[id].kind == TSIR_IR_EXPR_GLOBAL_REF);
    globals[0].class = TSIR_IR_GLOBAL_VAR;
    id = tsir_source_root(&b, &f, (NameSpan){6u, 6u}, false);
    assert(expressions[id].kind == TSIR_IR_EXPR_LOAD);
    locals[0].mutable = false;
    syntax[1] = (Expression){.kind = EXPRESSION_MEMBER, .resolved_type = IR_U32,
        .left = 0u, .value.member.member = {14u, 3u}}; /* gba -> first index 1 */
    id = tsir_source_expr(&b, &f, 1u, false, 0u);
    assert(expressions[id].kind == TSIR_IR_EXPR_LOAD &&
        expressions[expressions[id].inner].kind == TSIR_IR_EXPR_MEMBER &&
        expressions[expressions[id].inner].member_field_index == 1u);
    locals[0].mutable = true;
    id = tsir_source_expr(&b, &f, 1u, false, 0u);
    assert(expressions[id].kind == TSIR_IR_EXPR_LOAD);
    const NameSpan constructors[] = {{18u, 5u}, {24u, 4u}};
    for (size_t i = 0; i < 2u; ++i) {
        syntax[2] = (Expression){.kind = EXPRESSION_CALL, .resolved_type = IR_U32,
            .right = 0u, .left = NO_ID, .value.call = {.callee = constructors[i], .type.offset = NO_ID}};
        id = tsir_source_expr(&b, &f, 2u, false, 0u);
        assert(expressions[id].kind == TSIR_IR_EXPR_CONSTRUCT);
    }
    syntax[3] = (Expression){.kind = EXPRESSION_ADDRESS, .resolved_type = IR_U32, .left = 0u};
    arguments[0].expression = 3u;
    syntax[4] = (Expression){.kind = EXPRESSION_CALL, .resolved_type = IR_U32,
        .right = 1u, .left = 0u, .value.call = {.callee = {29u, 10u}, .type.offset = NO_ID}};
    id = tsir_source_expr(&b, &f, 4u, false, 0u);
    assert(expressions[id].kind == TSIR_IR_EXPR_CALL);
    assert(expressions[edges[expressions[id].args_start]].kind == TSIR_IR_EXPR_LOCAL_REF);
    syntax[4].value.call.callee = (NameSpan){40u, 11u};
    id = tsir_source_expr(&b, &f, 4u, false, 0u);
    assert(expressions[edges[expressions[id].args_start]].kind == TSIR_IR_EXPR_LOCAL_REF);
    syntax[5] = (Expression){.kind = EXPRESSION_DEREFERENCE, .resolved_type = IR_U32, .left = 3u};
    id = tsir_source_expr(&b, &f, 5u, false, 0u);
    assert(expressions[id].kind == TSIR_IR_EXPR_LOAD &&
        expressions[expressions[id].inner].kind == TSIR_IR_EXPR_DEREF);
    id = tsir_source_expr(&b, &f, 5u, true, 0u);
    assert(expressions[id].kind == TSIR_IR_EXPR_DEREF);
    static const char *const literals[] = {"0.1f", "0.1h", "7f", "100000000000000000000f", "0x1.8p2f"};
    const double expected[] = {0.1, 0.1, 7.0, 1e20, 6.0};
    for (size_t i = 0; i < 5u; ++i) {
        b.source.source = literals[i];
        b.source.length = strlen(literals[i]);
        syntax[6] = (Expression){.kind = EXPRESSION_LITERAL, .resolved_type = IR_F32,
            .value.name = {0u, (uint32_t)b.source.length}};
        id = tsir_source_expr(&b, &f, 6u, false, 0u);
        assert(id != TSIR_IR_NONE && expressions[id].kind == TSIR_IR_EXPR_FLOAT_LIT);
        assert(expressions[id].float_value == expected[i]);
    }
    static const char ldexp_source[] = "ldexp";
    b.source.source = ldexp_source;
    b.source.length = sizeof(ldexp_source) - 1u;
    syntax[0] = (Expression){.kind = EXPRESSION_INTEGER, .resolved_type = IR_ABSTRACT_INT, .value.integer = 1u};
    syntax[1] = (Expression){.kind = EXPRESSION_INTEGER, .resolved_type = IR_ABSTRACT_INT, .value.integer = 2u};
    arguments[0] = (ArgumentSyntax){0u, 1u};
    arguments[1] = (ArgumentSyntax){1u, NO_ID};
    compiler.argument_count = 2u;
    syntax[7] = (Expression){.kind = EXPRESSION_CALL, .resolved_type = IR_ABSTRACT_FLOAT,
        .left = 0u, .right = 2u, .value.call = {.callee = {0u, 5u}, .type.offset = NO_ID}};
    id = tsir_source_expr(&b, &f, 7u, false, 0u);
    assert(expressions[id].kind == TSIR_IR_EXPR_CALL);
    assert(expressions[edges[expressions[id].args_start]].kind == TSIR_IR_EXPR_CONSTRUCT);
    packed_controls();
    assert(b.status == TSIR_OK);
    puts("TSIR expression adaptation: 18 category, projection, constructor, reference and numeric controls passed");
    return 0;
}
