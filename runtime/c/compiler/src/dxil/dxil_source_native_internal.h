/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_SOURCE_NATIVE_INTERNAL_H
#define DOE_DXIL_SOURCE_NATIVE_INTERNAL_H
#include "dxil_analyze.h"
#include "../compile_internal.h"
#include "../override.h"

typedef struct DxilSourceFunction {
    DxilIrFunction *view;
    const FunctionRecord *syntax;
    NameSpan local_names[DXIL_IR_MAX_LOCALS];
    LocalBinding bindings[DXIL_IR_MAX_LOCALS];
    uint32_t scope[DXIL_IR_MAX_LOCALS];
    uint32_t scope_count;
} DxilSourceFunction;

typedef struct DxilSourceBridge {
    Source source;
    CompilerWorkspace *compiler;
    DxilIrModule *module;
    char *names;
    size_t names_count, names_capacity;
    DxilStatus status;
    uint32_t source_statement;
} DxilSourceBridge;

bool dxil_source_native_fail(DxilSourceBridge *, uint32_t offset,
    DxilStatus, const char *message);
const char *dxil_source_native_name(DxilSourceBridge *, NameSpan);
const char *dxil_source_native_text(DxilSourceBridge *, const char *);
DxilIrTypeId dxil_source_native_type(DxilSourceBridge *, TypeId);
uint32_t dxil_source_native_push_expr(DxilSourceBridge *, DxilSourceFunction *, DxilIrExpr);
uint32_t dxil_source_native_root(DxilSourceBridge *, DxilSourceFunction *, NameSpan, bool reference);
uint32_t dxil_source_native_expr(DxilSourceBridge *, DxilSourceFunction *, uint32_t, bool reference, unsigned depth);
bool dxil_source_native_types(DxilSourceBridge *);
bool dxil_source_native_globals(DxilSourceBridge *);
bool dxil_source_native_function(DxilSourceBridge *, uint32_t);
bool dxil_source_native_io(DxilSourceBridge *, uint32_t owner, uint32_t index, DxilIrIoAttr *);
bool dxil_source_native_value(DxilSourceBridge *, DxilSourceFunction *, uint32_t, ExpressionValue *);
bool dxil_source_native_constant(DxilSourceBridge *, DxilSourceFunction *, uint32_t, ConstantValue *);
bool dxil_source_native_constant_if_known(DxilSourceBridge *, DxilSourceFunction *, uint32_t, ConstantValue *, bool *);
#endif
