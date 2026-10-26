/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_HLSL_INTERNAL_H
#define DOE_DXIL_HLSL_INTERNAL_H

#include "dxil_hlsl.h"

typedef struct DxilHlslEmitter {
    const DxilIrModule *module;
    char *out;
    size_t out_len;
    size_t pos;
    size_t indent;
    unsigned depth;
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

/* Subgroup builtin classification from the maps, defined in
 * dxil_hlsl_expr.c. */
bool dxil_hlsl_builtin_is_intrinsic(DxilIrBuiltin builtin);
const char *dxil_hlsl_builtin_intrinsic(DxilIrBuiltin builtin);

#endif /* DOE_DXIL_HLSL_INTERNAL_H */
