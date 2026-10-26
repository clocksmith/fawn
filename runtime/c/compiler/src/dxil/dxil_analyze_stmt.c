/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_analyze_internal.h"

#include <string.h>

bool dxil_parse_braced_block(DxilAnalyzeParser *parser,
    DxilIrStmtId *out_id) {
    DxilIrStmtId block_id = DXIL_IR_INVALID_ID;
    DxilIrStmt block;
    uint32_t range_start = 0;
    if (parser->depth >= DXIL_ANALYZE_MAX_DEPTH) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    if (!dxil_parse_expect_punct(parser, '{')) {
        return false;
    }
    parser->depth += 1;
    range_start = parser->function->stmt_child_count;
    while (!dxil_parse_is_punct(parser, '}')) {
        DxilIrStmtId child = DXIL_IR_INVALID_ID;
        if (parser->tok.kind == DXIL_TOK_EOF) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
                DXIL_DIAG_STAGE_PARSER);
            return false;
        }
        if (!dxil_parse_stmt(parser, &child)) {
            return false;
        }
        if (!dxil_parse_append_stmt_child(parser, child)) {
            return false;
        }
    }
    dxil_parse_advance(parser);
    parser->depth -= 1;
    memset(&block, 0, sizeof(block));
    block.kind = DXIL_IR_STMT_BLOCK;
    block.range.start = range_start;
    block.range.len =
        parser->function->stmt_child_count - range_start;
    if (!dxil_parse_append_stmt(parser, &block, &block_id)) {
        return false;
    }
    *out_id = block_id;
    return true;
}

bool dxil_parse_braced_block_with_continuing(
    DxilAnalyzeParser *parser, DxilIrStmtId *out_block,
    bool *out_has_continuing, DxilIrStmtId *out_continuing) {
    DxilIrStmtId block_id = DXIL_IR_INVALID_ID;
    DxilIrStmt block;
    uint32_t range_start = 0;
    bool has_continuing = false;
    DxilIrStmtId continuing = DXIL_IR_INVALID_ID;
    if (parser->depth >= DXIL_ANALYZE_MAX_DEPTH) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    if (!dxil_parse_expect_punct(parser, '{')) {
        return false;
    }
    parser->depth += 1;
    range_start = parser->function->stmt_child_count;
    while (!dxil_parse_is_punct(parser, '}')) {
        DxilIrStmtId child = DXIL_IR_INVALID_ID;
        if (parser->tok.kind == DXIL_TOK_EOF) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
                DXIL_DIAG_STAGE_PARSER);
            return false;
        }
        if (dxil_parse_is_kw(parser, "continuing")) {
            if (has_continuing) {
                dxil_parse_fail_tok(parser,
                    DXIL_DIAG_UNEXPECTED_TOKEN,
                    DXIL_DIAG_STAGE_PARSER);
                return false;
            }
            dxil_parse_advance(parser);
            if (!dxil_parse_braced_block(parser, &continuing)) {
                return false;
            }
            has_continuing = true;
            break;
        }
        if (!dxil_parse_stmt(parser, &child)) {
            return false;
        }
        if (!dxil_parse_append_stmt_child(parser, child)) {
            return false;
        }
    }
    if (!dxil_parse_expect_punct(parser, '}')) {
        return false;
    }
    parser->depth -= 1;
    memset(&block, 0, sizeof(block));
    block.kind = DXIL_IR_STMT_BLOCK;
    block.range.start = range_start;
    block.range.len =
        parser->function->stmt_child_count - range_start;
    if (!dxil_parse_append_stmt(parser, &block, &block_id)) {
        return false;
    }
    *out_block = block_id;
    *out_has_continuing = has_continuing;
    *out_continuing = continuing;
    return true;
}

static bool parse_local_decl(DxilAnalyzeParser *parser,
    DxilIrStmtId *out_id) {
    bool is_const = false;
    const char *name_text = NULL;
    size_t name_len = 0;
    const char *name = NULL;
    DxilIrTypeId local_ty = DXIL_IR_INVALID_ID;
    bool has_type = false;
    DxilIrExprId initializer = DXIL_IR_INVALID_ID;
    bool has_initializer = false;
    DxilIrLocal local;
    uint32_t local_index = 0;
    DxilIrStmt stmt;
    uint32_t i = 0;
    is_const = !dxil_parse_is_kw(parser, "var");
    dxil_parse_advance(parser);
    if (!dxil_parse_expect_ident(parser, &name_text, &name_len,
            NULL)) {
        return false;
    }
    for (i = 0; i < parser->function->param_count; i++) {
        if (strlen(parser->function->params[i].name) == name_len
            && memcmp(parser->function->params[i].name, name_text,
                name_len) == 0) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_DUPLICATE_SYMBOL,
                DXIL_DIAG_STAGE_SEMA);
            return false;
        }
    }
    for (i = 0; i < parser->function->local_count; i++) {
        if (strlen(parser->function->locals[i].name) == name_len
            && memcmp(parser->function->locals[i].name, name_text,
                name_len) == 0) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_DUPLICATE_SYMBOL,
                DXIL_DIAG_STAGE_SEMA);
            return false;
        }
    }
    if (dxil_parse_accept_punct(parser, ':')) {
        if (!dxil_parse_type(parser, &local_ty)) {
            return false;
        }
        has_type = true;
    }
    if (dxil_parse_accept_punct(parser, '=')) {
        if (!dxil_parse_expr(parser, &initializer)) {
            return false;
        }
        has_initializer = true;
    }
    if (!has_type && !has_initializer) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_WGSL,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    if (!has_type) {
        local_ty =
            parser->function->exprs[initializer].ty;
    }
    if (!dxil_parse_expect_punct(parser, ';')) {
        return false;
    }
    name = dxil_parse_dup_name(parser, name_text, name_len);
    if (name == NULL) {
        return false;
    }
    if (parser->function->local_count >= DXIL_IR_MAX_LOCALS) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    local_index = parser->function->local_count;
    memset(&local, 0, sizeof(local));
    local.name = name;
    local.ty = local_ty;
    local.mutable = !is_const;
    parser->function->locals[local_index] = local;
    parser->function->local_count += 1;
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = DXIL_IR_STMT_LOCAL_DECL;
    stmt.local = local_index;
    stmt.has_initializer = has_initializer;
    stmt.initializer = initializer;
    stmt.is_const = is_const;
    if (!dxil_parse_append_stmt(parser, &stmt, out_id)) {
        return false;
    }
    return true;
}

static bool parse_return(DxilAnalyzeParser *parser,
    DxilIrStmtId *out_id) {
    DxilIrStmt stmt;
    DxilIrExprId value = DXIL_IR_INVALID_ID;
    bool has_value = false;
    dxil_parse_advance(parser);
    if (!dxil_parse_is_punct(parser, ';')) {
        if (!dxil_parse_expr(parser, &value)) {
            return false;
        }
        has_value = true;
    }
    if (!dxil_parse_expect_punct(parser, ';')) {
        return false;
    }
    if (has_value
        && parser->function->return_type == parser->void_ty) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_TYPE_MISMATCH,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    if (!has_value
        && parser->function->return_type != parser->void_ty) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_TYPE_MISMATCH,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = DXIL_IR_STMT_RETURN;
    stmt.has_return_value = has_value;
    stmt.return_value = value;
    if (!dxil_parse_append_stmt(parser, &stmt, out_id)) {
        return false;
    }
    return true;
}

static bool parse_if(DxilAnalyzeParser *parser,
    DxilIrStmtId *out_id) {
    DxilIrStmt stmt;
    DxilIrExprId cond = DXIL_IR_INVALID_ID;
    DxilIrStmtId then_block = DXIL_IR_INVALID_ID;
    DxilIrStmtId else_block = DXIL_IR_INVALID_ID;
    bool has_else_block = false;
    dxil_parse_advance(parser);
    if (!dxil_parse_expr(parser, &cond)) {
        return false;
    }
    if (!dxil_parse_braced_block(parser, &then_block)) {
        return false;
    }
    if (dxil_parse_accept_kw(parser, "else")) {
        has_else_block = true;
        if (dxil_parse_is_kw(parser, "if")) {
            if (!parse_if(parser, &else_block)) {
                return false;
            }
        } else if (!dxil_parse_braced_block(parser, &else_block)) {
            return false;
        }
    }
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = DXIL_IR_STMT_IF;
    stmt.cond = cond;
    stmt.then_block = then_block;
    stmt.has_else_block = has_else_block;
    stmt.else_block = else_block;
    if (!dxil_parse_append_stmt(parser, &stmt, out_id)) {
        return false;
    }
    return true;
}

static bool parse_loop(DxilAnalyzeParser *parser,
    DxilIrStmtId *out_id, bool is_while) {
    DxilIrStmt stmt;
    DxilIrExprId cond = DXIL_IR_INVALID_ID;
    DxilIrStmtId body = DXIL_IR_INVALID_ID;
    bool has_continuing = false;
    DxilIrStmtId continuing = DXIL_IR_INVALID_ID;
    dxil_parse_advance(parser);
    if (is_while) {
        if (!dxil_parse_expr(parser, &cond)) {
            return false;
        }
    }
    if (!dxil_parse_braced_block_with_continuing(parser, &body,
            &has_continuing, &continuing)) {
        return false;
    }
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = DXIL_IR_STMT_LOOP;
    stmt.loop_kind = is_while ? DXIL_IR_LOOP_WHILE
        : DXIL_IR_LOOP_LOOP;
    stmt.has_cond = is_while;
    stmt.cond = cond;
    stmt.body = body;
    stmt.has_continuing = has_continuing;
    stmt.continuing = continuing;
    if (!dxil_parse_append_stmt(parser, &stmt, out_id)) {
        return false;
    }
    return true;
}

static bool parse_switch(DxilAnalyzeParser *parser,
    DxilIrStmtId *out_id) {
    DxilIrStmt stmt;
    DxilIrExprId scrutinee = DXIL_IR_INVALID_ID;
    uint32_t case_start = 0;
    uint32_t case_count = 0;
    dxil_parse_advance(parser);
    if (!dxil_parse_expr(parser, &scrutinee)) {
        return false;
    }
    if (!dxil_parse_expect_punct(parser, '{')) {
        return false;
    }
    case_start = parser->function->switch_case_count;
    while (!dxil_parse_is_punct(parser, '}')) {
        DxilIrSwitchCase switch_case;
        uint32_t selector_start = 0;
        bool is_default = false;
        DxilIrStmtId body = DXIL_IR_INVALID_ID;
        if (parser->tok.kind == DXIL_TOK_EOF) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
                DXIL_DIAG_STAGE_PARSER);
            return false;
        }
        memset(&switch_case, 0, sizeof(switch_case));
        selector_start = parser->function->switch_selector_count;
        if (dxil_parse_accept_kw(parser, "case")) {
            for (;;) {
                DxilIrExprId selector = DXIL_IR_INVALID_ID;
                if (!dxil_parse_expr(parser, &selector)) {
                    return false;
                }
                if (!dxil_parse_append_selector(parser, selector)) {
                    return false;
                }
                if (!dxil_parse_accept_punct(parser, ',')) {
                    break;
                }
            }
        } else if (dxil_parse_accept_kw(parser, "default")) {
            is_default = true;
        } else {
            dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
                DXIL_DIAG_STAGE_PARSER);
            return false;
        }
        if (!dxil_parse_expect_punct(parser, ':')) {
            return false;
        }
        if (!dxil_parse_braced_block(parser, &body)) {
            return false;
        }
        switch_case.selectors.start = selector_start;
        switch_case.selectors.len =
            parser->function->switch_selector_count - selector_start;
        switch_case.body = body;
        switch_case.is_default = is_default;
        if (!dxil_parse_append_switch_case(parser, &switch_case)) {
            return false;
        }
        case_count += 1;
    }
    dxil_parse_advance(parser);
    if (case_count == 0) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_WGSL,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = DXIL_IR_STMT_SWITCH;
    stmt.switch_expr = scrutinee;
    stmt.cases.start = case_start;
    stmt.cases.len = case_count;
    if (!dxil_parse_append_stmt(parser, &stmt, out_id)) {
        return false;
    }
    return true;
}

static bool parse_simple_stmt(DxilAnalyzeParser *parser,
    DxilIrStmtId *out_id, DxilIrStmtKind kind) {
    DxilIrStmt stmt;
    dxil_parse_advance(parser);
    if (!dxil_parse_expect_punct(parser, ';')) {
        return false;
    }
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = kind;
    if (!dxil_parse_append_stmt(parser, &stmt, out_id)) {
        return false;
    }
    return true;
}

bool dxil_parse_stmt(DxilAnalyzeParser *parser,
    DxilIrStmtId *out_id) {
    if (dxil_parse_is_punct(parser, '{')) {
        return dxil_parse_braced_block(parser, out_id);
    }
    if (dxil_parse_is_kw(parser, "let")
        || dxil_parse_is_kw(parser, "var")
        || dxil_parse_is_kw(parser, "const")) {
        return parse_local_decl(parser, out_id);
    }
    if (dxil_parse_is_kw(parser, "return")) {
        return parse_return(parser, out_id);
    }
    if (dxil_parse_is_kw(parser, "if")) {
        return parse_if(parser, out_id);
    }
    if (dxil_parse_is_kw(parser, "loop")) {
        return parse_loop(parser, out_id, false);
    }
    if (dxil_parse_is_kw(parser, "while")) {
        return parse_loop(parser, out_id, true);
    }
    if (dxil_parse_is_kw(parser, "for")) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_UNSUPPORTED_CONSTRUCT,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    if (dxil_parse_is_kw(parser, "switch")) {
        return parse_switch(parser, out_id);
    }
    if (dxil_parse_is_kw(parser, "break")) {
        return parse_simple_stmt(parser, out_id,
            DXIL_IR_STMT_BREAK);
    }
    if (dxil_parse_is_kw(parser, "continue")) {
        return parse_simple_stmt(parser, out_id,
            DXIL_IR_STMT_CONTINUE);
    }
    if (dxil_parse_is_kw(parser, "discard")) {
        return parse_simple_stmt(parser, out_id,
            DXIL_IR_STMT_DISCARD);
    }
    if (dxil_parse_is_kw(parser, "continuing")) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    {
        DxilIrExprId expr = DXIL_IR_INVALID_ID;
        if (!dxil_parse_expr(parser, &expr)) {
            return false;
        }
        if (dxil_parse_is_punct(parser, '=')
            || dxil_parse_is_punct(parser, DXIL_P_PLUSEQ)
            || dxil_parse_is_punct(parser, DXIL_P_MINUSEQ)
            || dxil_parse_is_punct(parser, DXIL_P_MULEQ)
            || dxil_parse_is_punct(parser, DXIL_P_DIVEQ)
            || dxil_parse_is_punct(parser, DXIL_P_MODEQ)
            || dxil_parse_is_punct(parser, DXIL_P_ANDEQ)
            || dxil_parse_is_punct(parser, DXIL_P_OREQ)
            || dxil_parse_is_punct(parser, DXIL_P_XOREQ)
            || dxil_parse_is_punct(parser, DXIL_P_SHLEQ)
            || dxil_parse_is_punct(parser, DXIL_P_SHREQ)) {
            int punct = parser->tok.punct;
            DxilIrAssignOp op = DXIL_IR_ASSIGN_ASSIGN;
            DxilIrExprId rhs = DXIL_IR_INVALID_ID;
            DxilIrStmt stmt;
            switch (punct) {
            case '=':
                op = DXIL_IR_ASSIGN_ASSIGN;
                break;
            case DXIL_P_PLUSEQ:
                op = DXIL_IR_ASSIGN_ADD;
                break;
            case DXIL_P_MINUSEQ:
                op = DXIL_IR_ASSIGN_SUB;
                break;
            case DXIL_P_MULEQ:
                op = DXIL_IR_ASSIGN_MUL;
                break;
            case DXIL_P_DIVEQ:
                op = DXIL_IR_ASSIGN_DIV;
                break;
            case DXIL_P_MODEQ:
                op = DXIL_IR_ASSIGN_REM;
                break;
            case DXIL_P_ANDEQ:
                op = DXIL_IR_ASSIGN_BIT_AND;
                break;
            case DXIL_P_OREQ:
                op = DXIL_IR_ASSIGN_BIT_OR;
                break;
            case DXIL_P_XOREQ:
                op = DXIL_IR_ASSIGN_BIT_XOR;
                break;
            case DXIL_P_SHLEQ:
                op = DXIL_IR_ASSIGN_SHIFT_LEFT;
                break;
            default:
                op = DXIL_IR_ASSIGN_SHIFT_RIGHT;
                break;
            }
            dxil_parse_advance(parser);
            if (!dxil_parse_expr(parser, &rhs)) {
                return false;
            }
            if (!dxil_parse_expect_punct(parser, ';')) {
                return false;
            }
            memset(&stmt, 0, sizeof(stmt));
            stmt.kind = DXIL_IR_STMT_ASSIGN;
            stmt.assign_op = op;
            stmt.assign_lhs = expr;
            stmt.assign_rhs = rhs;
            if (!dxil_parse_append_stmt(parser, &stmt, out_id)) {
                return false;
            }
            return true;
        }
        if (!dxil_parse_expect_punct(parser, ';')) {
            return false;
        }
        {
            DxilIrStmt stmt;
            memset(&stmt, 0, sizeof(stmt));
            stmt.kind = DXIL_IR_STMT_EXPR;
            stmt.expr = expr;
            if (!dxil_parse_append_stmt(parser, &stmt, out_id)) {
                return false;
            }
        }
        return true;
    }
}
