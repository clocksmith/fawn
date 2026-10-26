/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_analyze.h"

#include "dxil_analyze_internal.h"

#include <stdlib.h>
#include <string.h>

void dxil_parse_fail_at(DxilAnalyzeParser *parser, DxilDiagKind kind,
    DxilDiagnosticStage stage, size_t offset) {
    if (parser->failed) {
        return;
    }
    parser->failed = true;
    parser->error_kind = kind;
    dxil_diagnostic_set(parser->diagnostic, stage, kind, parser->src,
        parser->len, offset);
}

void dxil_parse_fail_tok(DxilAnalyzeParser *parser, DxilDiagKind kind,
    DxilDiagnosticStage stage) {
    dxil_parse_fail_at(parser, kind, stage, parser->tok.offset);
}

static bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static bool ident_equals(const DxilAnalyzeToken *tok, const char *text) {
    size_t len = strlen(text);
    return tok->kind == DXIL_TOK_IDENT && tok->len == len
        && memcmp(tok->text, text, len) == 0;
}

static void skip_space_and_comments(DxilAnalyzeParser *parser) {
    for (;;) {
        char c = '\0';
        if (parser->pos >= parser->len) {
            return;
        }
        c = parser->src[parser->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            parser->pos += 1;
            continue;
        }
        if (c == '/' && parser->pos + 1 < parser->len
            && parser->src[parser->pos + 1] == '/') {
            while (parser->pos < parser->len
                && parser->src[parser->pos] != '\n') {
                parser->pos += 1;
            }
            continue;
        }
        if (c == '/' && parser->pos + 1 < parser->len
            && parser->src[parser->pos + 1] == '*') {
            size_t start = parser->pos;
            parser->pos += 2;
            while (parser->pos + 1 < parser->len
                && !(parser->src[parser->pos] == '*'
                    && parser->src[parser->pos + 1] == '/')) {
                parser->pos += 1;
            }
            if (parser->pos + 1 >= parser->len) {
                dxil_parse_fail_at(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
                    DXIL_DIAG_STAGE_PARSER, start);
                return;
            }
            parser->pos += 2;
            continue;
        }
        return;
    }
}

static void lex_token(DxilAnalyzeParser *parser) {
    DxilAnalyzeToken *tok = &parser->tok;
    char c = '\0';
    memset(tok, 0, sizeof(*tok));
    skip_space_and_comments(parser);
    if (parser->failed || parser->pos >= parser->len) {
        tok->kind = DXIL_TOK_EOF;
        tok->offset = parser->len;
        return;
    }
    tok->offset = parser->pos;
    c = parser->src[parser->pos];
    if (is_ident_start(c)) {
        size_t start = parser->pos;
        while (parser->pos < parser->len
            && is_ident_char(parser->src[parser->pos])) {
            parser->pos += 1;
        }
        tok->kind = DXIL_TOK_IDENT;
        tok->text = parser->src + start;
        tok->len = parser->pos - start;
        return;
    }
    if (c >= '0' && c <= '9') {
        size_t start = parser->pos;
        bool is_float = false;
        bool is_hex = false;
        char suffix = '\0';
        size_t digits_end = 0;
        if (c == '0' && parser->pos + 1 < parser->len
            && (parser->src[parser->pos + 1] == 'x'
                || parser->src[parser->pos + 1] == 'X')) {
            is_hex = true;
            parser->pos += 2;
            while (parser->pos < parser->len) {
                char h = parser->src[parser->pos];
                if ((h >= '0' && h <= '9')
                    || (h >= 'a' && h <= 'f')
                    || (h >= 'A' && h <= 'F')) {
                    parser->pos += 1;
                } else {
                    break;
                }
            }
        } else {
            while (parser->pos < parser->len
                && parser->src[parser->pos] >= '0'
                && parser->src[parser->pos] <= '9') {
                parser->pos += 1;
            }
            if (parser->pos < parser->len
                && parser->src[parser->pos] == '.'
                && parser->pos + 1 < parser->len
                && parser->src[parser->pos + 1] >= '0'
                && parser->src[parser->pos + 1] <= '9') {
                is_float = true;
                parser->pos += 1;
                while (parser->pos < parser->len
                    && parser->src[parser->pos] >= '0'
                    && parser->src[parser->pos] <= '9') {
                    parser->pos += 1;
                }
            }
            if (parser->pos < parser->len
                && (parser->src[parser->pos] == 'e'
                    || parser->src[parser->pos] == 'E')) {
                size_t save = parser->pos;
                size_t p = parser->pos + 1;
                if (p < parser->len
                    && (parser->src[p] == '+'
                        || parser->src[p] == '-')) {
                    p += 1;
                }
                if (p < parser->len && parser->src[p] >= '0'
                    && parser->src[p] <= '9') {
                    is_float = true;
                    parser->pos = p;
                    while (parser->pos < parser->len
                        && parser->src[parser->pos] >= '0'
                        && parser->src[parser->pos] <= '9') {
                        parser->pos += 1;
                    }
                } else {
                    parser->pos = save;
                }
            }
        }
        digits_end = parser->pos;
        if (parser->pos < parser->len) {
            char s = parser->src[parser->pos];
            if (s == 'u' || s == 'i' || s == 'f' || s == 'h') {
                if (parser->pos + 1 >= parser->len
                    || !is_ident_char(parser->src[parser->pos + 1])) {
                    suffix = s;
                    parser->pos += 1;
                }
            }
        }
        if (parser->pos < parser->len
            && is_ident_char(parser->src[parser->pos])) {
            dxil_parse_fail_at(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
                DXIL_DIAG_STAGE_PARSER, start);
            tok->kind = DXIL_TOK_EOF;
            return;
        }
        tok->text = parser->src + start;
        tok->len = parser->pos - start;
        if (!is_float && suffix != 'f' && suffix != 'h') {
            uint64_t value = 0;
            size_t i = 0;
            size_t digits_start = start;
            uint64_t base = 10;
            if (is_hex) {
                base = 16;
                digits_start = start + 2;
            }
            for (i = digits_start; i < digits_end; i++) {
                char h = parser->src[i];
                uint64_t digit = 0;
                if (h >= '0' && h <= '9') {
                    digit = (uint64_t)(h - '0');
                } else if (h >= 'a' && h <= 'f') {
                    digit = (uint64_t)(h - 'a' + 10);
                } else if (h >= 'A' && h <= 'F') {
                    digit = (uint64_t)(h - 'A' + 10);
                }
                if (value > (UINT64_MAX - digit) / base) {
                    dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_WGSL,
                        DXIL_DIAG_STAGE_PARSER, start);
                    tok->kind = DXIL_TOK_EOF;
                    return;
                }
                value = value * base + digit;
            }
            if (suffix == 'u') {
                tok->lit_scalar = DXIL_IR_SCALAR_U32;
            } else if (suffix == 'i') {
                tok->lit_scalar = DXIL_IR_SCALAR_I32;
            } else {
                tok->lit_scalar = DXIL_IR_SCALAR_ABSTRACT_INT;
            }
            tok->kind = DXIL_TOK_INT;
            tok->int_value = value;
            return;
        }
        if (suffix == 'u' || suffix == 'i') {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_WGSL,
                DXIL_DIAG_STAGE_PARSER, start);
            tok->kind = DXIL_TOK_EOF;
            return;
        }
        {
            char buf[128];
            size_t num_len = digits_end - start;
            if (num_len >= sizeof(buf)) {
                dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_WGSL,
                    DXIL_DIAG_STAGE_PARSER, start);
                tok->kind = DXIL_TOK_EOF;
                return;
            }
            memcpy(buf, parser->src + start, num_len);
            buf[num_len] = '\0';
            tok->float_value = strtod(buf, NULL);
            if (suffix == 'f') {
                tok->lit_scalar = DXIL_IR_SCALAR_F32;
            } else if (suffix == 'h') {
                tok->lit_scalar = DXIL_IR_SCALAR_F16;
            } else {
                tok->lit_scalar = DXIL_IR_SCALAR_ABSTRACT_FLOAT;
            }
            tok->kind = DXIL_TOK_FLOAT;
        }
        return;
    }
    {
        static const struct DxilAnalyzeMulti {
            const char *text;
            int punct;
        } multi[] = {
            { "->", DXIL_P_ARROW },
            { "==", DXIL_P_EQEQ },
            { "!=", DXIL_P_NOTEQ },
            { "<=", DXIL_P_LTE },
            { ">=", DXIL_P_GTE },
            { "&&", DXIL_P_ANDAND },
            { "||", DXIL_P_OROR },
            { "<<=", DXIL_P_SHLEQ },
            { ">>=", DXIL_P_SHREQ },
            { "<<", DXIL_P_SHL },
            { ">>", DXIL_P_SHR },
            { "+=", DXIL_P_PLUSEQ },
            { "-=", DXIL_P_MINUSEQ },
            { "*=", DXIL_P_MULEQ },
            { "/=", DXIL_P_DIVEQ },
            { "%=", DXIL_P_MODEQ },
            { "&=", DXIL_P_ANDEQ },
            { "|=", DXIL_P_OREQ },
            { "^=", DXIL_P_XOREQ },
        };
        size_t i = 0;
        for (i = 0; i < sizeof(multi) / sizeof(multi[0]); i++) {
            size_t mlen = strlen(multi[i].text);
            if (parser->pos + mlen <= parser->len
                && memcmp(parser->src + parser->pos, multi[i].text,
                    mlen) == 0) {
                tok->kind = DXIL_TOK_PUNCT;
                tok->punct = multi[i].punct;
                tok->text = parser->src + parser->pos;
                tok->len = mlen;
                parser->pos += mlen;
                return;
            }
        }
    }
    if (strchr("(){}[],;:.<>+-*/%&|^!~=@", c) != NULL) {
        tok->kind = DXIL_TOK_PUNCT;
        tok->punct = (unsigned char)c;
        tok->text = parser->src + parser->pos;
        tok->len = 1;
        parser->pos += 1;
        return;
    }
    dxil_parse_fail_at(parser, DXIL_DIAG_UNEXPECTED_TOKEN, DXIL_DIAG_STAGE_PARSER,
        parser->pos);
    tok->kind = DXIL_TOK_EOF;
}

void dxil_parse_advance(DxilAnalyzeParser *parser) {
    if (parser->failed) {
        parser->tok.kind = DXIL_TOK_EOF;
        return;
    }
    lex_token(parser);
}

bool dxil_parse_is_punct(DxilAnalyzeParser *parser, int punct) {
    return parser->tok.kind == DXIL_TOK_PUNCT
        && parser->tok.punct == punct;
}

bool dxil_parse_accept_punct(DxilAnalyzeParser *parser, int punct) {
    if (!dxil_parse_is_punct(parser, punct)) {
        return false;
    }
    dxil_parse_advance(parser);
    return true;
}

bool dxil_parse_expect_punct(DxilAnalyzeParser *parser, int punct) {
    if (parser->failed) {
        return false;
    }
    if (!dxil_parse_is_punct(parser, punct)) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    dxil_parse_advance(parser);
    return true;
}

bool dxil_parse_is_kw(DxilAnalyzeParser *parser, const char *text) {
    return ident_equals(&parser->tok, text);
}

bool dxil_parse_accept_kw(DxilAnalyzeParser *parser, const char *text) {
    if (!dxil_parse_is_kw(parser, text)) {
        return false;
    }
    dxil_parse_advance(parser);
    return true;
}

bool dxil_parse_expect_ident(DxilAnalyzeParser *parser,
    const char **out_text, size_t *out_len, size_t *out_offset) {
    if (parser->failed) {
        return false;
    }
    if (parser->tok.kind != DXIL_TOK_IDENT) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    *out_text = parser->tok.text;
    *out_len = parser->tok.len;
    if (out_offset != NULL) {
        *out_offset = parser->tok.offset;
    }
    dxil_parse_advance(parser);
    return true;
}

bool dxil_parse_expect_int_value(DxilAnalyzeParser *parser,
    uint64_t *out_value) {
    if (parser->failed) {
        return false;
    }
    if (parser->tok.kind != DXIL_TOK_INT) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
            DXIL_DIAG_STAGE_PARSER);
        return false;
    }
    *out_value = parser->tok.int_value;
    dxil_parse_advance(parser);
    return true;
}

const char *dxil_parse_dup_name(DxilAnalyzeParser *parser,
    const char *text, size_t len) {
    char *dest = NULL;
    if (parser->failed) {
        return NULL;
    }
    if (parser->name_pos + len + 1 > parser->name_buf_len) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return NULL;
    }
    dest = parser->name_buf + parser->name_pos;
    memcpy(dest, text, len);
    dest[len] = '\0';
    parser->name_pos += len + 1;
    return dest;
}

static DxilStatus intern_scalar(DxilAnalyzeParser *parser,
    DxilIrScalar scalar, DxilIrTypeId *out_id) {
    DxilIrType type;
    memset(&type, 0, sizeof(type));
    type.kind = DXIL_IR_TYPE_SCALAR;
    type.scalar = scalar;
    if (dxil_ir_intern_type(parser->module, &type, out_id)
        != DXIL_OK) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return DXIL_OUTPUT_TOO_LARGE;
    }
    return DXIL_OK;
}

static bool init_cached_types(DxilAnalyzeParser *parser) {
    if (intern_scalar(parser, DXIL_IR_SCALAR_VOID, &parser->void_ty)
            != DXIL_OK
        || intern_scalar(parser, DXIL_IR_SCALAR_BOOL,
            &parser->bool_ty) != DXIL_OK
        || intern_scalar(parser, DXIL_IR_SCALAR_I32, &parser->i32_ty)
            != DXIL_OK
        || intern_scalar(parser, DXIL_IR_SCALAR_U32, &parser->u32_ty)
            != DXIL_OK
        || intern_scalar(parser, DXIL_IR_SCALAR_F32, &parser->f32_ty)
            != DXIL_OK
        || intern_scalar(parser, DXIL_IR_SCALAR_F16, &parser->f16_ty)
            != DXIL_OK
        || intern_scalar(parser, DXIL_IR_SCALAR_ABSTRACT_INT,
            &parser->aint_ty) != DXIL_OK
        || intern_scalar(parser, DXIL_IR_SCALAR_ABSTRACT_FLOAT,
            &parser->afloat_ty) != DXIL_OK) {
        return false;
    }
    return true;
}

DxilIrTypeId dxil_parse_scalar_type_id(DxilAnalyzeParser *parser,
    DxilIrScalar scalar) {
    switch (scalar) {
        case DXIL_IR_SCALAR_VOID:
            return parser->void_ty;
        case DXIL_IR_SCALAR_BOOL:
            return parser->bool_ty;
        case DXIL_IR_SCALAR_I32:
            return parser->i32_ty;
        case DXIL_IR_SCALAR_U32:
            return parser->u32_ty;
        case DXIL_IR_SCALAR_F32:
            return parser->f32_ty;
        case DXIL_IR_SCALAR_F16:
            return parser->f16_ty;
        case DXIL_IR_SCALAR_ABSTRACT_INT:
            return parser->aint_ty;
        case DXIL_IR_SCALAR_ABSTRACT_FLOAT:
            return parser->afloat_ty;
    }
    return DXIL_IR_INVALID_ID;
}

bool dxil_parse_scalar_name_to_enum(const char *text, size_t len,
    DxilIrScalar *out) {
    static const struct DxilAnalyzeScalarName {
        const char *name;
        DxilIrScalar scalar;
    } table[] = {
        { "void", DXIL_IR_SCALAR_VOID },
        { "bool", DXIL_IR_SCALAR_BOOL },
        { "i32", DXIL_IR_SCALAR_I32 },
        { "u32", DXIL_IR_SCALAR_U32 },
        { "f32", DXIL_IR_SCALAR_F32 },
        { "f16", DXIL_IR_SCALAR_F16 },
        { "abstract_int", DXIL_IR_SCALAR_ABSTRACT_INT },
        { "abstract_float", DXIL_IR_SCALAR_ABSTRACT_FLOAT },
    };
    size_t i = 0;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strlen(table[i].name) == len
            && memcmp(text, table[i].name, len) == 0) {
            *out = table[i].scalar;
            return true;
        }
    }
    return false;
}

int dxil_parse_find_struct(DxilAnalyzeParser *parser, const char *text,
    size_t len) {
    uint32_t i = 0;
    for (i = 0; i < parser->module->struct_count; i++) {
        const char *name = parser->module->structs[i].name;
        if (strlen(name) == len && memcmp(name, text, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool dxil_parse_builtin_name_to_enum(const char *text, size_t len,
    DxilIrBuiltin *out) {
    static const struct DxilAnalyzeBuiltinName {
        const char *name;
        DxilIrBuiltin builtin;
    } table[] = {
        { "position", DXIL_IR_BUILTIN_POSITION },
        { "frag_depth", DXIL_IR_BUILTIN_FRAG_DEPTH },
        { "front_facing", DXIL_IR_BUILTIN_FRONT_FACING },
        { "global_invocation_id",
            DXIL_IR_BUILTIN_GLOBAL_INVOCATION_ID },
        { "local_invocation_id",
            DXIL_IR_BUILTIN_LOCAL_INVOCATION_ID },
        { "local_invocation_index",
            DXIL_IR_BUILTIN_LOCAL_INVOCATION_INDEX },
        { "workgroup_id", DXIL_IR_BUILTIN_WORKGROUP_ID },
        { "num_workgroups", DXIL_IR_BUILTIN_NUM_WORKGROUPS },
        { "sample_index", DXIL_IR_BUILTIN_SAMPLE_INDEX },
        { "sample_mask", DXIL_IR_BUILTIN_SAMPLE_MASK },
        { "vertex_index", DXIL_IR_BUILTIN_VERTEX_INDEX },
        { "instance_index", DXIL_IR_BUILTIN_INSTANCE_INDEX },
        { "subgroup_size", DXIL_IR_BUILTIN_SUBGROUP_SIZE },
        { "subgroup_invocation_id",
            DXIL_IR_BUILTIN_SUBGROUP_INVOCATION_ID },
        { "clip_distances", DXIL_IR_BUILTIN_CLIP_DISTANCES },
        { "primitive_index", DXIL_IR_BUILTIN_PRIMITIVE_INDEX },
    };
    size_t i = 0;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strlen(table[i].name) == len
            && memcmp(text, table[i].name, len) == 0) {
            *out = table[i].builtin;
            return true;
        }
    }
    return false;
}


bool dxil_parse_append_expr(DxilAnalyzeParser *parser,
    const DxilIrExpr *expr, DxilIrExprId *out_id) {
    if (dxil_ir_append_expr(parser->function, expr, out_id)
        != DXIL_OK) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    return true;
}

bool dxil_parse_append_stmt(DxilAnalyzeParser *parser,
    const DxilIrStmt *stmt, DxilIrStmtId *out_id) {
    if (dxil_ir_append_stmt(parser->function, stmt, out_id)
        != DXIL_OK) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    return true;
}

bool dxil_parse_append_expr_arg(DxilAnalyzeParser *parser,
    DxilIrExprId arg) {
    if (dxil_ir_append_expr_arg(parser->function, arg) != DXIL_OK) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    return true;
}

bool dxil_parse_append_stmt_child(DxilAnalyzeParser *parser,
    DxilIrStmtId child) {
    if (dxil_ir_append_stmt_child(parser->function, child)
        != DXIL_OK) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    return true;
}

bool dxil_parse_append_selector(DxilAnalyzeParser *parser,
    DxilIrExprId selector) {
    if (dxil_ir_append_selector(parser->function, selector)
        != DXIL_OK) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    return true;
}

bool dxil_parse_append_switch_case(DxilAnalyzeParser *parser,
    const DxilIrSwitchCase *switch_case) {
    if (dxil_ir_append_switch_case(parser->function, switch_case)
        != DXIL_OK) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    return true;
}

static bool fixup_pending_globals(DxilAnalyzeParser *parser) {
    uint32_t i = 0;
    for (i = 0; i < parser->pending_count; i++) {
        DxilAnalyzePending *pending = &parser->pending[i];
        DxilIrFunction *function =
            &parser->module->functions[pending->function_index];
        uint32_t global_index = 0;
        DxilIrExpr *expr = NULL;
        if (!dxil_parse_find_global(parser, pending->name,
                pending->name_len, &global_index)) {
            dxil_parse_fail_at(parser, DXIL_DIAG_UNKNOWN_IDENTIFIER,
                DXIL_DIAG_STAGE_SEMA, pending->offset);
            return false;
        }
        expr = &function->exprs[pending->expr_id];
        expr->index = global_index;
        expr->ty = parser->module->globals[global_index].ty;
    }
    return true;
}

static bool fixup_member_indices(DxilAnalyzeParser *parser) {
    uint32_t i = 0;
    for (i = 0; i < parser->member_pending_count; i++) {
        DxilAnalyzePending *pending = &parser->member_pending[i];
        DxilIrFunction *function =
            &parser->module->functions[pending->function_index];
        DxilIrExpr *expr = &function->exprs[pending->expr_id];
        DxilIrExpr *base = &function->exprs[expr->base];
        DxilIrType base_ty;
        if (dxil_ir_type_get(parser->module, base->ty, &base_ty)
            != DXIL_OK) {
            parser->failed = true;
            parser->error_kind = DXIL_DIAG_UNKNOWN_IDENTIFIER;
            dxil_diagnostic_set(parser->diagnostic,
                DXIL_DIAG_STAGE_SEMA, DXIL_DIAG_UNKNOWN_IDENTIFIER,
                NULL, 0, 0);
            return false;
        }
        if (base_ty.kind == DXIL_IR_TYPE_STRUCT) {
            DxilIrStructDef *def =
                &parser->module->structs[base_ty.struct_id];
            uint32_t fi = 0;
            bool found = false;
            for (fi = 0; fi < def->field_count; fi++) {
                if (strlen(def->fields[fi].name)
                        == pending->name_len
                    && memcmp(def->fields[fi].name, pending->name,
                        pending->name_len) == 0) {
                    expr->field_index = fi;
                    expr->ty = def->fields[fi].ty;
                    found = true;
                    break;
                }
            }
            if (!found) {
                parser->failed = true;
                parser->error_kind = DXIL_DIAG_UNKNOWN_IDENTIFIER;
                dxil_diagnostic_set(parser->diagnostic,
                    DXIL_DIAG_STAGE_SEMA,
                    DXIL_DIAG_UNKNOWN_IDENTIFIER, NULL, 0, 0);
                return false;
            }
        } else if (base_ty.kind == DXIL_IR_TYPE_VECTOR) {
            uint32_t component = UINT32_MAX;
            if (pending->name_len == 1
                && pending->name[0] == 'x') {
                component = 0;
            } else if (pending->name_len == 1
                && pending->name[0] == 'y') {
                component = 1;
            } else if (pending->name_len == 1
                && pending->name[0] == 'z') {
                component = 2;
            } else if (pending->name_len == 1
                && pending->name[0] == 'w') {
                component = 3;
            }
            if (component == UINT32_MAX) {
                parser->failed = true;
                parser->error_kind = DXIL_DIAG_UNKNOWN_IDENTIFIER;
                dxil_diagnostic_set(parser->diagnostic,
                    DXIL_DIAG_STAGE_SEMA,
                    DXIL_DIAG_UNKNOWN_IDENTIFIER, NULL, 0, 0);
                return false;
            }
            expr->field_index = component;
            expr->ty = base_ty.elem;
        } else {
            parser->failed = true;
            parser->error_kind = DXIL_DIAG_UNKNOWN_IDENTIFIER;
            dxil_diagnostic_set(parser->diagnostic,
                DXIL_DIAG_STAGE_SEMA, DXIL_DIAG_UNKNOWN_IDENTIFIER,
                NULL, 0, 0);
            return false;
        }
    }
    return true;
}

static void fixup_call_kinds(DxilAnalyzeParser *parser) {
    uint32_t fi = 0;
    for (fi = 0; fi < parser->module->function_count; fi++) {
        DxilIrFunction *function = &parser->module->functions[fi];
        uint32_t ei = 0;
        for (ei = 0; ei < function->expr_count; ei++) {
            DxilIrExpr *expr = &function->exprs[ei];
            uint32_t fj = 0;
            if (expr->kind != DXIL_IR_EXPR_CALL) {
                continue;
            }
            expr->call_kind = DXIL_IR_CALL_BUILTIN;
            for (fj = 0; fj < parser->module->function_count;
                fj++) {
                if (strcmp(expr->name,
                        parser->module->functions[fj].name)
                    == 0) {
                    expr->call_kind = DXIL_IR_CALL_USER;
                    break;
                }
            }
        }
    }
}

bool dxil_analyze(const char *source, size_t source_len,
    DxilIrModule *module, char *name_buf, size_t name_buf_len,
    DxilDiagnostic *diagnostic, DxilDiagKind *out_error) {
    DxilAnalyzeParser parser;
    if (module == NULL) {
        if (out_error != NULL) {
            *out_error = DXIL_DIAG_INVALID_IR;
        }
        dxil_diagnostic_set(diagnostic, DXIL_DIAG_STAGE_IR_BUILDER,
            DXIL_DIAG_INVALID_IR, NULL, 0, 0);
        return false;
    }
    if (source == NULL && source_len != 0) {
        if (out_error != NULL) {
            *out_error = DXIL_DIAG_INVALID_WGSL;
        }
        dxil_diagnostic_set(diagnostic, DXIL_DIAG_STAGE_PARSER,
            DXIL_DIAG_INVALID_WGSL, NULL, 0, 0);
        return false;
    }
    if (source == NULL) {
        source = "";
    }
    dxil_diagnostic_clear(diagnostic);
    dxil_ir_init(module);
    memset(&parser, 0, sizeof(parser));
    parser.src = source;
    parser.len = source_len;
    parser.module = module;
    parser.name_buf = name_buf;
    parser.name_buf_len = name_buf_len;
    parser.diagnostic = diagnostic;
    dxil_parse_advance(&parser);
    if (init_cached_types(&parser)) {
        while (parser.tok.kind != DXIL_TOK_EOF) {
            if (dxil_parse_accept_punct(&parser, ';')) {
                continue;
            }
            if (parser.tok.kind == DXIL_TOK_EOF) {
                break;
            }
            if (!dxil_parse_decl(&parser)) {
                break;
            }
        }
        if (!parser.failed) {
            if (fixup_pending_globals(&parser)
                && fixup_member_indices(&parser)) {
                fixup_call_kinds(&parser);
            }
        }
    }
    if (parser.failed) {
        if (out_error != NULL) {
            *out_error = parser.error_kind;
        }
        return false;
    }
    return true;
}
