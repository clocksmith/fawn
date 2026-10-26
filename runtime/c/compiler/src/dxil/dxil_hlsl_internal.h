/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_HLSL_INTERNAL_H
#define DOE_DXIL_HLSL_INTERNAL_H

#include "dxil_hlsl.h"

#define DXIL_HLSL_TEMPORARY_NAME_CAP 96u

typedef struct DxilHlslEmitter {
    const DxilIrModule *module;
    char *out;
    size_t out_len;
    size_t pos;
    size_t indent;
    unsigned depth;
    char temporary_prefix[32];
    bool store_override;
    uint32_t store_owner, store_expression_count;
    DxilIrExprId store_expressions[DXIL_IR_MAX_EXPRS];
    bool pointer_override;
    DxilIrExprId pointer_override_raw, pointer_override_length, pointer_override_owner;
    bool stage_array_return;
    uint32_t stage_array_length;
    bool loop_continuing_has;
    DxilIrStmtId loop_continuing;
    bool loop_break_has;
    DxilIrStmtId loop_break;
    bool continuing_break_has;
    DxilIrStmtId continuing_break;
} DxilHlslEmitter;

/* Text writers defined in dxil_hlsl.c, shared by the expression and
 * statement emitters. */
DxilStatus dxil_hlsl_write_text(DxilHlslEmitter *emitter,
    const char *text);
DxilStatus dxil_hlsl_write_indent(DxilHlslEmitter *emitter);
DxilStatus dxil_hlsl_write_u32(DxilHlslEmitter *emitter, uint32_t value);
DxilStatus dxil_hlsl_write_u64(DxilHlslEmitter *emitter, uint64_t value);
DxilStatus dxil_hlsl_write_float(DxilHlslEmitter *emitter, double value);
DxilStatus dxil_hlsl_choose_temporary_names(DxilHlslEmitter *);
DxilStatus dxil_hlsl_temporary_name(const DxilHlslEmitter *, const char *, uint32_t,
    char buffer[DXIL_HLSL_TEMPORARY_NAME_CAP]);
DxilStatus dxil_hlsl_write_temporary(DxilHlslEmitter *, const char *, uint32_t);

/* Type, constant, expression, and statement emitters. Types and
 * expressions live in dxil_hlsl_expr.c, statements in
 * dxil_hlsl_stmt.c, and the constant writer in dxil_hlsl_expr.c. */
DxilStatus dxil_hlsl_emit_type_only(DxilHlslEmitter *emitter,
    DxilIrTypeId ty);
DxilStatus dxil_hlsl_emit_typed_name(DxilHlslEmitter *emitter,
    DxilIrTypeId ty, const char *name);
DxilStatus dxil_hlsl_emit_constant(DxilHlslEmitter *emitter,
    const DxilIrGlobal *global);
DxilStatus dxil_hlsl_emit_expr_list(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrRange args);
DxilStatus dxil_hlsl_emit_expr(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrExprId expr_id);
DxilStatus dxil_hlsl_emit_block_contents(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrStmtId stmt_id);
DxilStatus dxil_hlsl_emit_stmt(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrStmtId stmt_id);

DxilStatus dxil_hlsl_emit_guarded_store(DxilHlslEmitter *, const DxilIrFunction *, DxilIrStmtId,
    DxilIrExprId captured, DxilIrExprId raw, DxilIrExprId length);
const char *dxil_hlsl_assignment_text(DxilIrAssignOp);
const char *dxil_hlsl_local_name(const DxilIrFunction *, uint32_t, char buffer[32]);
DxilStatus dxil_hlsl_emit_pointer_captures(DxilHlslEmitter *, const DxilIrFunction *, DxilIrExprId);
DxilStatus dxil_hlsl_emit_captured_index(DxilHlslEmitter *, const DxilIrFunction *, DxilIrExprId, bool runtime_raw);
bool dxil_hlsl_runtime_guard(const DxilHlslEmitter *, const DxilIrFunction *, DxilIrExprId,
    DxilIrExprId *captured, DxilIrExprId *raw, DxilIrExprId *length);
bool dxil_hlsl_pointer_index_captured(const DxilHlslEmitter *, const DxilIrFunction *, DxilIrExprId);
DxilStatus dxil_hlsl_emit_array_length(DxilHlslEmitter *, const DxilIrFunction *, DxilIrRange);
DxilStatus dxil_hlsl_emit_array_helpers(DxilHlslEmitter *);
DxilStatus dxil_hlsl_emit_stage_return(DxilHlslEmitter *, const DxilIrFunction *, DxilIrExprId);
DxilStatus dxil_hlsl_emit_stage(DxilHlslEmitter *, const DxilIrFunction *);
DxilStatus dxil_hlsl_emit_plain_function(DxilHlslEmitter *, const DxilIrFunction *, bool implementation);
DxilStatus dxil_hlsl_emit_helpers(DxilHlslEmitter *);
DxilStatus dxil_hlsl_emit_extra_builtin(DxilHlslEmitter *, const DxilIrFunction *, DxilIrTypeId,
    const char *, DxilIrRange, bool *handled);
const char *dxil_hlsl_builtin_semantic(DxilIrBuiltin);

/* Subgroup builtin classification from the maps, defined in
 * dxil_hlsl_expr.c. */
bool dxil_hlsl_builtin_is_intrinsic(DxilIrBuiltin builtin);
const char *dxil_hlsl_builtin_intrinsic(DxilIrBuiltin builtin);

#endif /* DOE_DXIL_HLSL_INTERNAL_H */
