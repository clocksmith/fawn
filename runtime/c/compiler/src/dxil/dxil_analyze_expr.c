/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_analyze_internal.h"

#include <string.h>

static bool parse_unary(DxilAnalyzeParser *parser,
    DxilIrExprId *out_id);
static bool parse_binary_level(DxilAnalyzeParser *parser, int level,
    DxilIrExprId *out_id);

/* Public entry: tracks nesting depth so deeply nested parentheses,
 * call arguments, and unary chains fail visibly instead of
 * overflowing the stack. */
bool dxil_parse_expr(DxilAnalyzeParser *parser,
    DxilIrExprId *out_id) {
    bool ok = false;
    if (parser->depth >= DXIL_ANALYZE_MAX_DEPTH) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    parser->depth += 1;
    ok = parse_binary_level(parser, 0, out_id);
    parser->depth -= 1;
    return ok;
}

static bool binary_op_for_level(DxilAnalyzeParser *parser, int level,
    DxilIrBinaryOp *out_op, bool *out_is_comparison) {
    int punct = parser->tok.punct;
    *out_is_comparison = false;
    if (parser->tok.kind != DXIL_TOK_PUNCT) {
        return false;
    }
    switch (level) {
    case 0:
        if (punct == DXIL_P_OROR) {
            *out_op = DXIL_IR_BINARY_LOGICAL_OR;
            *out_is_comparison = true;
            return true;
        }
        return false;
    case 1:
        if (punct == DXIL_P_ANDAND) {
            *out_op = DXIL_IR_BINARY_LOGICAL_AND;
            *out_is_comparison = true;
            return true;
        }
        return false;
    case 2:
        if (punct == '|') {
            *out_op = DXIL_IR_BINARY_BIT_OR;
            return true;
        }
        return false;
    case 3:
        if (punct == '^') {
            *out_op = DXIL_IR_BINARY_BIT_XOR;
            return true;
        }
        return false;
    case 4:
        if (punct == '&') {
            *out_op = DXIL_IR_BINARY_BIT_AND;
            return true;
        }
        return false;
    case 5:
        if (punct == DXIL_P_EQEQ) {
            *out_op = DXIL_IR_BINARY_EQUAL;
            *out_is_comparison = true;
            return true;
        }
        if (punct == DXIL_P_NOTEQ) {
            *out_op = DXIL_IR_BINARY_NOT_EQUAL;
            *out_is_comparison = true;
            return true;
        }
        return false;
    case 6:
        if (punct == '<') {
            *out_op = DXIL_IR_BINARY_LESS;
            *out_is_comparison = true;
            return true;
        }
        if (punct == DXIL_P_LTE) {
            *out_op = DXIL_IR_BINARY_LESS_EQUAL;
            *out_is_comparison = true;
            return true;
        }
        if (punct == '>') {
            *out_op = DXIL_IR_BINARY_GREATER;
            *out_is_comparison = true;
            return true;
        }
        if (punct == DXIL_P_GTE) {
            *out_op = DXIL_IR_BINARY_GREATER_EQUAL;
            *out_is_comparison = true;
            return true;
        }
        return false;
    case 7:
        if (punct == DXIL_P_SHL) {
            *out_op = DXIL_IR_BINARY_SHIFT_LEFT;
            return true;
        }
        if (punct == DXIL_P_SHR) {
            *out_op = DXIL_IR_BINARY_SHIFT_RIGHT;
            return true;
        }
        return false;
    case 8:
        if (punct == '+') {
            *out_op = DXIL_IR_BINARY_ADD;
            return true;
        }
        if (punct == '-') {
            *out_op = DXIL_IR_BINARY_SUB;
            return true;
        }
        return false;
    case 9:
        if (punct == '*') {
            *out_op = DXIL_IR_BINARY_MUL;
            return true;
        }
        if (punct == '/') {
            *out_op = DXIL_IR_BINARY_DIV;
            return true;
        }
        if (punct == '%') {
            *out_op = DXIL_IR_BINARY_REM;
            return true;
        }
        return false;
    default:
        return false;
    }
}

static bool parse_binary_level(DxilAnalyzeParser *parser, int level,
    DxilIrExprId *out_id) {
    DxilIrExprId lhs = DXIL_IR_INVALID_ID;
    if (level >= 10) {
        return parse_unary(parser, out_id);
    }
    if (!parse_binary_level(parser, level + 1, &lhs)) {
        return false;
    }
    for (;;) {
        DxilIrBinaryOp op = DXIL_IR_BINARY_ADD;
        bool is_comparison = false;
        DxilIrExprId rhs = DXIL_IR_INVALID_ID;
        DxilIrExpr expr;
        DxilIrExprId result = DXIL_IR_INVALID_ID;
        DxilIrTypeId result_ty = DXIL_IR_INVALID_ID;
        if (!binary_op_for_level(parser, level, &op,
                &is_comparison)) {
            break;
        }
        dxil_parse_advance(parser);
        if (!parse_binary_level(parser, level + 1, &rhs)) {
            return false;
        }
        if (is_comparison) {
            result_ty = parser->bool_ty;
        } else if (dxil_ir_is_float_type(parser->module,
                parser->function->exprs[lhs].ty)
            || dxil_ir_is_float_type(parser->module,
                parser->function->exprs[rhs].ty)) {
            if (dxil_ir_is_float_type(parser->module,
                    parser->function->exprs[lhs].ty)) {
                result_ty = parser->function->exprs[lhs].ty;
            } else {
                result_ty = parser->function->exprs[rhs].ty;
            }
        } else {
            result_ty = parser->function->exprs[lhs].ty;
        }
        memset(&expr, 0, sizeof(expr));
        expr.kind = DXIL_IR_EXPR_BINARY;
        expr.ty = result_ty;
        expr.category = DXIL_IR_EXPR_VALUE;
        expr.binary_op = op;
        expr.lhs = lhs;
        expr.rhs = rhs;
        if (!dxil_parse_append_expr(parser, &expr, &result)) {
            return false;
        }
        lhs = result;
    }
    *out_id = lhs;
    return true;
}

static bool parse_unary(DxilAnalyzeParser *parser,
    DxilIrExprId *out_id);

static bool parse_postfix(DxilAnalyzeParser *parser,
    DxilIrExprId *out_id);

static bool parse_primary(DxilAnalyzeParser *parser,
    DxilIrExprId *out_id) {
    if (parser->tok.kind == DXIL_TOK_INT) {
        DxilIrExpr expr;
        DxilIrTypeId ty = DXIL_IR_INVALID_ID;
        uint64_t value = parser->tok.int_value;
        switch (parser->tok.lit_scalar) {
        case DXIL_IR_SCALAR_I32:
            ty = parser->i32_ty;
            break;
        case DXIL_IR_SCALAR_U32:
            ty = parser->u32_ty;
            break;
        default:
            ty = parser->aint_ty;
            break;
        }
        memset(&expr, 0, sizeof(expr));
        expr.kind = DXIL_IR_EXPR_INT_LIT;
        expr.ty = ty;
        expr.category = DXIL_IR_EXPR_VALUE;
        expr.uint_value = value;
        dxil_parse_advance(parser);
        return dxil_parse_append_expr(parser, &expr, out_id);
    }
    if (parser->tok.kind == DXIL_TOK_FLOAT) {
        DxilIrExpr expr;
        DxilIrTypeId ty = DXIL_IR_INVALID_ID;
        double value = parser->tok.float_value;
        switch (parser->tok.lit_scalar) {
        case DXIL_IR_SCALAR_F32:
            ty = parser->f32_ty;
            break;
        case DXIL_IR_SCALAR_F16:
            ty = parser->f16_ty;
            break;
        default:
            ty = parser->afloat_ty;
            break;
        }
        memset(&expr, 0, sizeof(expr));
        expr.kind = DXIL_IR_EXPR_FLOAT_LIT;
        expr.ty = ty;
        expr.category = DXIL_IR_EXPR_VALUE;
        expr.float_value = value;
        dxil_parse_advance(parser);
        return dxil_parse_append_expr(parser, &expr, out_id);
    }
    if (dxil_parse_is_kw(parser, "true")
        || dxil_parse_is_kw(parser, "false")) {
        DxilIrExpr expr;
        bool value = dxil_parse_is_kw(parser, "true");
        memset(&expr, 0, sizeof(expr));
        expr.kind = DXIL_IR_EXPR_BOOL_LIT;
        expr.ty = parser->bool_ty;
        expr.category = DXIL_IR_EXPR_VALUE;
        expr.bool_value = value;
        dxil_parse_advance(parser);
        return dxil_parse_append_expr(parser, &expr, out_id);
    }
    if (dxil_parse_is_punct(parser, '(')) {
        DxilIrExprId inner = DXIL_IR_INVALID_ID;
        dxil_parse_advance(parser);
        if (!dxil_parse_expr(parser, &inner)) {
            return false;
        }
        if (!dxil_parse_expect_punct(parser, ')')) {
            return false;
        }
        *out_id = inner;
        return true;
    }
    if (parser->tok.kind == DXIL_TOK_IDENT) {
        const char *text = parser->tok.text;
        size_t len = parser->tok.len;
        size_t offset = parser->tok.offset;
        if (dxil_parse_is_type_starter(parser)) {
            size_t saved_pos = parser->pos;
            DxilAnalyzeToken saved_tok = parser->tok;
            bool saved_failed = parser->failed;
            DxilDiagKind saved_kind = parser->error_kind;
            DxilDiagnostic saved_diag;
            DxilIrTypeId construct_ty = DXIL_IR_INVALID_ID;
            bool have_diag = parser->diagnostic != NULL;
            if (have_diag) {
                saved_diag = *parser->diagnostic;
            }
            if (dxil_parse_type(parser, &construct_ty)
                && dxil_parse_is_punct(parser, '(')) {
                DxilIrExpr expr;
                DxilIrExprId result = DXIL_IR_INVALID_ID;
                uint32_t arg_start = 0;
                dxil_parse_advance(parser);
                arg_start = parser->function->expr_arg_count;
                if (!dxil_parse_is_punct(parser, ')')) {
                    for (;;) {
                        DxilIrExprId arg = DXIL_IR_INVALID_ID;
                        if (!dxil_parse_expr(parser, &arg)) {
                            return false;
                        }
                        if (!dxil_parse_append_expr_arg(parser,
                                arg)) {
                            return false;
                        }
                        if (!dxil_parse_accept_punct(parser, ',')) {
                            break;
                        }
                    }
                }
                if (!dxil_parse_expect_punct(parser, ')')) {
                    return false;
                }
                memset(&expr, 0, sizeof(expr));
                expr.kind = DXIL_IR_EXPR_CONSTRUCT;
                expr.ty = construct_ty;
                expr.category = DXIL_IR_EXPR_VALUE;
                expr.construct_ty = construct_ty;
                expr.args.start = arg_start;
                expr.args.len =
                    parser->function->expr_arg_count - arg_start;
                if (!dxil_parse_append_expr(parser, &expr,
                        &result)) {
                    return false;
                }
                *out_id = result;
                return true;
            }
            parser->pos = saved_pos;
            parser->tok = saved_tok;
            parser->failed = saved_failed;
            parser->error_kind = saved_kind;
            if (have_diag) {
                *parser->diagnostic = saved_diag;
            }
        }
        {
            const char *name_text = text;
            size_t name_len = len;
            DxilIrExpr expr;
            DxilIrExprId result = DXIL_IR_INVALID_ID;
            uint32_t param_index = 0;
            uint32_t local_index = 0;
            uint32_t global_index = 0;
            dxil_parse_advance(parser);
            if (dxil_parse_is_punct(parser, '(')) {
                const char *call_name = NULL;
                uint32_t arg_start = 0;
                dxil_parse_advance(parser);
                arg_start = parser->function->expr_arg_count;
                if (!dxil_parse_is_punct(parser, ')')) {
                    for (;;) {
                        DxilIrExprId arg = DXIL_IR_INVALID_ID;
                        if (!dxil_parse_expr(parser, &arg)) {
                            return false;
                        }
                        if (!dxil_parse_append_expr_arg(parser,
                                arg)) {
                            return false;
                        }
                        if (!dxil_parse_accept_punct(parser, ',')) {
                            break;
                        }
                    }
                }
                if (!dxil_parse_expect_punct(parser, ')')) {
                    return false;
                }
                call_name = dxil_parse_dup_name(parser, name_text,
                    name_len);
                if (call_name == NULL) {
                    return false;
                }
                memset(&expr, 0, sizeof(expr));
                expr.kind = DXIL_IR_EXPR_CALL;
                expr.ty = DXIL_IR_INVALID_ID;
                expr.category = DXIL_IR_EXPR_VALUE;
                expr.name = call_name;
                expr.call_kind = DXIL_IR_CALL_BUILTIN;
                expr.args.start = arg_start;
                expr.args.len =
                    parser->function->expr_arg_count - arg_start;
                if (expr.args.len > 0) {
                    expr.ty = parser->function->exprs[
                        parser->function->expr_args[arg_start]].ty;
                }
                expr.robustness_generated = false;
                if (!dxil_parse_append_expr(parser, &expr,
                        &result)) {
                    return false;
                }
                *out_id = result;
                return true;
            }
            if (dxil_parse_find_param(parser, name_text, name_len,
                    &param_index)) {
                memset(&expr, 0, sizeof(expr));
                expr.kind = DXIL_IR_EXPR_PARAM_REF;
                expr.ty =
                    parser->function->params[param_index].ty;
                expr.category = DXIL_IR_EXPR_REF;
                expr.index = param_index;
                if (!dxil_parse_append_expr(parser, &expr,
                        &result)) {
                    return false;
                }
                *out_id = result;
                return true;
            }
            if (dxil_parse_find_local(parser, name_text, name_len,
                    &local_index)) {
                memset(&expr, 0, sizeof(expr));
                expr.kind = DXIL_IR_EXPR_LOCAL_REF;
                expr.ty =
                    parser->function->locals[local_index].ty;
                expr.category = DXIL_IR_EXPR_REF;
                expr.index = local_index;
                if (!dxil_parse_append_expr(parser, &expr,
                        &result)) {
                    return false;
                }
                *out_id = result;
                return true;
            }
            if (dxil_parse_find_global(parser, name_text, name_len,
                    &global_index)) {
                memset(&expr, 0, sizeof(expr));
                expr.kind = DXIL_IR_EXPR_GLOBAL_REF;
                expr.ty =
                    parser->module->globals[global_index].ty;
                expr.category = DXIL_IR_EXPR_REF;
                expr.index = global_index;
                if (!dxil_parse_append_expr(parser, &expr,
                        &result)) {
                    return false;
                }
                *out_id = result;
                return true;
            }
            memset(&expr, 0, sizeof(expr));
            expr.kind = DXIL_IR_EXPR_GLOBAL_REF;
            expr.ty = DXIL_IR_INVALID_ID;
            expr.category = DXIL_IR_EXPR_REF;
            expr.index = UINT32_MAX;
            if (!dxil_parse_append_expr(parser, &expr, &result)) {
                return false;
            }
            if (!dxil_parse_add_pending(parser,
                    parser->function_index, result, name_text,
                    name_len, offset)) {
                return false;
            }
            *out_id = result;
            return true;
        }
    }
    dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
        DXIL_DIAG_STAGE_PARSER);
    return false;
}

static bool parse_unary(DxilAnalyzeParser *parser,
    DxilIrExprId *out_id) {
    DxilIrUnaryOp op = DXIL_IR_UNARY_NEG;
    bool is_addr = false;
    bool is_deref = false;
    if (dxil_parse_is_punct(parser, '-')) {
        op = DXIL_IR_UNARY_NEG;
    } else if (dxil_parse_is_punct(parser, '!')) {
        op = DXIL_IR_UNARY_NOT;
    } else if (dxil_parse_is_punct(parser, '~')) {
        op = DXIL_IR_UNARY_BIT_NOT;
    } else if (dxil_parse_is_punct(parser, '&')) {
        is_addr = true;
    } else if (dxil_parse_is_punct(parser, '*')) {
        is_deref = true;
    } else {
        return parse_postfix(parser, out_id);
    }
    dxil_parse_advance(parser);
    if (parser->depth >= DXIL_ANALYZE_MAX_DEPTH) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    parser->depth += 1;
    {
        DxilIrExprId operand = DXIL_IR_INVALID_ID;
        DxilIrExpr expr;
        DxilIrExprId result = DXIL_IR_INVALID_ID;
        bool ok = parse_unary(parser, &operand);
        parser->depth -= 1;
        if (!ok) {
            return false;
        }
        memset(&expr, 0, sizeof(expr));
        if (is_addr || is_deref) {
            DxilIrType operand_ty;
            expr.kind = is_addr ? DXIL_IR_EXPR_ADDRESS_OF
                : DXIL_IR_EXPR_DEREF;
            expr.category = DXIL_IR_EXPR_REF;
            expr.inner = operand;
            if (is_addr) {
                DxilIrType ref_type;
                DxilIrTypeId ref_id = DXIL_IR_INVALID_ID;
                if (dxil_ir_type_get(parser->module,
                        parser->function->exprs[operand].ty,
                        &operand_ty) == DXIL_OK) {
                    (void)operand_ty;
                    memset(&ref_type, 0, sizeof(ref_type));
                    ref_type.kind = DXIL_IR_TYPE_REF;
                    ref_type.elem =
                        parser->function->exprs[operand].ty;
                    ref_type.addr_space = DXIL_IR_ADDR_FUNCTION;
                    if (dxil_ir_intern_type(parser->module,
                            &ref_type, &ref_id) == DXIL_OK) {
                        expr.ty = ref_id;
                    } else {
                        expr.ty = DXIL_IR_INVALID_ID;
                    }
                } else {
                    expr.ty = DXIL_IR_INVALID_ID;
                }
            } else if (dxil_ir_type_get(parser->module,
                    parser->function->exprs[operand].ty,
                    &operand_ty) == DXIL_OK
                && operand_ty.kind == DXIL_IR_TYPE_REF) {
                expr.ty = operand_ty.elem;
            } else {
                expr.ty = parser->function->exprs[operand].ty;
            }
        } else {
            expr.kind = DXIL_IR_EXPR_UNARY;
            expr.ty = parser->function->exprs[operand].ty;
            expr.category = DXIL_IR_EXPR_VALUE;
            expr.unary_op = op;
            expr.operand = operand;
        }
        if (!dxil_parse_append_expr(parser, &expr, &result)) {
            return false;
        }
        *out_id = result;
        return true;
    }
}

static bool parse_postfix(DxilAnalyzeParser *parser,
    DxilIrExprId *out_id) {
    DxilIrExprId base = DXIL_IR_INVALID_ID;
    if (!parse_primary(parser, &base)) {
        return false;
    }
    for (;;) {
        if (dxil_parse_is_punct(parser, '.')) {
            const char *field_text = NULL;
            size_t field_len = 0;
            const char *field_name = NULL;
            DxilIrExpr expr;
            DxilIrExprId result = DXIL_IR_INVALID_ID;
            DxilIrType base_ty;
            bool base_known = false;
            dxil_parse_advance(parser);
            if (!dxil_parse_expect_ident(parser, &field_text,
                    &field_len, NULL)) {
                return false;
            }
            field_name = dxil_parse_dup_name(parser, field_text,
                field_len);
            if (field_name == NULL) {
                return false;
            }
            memset(&expr, 0, sizeof(expr));
            expr.kind = DXIL_IR_EXPR_MEMBER;
            expr.ty = DXIL_IR_INVALID_ID;
            expr.category = DXIL_IR_EXPR_VALUE;
            expr.base = base;
            expr.field_name = field_name;
            expr.field_index = UINT32_MAX;
            if (dxil_ir_type_get(parser->module,
                    parser->function->exprs[base].ty,
                    &base_ty) == DXIL_OK) {
                base_known = true;
            }
            if (base_known && base_ty.kind == DXIL_IR_TYPE_STRUCT) {
                DxilIrStructDef *def =
                    &parser->module->structs[base_ty.struct_id];
                uint32_t fi = 0;
                bool found = false;
                for (fi = 0; fi < def->field_count; fi++) {
                    if (strlen(def->fields[fi].name) == field_len
                        && memcmp(def->fields[fi].name, field_text,
                            field_len) == 0) {
                        expr.field_index = fi;
                        expr.ty = def->fields[fi].ty;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    dxil_parse_fail_tok(parser,
                        DXIL_DIAG_UNKNOWN_IDENTIFIER,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
            } else if (base_known
                && base_ty.kind == DXIL_IR_TYPE_VECTOR) {
                uint32_t component = UINT32_MAX;
                if (field_len == 1 && field_text[0] == 'x') {
                    component = 0;
                } else if (field_len == 1
                    && field_text[0] == 'y') {
                    component = 1;
                } else if (field_len == 1
                    && field_text[0] == 'z') {
                    component = 2;
                } else if (field_len == 1
                    && field_text[0] == 'w') {
                    component = 3;
                }
                if (component == UINT32_MAX) {
                    dxil_parse_fail_tok(parser,
                        DXIL_DIAG_UNKNOWN_IDENTIFIER,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
                expr.field_index = component;
                expr.ty = base_ty.elem;
            } else if (base_known) {
                dxil_parse_fail_tok(parser,
                    DXIL_DIAG_UNKNOWN_IDENTIFIER,
                    DXIL_DIAG_STAGE_SEMA);
                return false;
            } else {
                if (!dxil_parse_append_expr(parser, &expr,
                        &result)) {
                    return false;
                }
                if (!dxil_parse_add_member_pending(parser,
                        parser->function_index, result, field_text,
                        field_len, parser->tok.offset)) {
                    return false;
                }
                base = result;
                continue;
            }
            if (!dxil_parse_append_expr(parser, &expr, &result)) {
                return false;
            }
            base = result;
            continue;
        }
        if (dxil_parse_is_punct(parser, '[')) {
            DxilIrExprId index_expr = DXIL_IR_INVALID_ID;
            DxilIrExpr expr;
            DxilIrExprId result = DXIL_IR_INVALID_ID;
            DxilIrType base_ty;
            dxil_parse_advance(parser);
            if (!dxil_parse_expr(parser, &index_expr)) {
                return false;
            }
            if (!dxil_parse_expect_punct(parser, ']')) {
                return false;
            }
            memset(&expr, 0, sizeof(expr));
            expr.kind = DXIL_IR_EXPR_INDEX;
            expr.category = DXIL_IR_EXPR_VALUE;
            expr.base = base;
            expr.index_expr = index_expr;
            if (dxil_ir_type_get(parser->module,
                    parser->function->exprs[base].ty,
                    &base_ty) == DXIL_OK
                && (base_ty.kind == DXIL_IR_TYPE_ARRAY
                    || base_ty.kind == DXIL_IR_TYPE_VECTOR)) {
                expr.ty = base_ty.elem;
            } else {
                expr.ty = parser->function->exprs[base].ty;
            }
            if (!dxil_parse_append_expr(parser, &expr, &result)) {
                return false;
            }
            base = result;
            continue;
        }
        break;
    }
    *out_id = base;
    return true;
}
