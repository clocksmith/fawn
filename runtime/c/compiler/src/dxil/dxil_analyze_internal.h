/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_ANALYZE_INTERNAL_H
#define DOE_DXIL_ANALYZE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dxil_analyze.h"

#define DXIL_ANALYZE_MAX_PENDING 256u
#define DXIL_ANALYZE_MAX_ATTRS 8u
#define DXIL_ANALYZE_MAX_DEPTH 256u

enum DxilAnalyzePunct {
    DXIL_P_ARROW = 256,
    DXIL_P_EQEQ,
    DXIL_P_NOTEQ,
    DXIL_P_LTE,
    DXIL_P_GTE,
    DXIL_P_ANDAND,
    DXIL_P_OROR,
    DXIL_P_SHL,
    DXIL_P_SHR,
    DXIL_P_PLUSEQ,
    DXIL_P_MINUSEQ,
    DXIL_P_MULEQ,
    DXIL_P_DIVEQ,
    DXIL_P_MODEQ,
    DXIL_P_ANDEQ,
    DXIL_P_OREQ,
    DXIL_P_XOREQ,
    DXIL_P_SHLEQ,
    DXIL_P_SHREQ
};

typedef enum DxilAnalyzeTokKind {
    DXIL_TOK_EOF,
    DXIL_TOK_IDENT,
    DXIL_TOK_INT,
    DXIL_TOK_FLOAT,
    DXIL_TOK_PUNCT
} DxilAnalyzeTokKind;

typedef struct DxilAnalyzeToken {
    DxilAnalyzeTokKind kind;
    int punct;
    const char *text;
    size_t len;
    size_t offset;
    uint64_t int_value;
    double float_value;
    DxilIrScalar lit_scalar;
} DxilAnalyzeToken;

typedef struct DxilAnalyzePending {
    uint32_t function_index;
    DxilIrExprId expr_id;
    const char *name;
    size_t name_len;
    size_t offset;
} DxilAnalyzePending;

typedef struct DxilAnalyzeAttr {
    const char *name;
    size_t name_len;
    size_t offset;
    uint32_t int_count;
    uint64_t int_values[3];
    bool has_ident;
    const char *ident;
    size_t ident_len;
    bool has_ident2;
    const char *ident2;
    size_t ident2_len;
} DxilAnalyzeAttr;

typedef struct DxilAnalyzeParser {
    const char *src;
    size_t len;
    size_t pos;
    DxilAnalyzeToken tok;
    DxilIrModule *module;
    char *name_buf;
    size_t name_buf_len;
    size_t name_pos;
    DxilDiagnostic *diagnostic;
    bool failed;
    DxilDiagKind error_kind;
    DxilIrFunction *function;
    uint32_t function_index;
    DxilAnalyzePending pending[DXIL_ANALYZE_MAX_PENDING];
    uint32_t pending_count;
    DxilAnalyzePending member_pending[DXIL_ANALYZE_MAX_PENDING];
    uint32_t member_pending_count;
    unsigned depth;
    DxilIrTypeId void_ty;
    DxilIrTypeId bool_ty;
    DxilIrTypeId i32_ty;
    DxilIrTypeId u32_ty;
    DxilIrTypeId f32_ty;
    DxilIrTypeId f16_ty;
    DxilIrTypeId aint_ty;
    DxilIrTypeId afloat_ty;
} DxilAnalyzeParser;


/* Shared parser helpers defined in dxil_analyze.c. */
void dxil_parse_fail_at(DxilAnalyzeParser *parser, DxilDiagKind kind,
    DxilDiagnosticStage stage, size_t offset);
void dxil_parse_fail_tok(DxilAnalyzeParser *parser, DxilDiagKind kind,
    DxilDiagnosticStage stage);
void dxil_parse_advance(DxilAnalyzeParser *parser);
bool dxil_parse_is_punct(DxilAnalyzeParser *parser, int punct);
bool dxil_parse_accept_punct(DxilAnalyzeParser *parser, int punct);
bool dxil_parse_expect_punct(DxilAnalyzeParser *parser, int punct);
bool dxil_parse_is_kw(DxilAnalyzeParser *parser, const char *text);
bool dxil_parse_accept_kw(DxilAnalyzeParser *parser, const char *text);
bool dxil_parse_expect_ident(DxilAnalyzeParser *parser,
    const char **out_text, size_t *out_len, size_t *out_offset);
bool dxil_parse_expect_int_value(DxilAnalyzeParser *parser,
    uint64_t *out_value);
const char *dxil_parse_dup_name(DxilAnalyzeParser *parser,
    const char *text, size_t len);
DxilIrTypeId dxil_parse_scalar_type_id(DxilAnalyzeParser *parser,
    DxilIrScalar scalar);
bool dxil_parse_scalar_name_to_enum(const char *text, size_t len,
    DxilIrScalar *out);
int dxil_parse_find_struct(DxilAnalyzeParser *parser, const char *text,
    size_t len);
bool dxil_parse_builtin_name_to_enum(const char *text, size_t len,
    DxilIrBuiltin *out);

/* Append helpers: wrap the dxil_ir append calls and fail with
 * OutputTooLarge at the ir_builder stage on capacity overflow. */
bool dxil_parse_append_expr(DxilAnalyzeParser *parser,
    const DxilIrExpr *expr, DxilIrExprId *out_id);
bool dxil_parse_append_stmt(DxilAnalyzeParser *parser,
    const DxilIrStmt *stmt, DxilIrStmtId *out_id);
bool dxil_parse_append_expr_arg(DxilAnalyzeParser *parser,
    DxilIrExprId arg);
bool dxil_parse_append_stmt_child(DxilAnalyzeParser *parser,
    DxilIrStmtId child);
bool dxil_parse_append_selector(DxilAnalyzeParser *parser,
    DxilIrExprId selector);
bool dxil_parse_append_switch_case(DxilAnalyzeParser *parser,
    const DxilIrSwitchCase *switch_case);

/* Name lookups in the current function (defined in
 * dxil_analyze_decl.c). Param/local indices are function-local;
 * globals are module-wide. */
bool dxil_parse_find_param(DxilAnalyzeParser *parser, const char *text,
    size_t len, uint32_t *out_index);
bool dxil_parse_find_local(DxilAnalyzeParser *parser, const char *text,
    size_t len, uint32_t *out_index);
bool dxil_parse_find_global(DxilAnalyzeParser *parser, const char *text,
    size_t len, uint32_t *out_index);
bool dxil_parse_add_pending(DxilAnalyzeParser *parser,
    uint32_t function_index, DxilIrExprId expr_id, const char *name,
    size_t name_len, size_t offset);
bool dxil_parse_add_member_pending(DxilAnalyzeParser *parser,
    uint32_t function_index, DxilIrExprId expr_id, const char *name,
    size_t name_len, size_t offset);

/* Parse entry points across the analyzer files. */
bool dxil_parse_type(DxilAnalyzeParser *parser, DxilIrTypeId *out_id);
bool dxil_parse_expr(DxilAnalyzeParser *parser, DxilIrExprId *out_id);
bool dxil_parse_stmt(DxilAnalyzeParser *parser, DxilIrStmtId *out_id);
bool dxil_parse_braced_block(DxilAnalyzeParser *parser,
    DxilIrStmtId *out_id);
bool dxil_parse_braced_block_with_continuing(
    DxilAnalyzeParser *parser, DxilIrStmtId *out_block,
    bool *out_has_continuing, DxilIrStmtId *out_continuing);
bool dxil_parse_decl(DxilAnalyzeParser *parser);
bool dxil_parse_raw_attrs(DxilAnalyzeParser *parser,
    DxilAnalyzeAttr *attrs, uint32_t *out_count);
const DxilAnalyzeAttr *dxil_parse_find_attr(
    const DxilAnalyzeAttr *attrs, uint32_t count, const char *name,
    bool *duplicate);
bool dxil_parse_interpret_io_attrs(DxilAnalyzeParser *parser,
    const DxilAnalyzeAttr *attrs, uint32_t count,
    DxilIrIoAttr *out_io, bool *out_has_io);
bool dxil_parse_is_type_starter(DxilAnalyzeParser *parser);

#endif /* DOE_DXIL_ANALYZE_INTERNAL_H */
