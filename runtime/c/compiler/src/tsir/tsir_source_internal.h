/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_SOURCE_INTERNAL_H
#define DOE_TSIR_SOURCE_INTERNAL_H
#include "tsir_source.h"
#include "../compile_internal.h"

typedef struct TsirSourceFunction {
    TsirIrFunction view;
    TsirIrParam *params;
    TsirIrLocal *locals;
    TsirIrExpr *exprs;
    TsirIrStmt *stmts;
    uint32_t *args, *children, *scope;
    size_t expr_cap, stmt_cap, local_cap, scope_count;
    NameSpan *local_names;
    const FunctionRecord *syntax;
} TsirSourceFunction;

typedef struct TsirSourceBridge {
    Source source;
    CompilerWorkspace *compiler;
    TsirWgslModule module;
    TsirIrType *types;
    TsirIrStructDef *structs;
    TsirIrStructField *fields;
    TsirIrGlobal *globals;
    TsirIrFunction *functions;
    TsirSourceFunction *owned_functions;
    char *names;
    size_t names_count, names_cap, type_cap;
    TsirStatus status;
} TsirSourceBridge;
const char *tsir_source_name(TsirSourceBridge *, NameSpan);
bool tsir_source_fail(TsirSourceBridge *, uint32_t, const char *);
uint32_t tsir_source_expr(TsirSourceBridge *, TsirSourceFunction *, uint32_t, bool, unsigned);
uint32_t tsir_source_push_expr(TsirSourceBridge *, TsirSourceFunction *, TsirIrExpr);
uint32_t tsir_source_root(TsirSourceBridge *, TsirSourceFunction *, NameSpan, bool);
bool tsir_source_function(TsirSourceBridge *, TsirSourceFunction *, uint32_t);
#endif
