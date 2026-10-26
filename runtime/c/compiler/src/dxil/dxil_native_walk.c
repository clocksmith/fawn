/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_native_internal.h"

#include <string.h>

#include "dxil_builder.h"
#include "dxil_mapping.h"
#include "dxil_spec.h"

static DxilIrBuiltin param_builtin_to_mapping(DxilIrBuiltin builtin,
    DxilParamBuiltin *out) {
    switch (builtin) {
        case DXIL_IR_BUILTIN_GLOBAL_INVOCATION_ID:
            *out = DXIL_BUILTIN_GLOBAL_INVOCATION_ID;
            return true;
        case DXIL_IR_BUILTIN_LOCAL_INVOCATION_ID:
            *out = DXIL_BUILTIN_LOCAL_INVOCATION_ID;
            return true;
        case DXIL_IR_BUILTIN_WORKGROUP_ID:
            *out = DXIL_BUILTIN_WORKGROUP_ID;
            return true;
        case DXIL_IR_BUILTIN_LOCAL_INVOCATION_INDEX:
            *out = DXIL_BUILTIN_LOCAL_INVOCATION_INDEX;
            return true;
        case DXIL_IR_BUILTIN_VERTEX_INDEX:
            *out = DXIL_BUILTIN_VERTEX_INDEX;
            return true;
        case DXIL_IR_BUILTIN_INSTANCE_INDEX:
            *out = DXIL_BUILTIN_INSTANCE_INDEX;
            return true;
        case DXIL_IR_BUILTIN_POSITION:
            *out = DXIL_BUILTIN_POSITION;
            return true;
        case DXIL_IR_BUILTIN_FRONT_FACING:
            *out = DXIL_BUILTIN_FRONT_FACING;
            return true;
        default:
            *out = DXIL_BUILTIN_OTHER;
            return false;
    }
}

static DxilStatus emit_expr_value(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, const DxilIrFunction *function,
    DxilIrExprId expr_id, unsigned depth, DxilValueIndex *out);

static DxilStatus emit_unary_op(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, DxilIrUnaryOp op, DxilValueIndex operand,
    DxilIrTypeId result_ty, DxilValueIndex *out) {
    bool is_float = dxil_ir_is_float_type(emitter->module, result_ty);
    uint32_t llvm_op = 0;
    DxilUnaryForm form = DXIL_UNARY_ZERO_MINUS;
    DxilUnaryOp mapping_op = DXIL_UNARY_NEG;
    DxilBodyOp body_op;
    switch (op) {
        case DXIL_IR_UNARY_NEG:
            mapping_op = DXIL_UNARY_NEG;
            break;
        case DXIL_IR_UNARY_NOT:
            mapping_op = DXIL_UNARY_NOT;
            break;
        case DXIL_IR_UNARY_BIT_NOT:
            mapping_op = DXIL_UNARY_BIT_NOT;
            break;
    }
    dxil_unary_opcode(mapping_op, is_float, &llvm_op, &form);
    memset(&body_op, 0, sizeof(body_op));
    body_op.kind = DXIL_OP_BINOP;
    body_op.opcode = llvm_op;
    if (form == DXIL_UNARY_ZERO_MINUS) {
        DxilValueIndex zero = 0;
        DxilStatus status = DXIL_OK;
        if (is_float) {
            status = dxil_native_const_f32(emitter, 0.0f, &zero);
        } else {
            status = dxil_native_const_i32(emitter, 0, &zero);
        }
        if (status != DXIL_OK) {
            return status;
        }
        body_op.lhs = zero;
        body_op.rhs = operand;
    } else {
        DxilValueIndex all_ones = 0;
        DxilStatus status = dxil_native_const_i32(emitter, -1, &all_ones);
        if (status != DXIL_OK) {
            return status;
        }
        body_op.lhs = operand;
        body_op.rhs = all_ones;
    }
    return dxil_body_add_op(body, &body_op, emitter->i32_ty, out);
}

static DxilStatus emit_binary_op(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, DxilIrBinaryOp op, DxilValueIndex lhs,
    DxilValueIndex rhs, DxilIrTypeId result_ty,
    DxilValueIndex *out) {
    bool is_float = dxil_ir_is_float_type(emitter->module, result_ty);
    bool is_signed = dxil_ir_is_signed_type(emitter->module, result_ty);
    bool is_comparison = false;
    uint32_t llvm_op = 0;
    DxilBinaryOp mapping_op = DXIL_BINARY_ADD;
    DxilBodyOp body_op;
    switch (op) {
        case DXIL_IR_BINARY_ADD:
            mapping_op = DXIL_BINARY_ADD;
            break;
        case DXIL_IR_BINARY_SUB:
            mapping_op = DXIL_BINARY_SUB;
            break;
        case DXIL_IR_BINARY_MUL:
            mapping_op = DXIL_BINARY_MUL;
            break;
        case DXIL_IR_BINARY_DIV:
            mapping_op = DXIL_BINARY_DIV;
            break;
        case DXIL_IR_BINARY_REM:
            mapping_op = DXIL_BINARY_REM;
            break;
        case DXIL_IR_BINARY_BIT_AND:
            mapping_op = DXIL_BINARY_BIT_AND;
            break;
        case DXIL_IR_BINARY_BIT_OR:
            mapping_op = DXIL_BINARY_BIT_OR;
            break;
        case DXIL_IR_BINARY_BIT_XOR:
            mapping_op = DXIL_BINARY_BIT_XOR;
            break;
        case DXIL_IR_BINARY_SHIFT_LEFT:
            mapping_op = DXIL_BINARY_SHIFT_LEFT;
            break;
        case DXIL_IR_BINARY_SHIFT_RIGHT:
            mapping_op = DXIL_BINARY_SHIFT_RIGHT;
            break;
        case DXIL_IR_BINARY_EQUAL:
            mapping_op = DXIL_BINARY_EQUAL;
            break;
        case DXIL_IR_BINARY_NOT_EQUAL:
            mapping_op = DXIL_BINARY_NOT_EQUAL;
            break;
        case DXIL_IR_BINARY_LESS:
            mapping_op = DXIL_BINARY_LESS;
            break;
        case DXIL_IR_BINARY_LESS_EQUAL:
            mapping_op = DXIL_BINARY_LESS_EQUAL;
            break;
        case DXIL_IR_BINARY_GREATER:
            mapping_op = DXIL_BINARY_GREATER;
            break;
        case DXIL_IR_BINARY_GREATER_EQUAL:
            mapping_op = DXIL_BINARY_GREATER_EQUAL;
            break;
        case DXIL_IR_BINARY_LOGICAL_AND:
            mapping_op = DXIL_BINARY_LOGICAL_AND;
            break;
        case DXIL_IR_BINARY_LOGICAL_OR:
            mapping_op = DXIL_BINARY_LOGICAL_OR;
            break;
    }
    dxil_binary_opcode(mapping_op, is_float, is_signed, &is_comparison,
        &llvm_op);
    memset(&body_op, 0, sizeof(body_op));
    body_op.lhs = lhs;
    body_op.rhs = rhs;
    if (is_comparison) {
        body_op.kind = DXIL_OP_CMP;
        body_op.predicate = llvm_op;
        return dxil_body_add_op(body, &body_op, emitter->i1_ty, out);
    }
    body_op.kind = DXIL_OP_BINOP;
    body_op.opcode = llvm_op;
    return dxil_body_add_op(body, &body_op, emitter->i32_ty, out);
}

static DxilStatus emit_builtin_call(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, const DxilIrFunction *function,
    const char *name, DxilIrRange args, unsigned depth,
    DxilValueIndex *out) {
    DxilBuiltinCall call;
    DxilValueIndex first_arg = 0;
    DxilValueIndex extra[2];
    DxilStatus status = DXIL_OK;
    uint32_t i = 0;
    dxil_builtin_call(name, &call);
    if (!call.recognized) {
        for (i = 0; i < args.len; i++) {
            DxilValueIndex discarded = 0;
            status = emit_expr_value(emitter, body, function,
                function->expr_args[args.start + i], depth, &discarded);
            if (status != DXIL_OK) {
                return status;
            }
        }
        return dxil_native_const_i32(emitter, 0, out);
    }
    switch (call.arg_mode) {
        case DXIL_BUILTIN_ARG_NONE:
            return dxil_native_dx_op_call(emitter, body, call.opcode, NULL, 0,
                out);
        case DXIL_BUILTIN_ARG_MODE_CONSTANT: {
            DxilValueIndex mode = 0;
            status = dxil_native_const_u32(emitter, call.mode_value, &mode);
            if (status != DXIL_OK) {
                return status;
            }
            return dxil_native_dx_op_call(emitter, body, call.opcode, &mode, 1,
                out);
        }
        case DXIL_BUILTIN_ARG_FIRST_IF_PRESENT:
            if (args.len >= 1) {
                status = emit_expr_value(emitter, body, function,
                    function->expr_args[args.start], depth,
                    &first_arg);
                if (status != DXIL_OK) {
                    return status;
                }
                return dxil_native_dx_op_call(emitter, body, call.opcode,
                    &first_arg, 1, out);
            }
            return dxil_native_dx_op_call(emitter, body, call.opcode, NULL, 0,
                out);
        case DXIL_BUILTIN_ARG_FIRST_PLUS_WAVE_SUM:
            if (args.len >= 1) {
                DxilValueIndex wave_op = 0;
                status = emit_expr_value(emitter, body, function,
                    function->expr_args[args.start], depth,
                    &first_arg);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_native_const_u32(emitter, 0, &wave_op);
                if (status != DXIL_OK) {
                    return status;
                }
                extra[0] = first_arg;
                extra[1] = wave_op;
                return dxil_native_dx_op_call(emitter, body, call.opcode, extra,
                    2, out);
            }
            return dxil_native_const_i32(emitter, 0, out);
    }
    return dxil_native_const_i32(emitter, 0, out);
}

static DxilStatus emit_expr_value(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, const DxilIrFunction *function,
    DxilIrExprId expr_id, unsigned depth, DxilValueIndex *out) {
    const DxilIrExpr *expr = NULL;
    if (depth > DXIL_NATIVE_MAX_DEPTH) {
        return DXIL_INVALID_IR;
    }
    if (expr_id >= function->expr_count) {
        return DXIL_INVALID_IR;
    }
    expr = &function->exprs[expr_id];
    switch (expr->kind) {
        case DXIL_IR_EXPR_BOOL_LIT:
            return dxil_native_const_value(emitter, emitter->i1_ty,
                DXIL_CONST_UINT, 0, expr->bool_value ? 1u : 0u, 0,
                out);
        case DXIL_IR_EXPR_INT_LIT:
            return dxil_native_const_value(emitter, emitter->i32_ty,
                DXIL_CONST_UINT, 0, expr->uint_value, 0, out);
        case DXIL_IR_EXPR_FLOAT_LIT:
            return dxil_native_const_f32(emitter, (float)expr->float_value, out);
        case DXIL_IR_EXPR_PARAM_REF:
        case DXIL_IR_EXPR_LOCAL_REF:
        case DXIL_IR_EXPR_GLOBAL_REF:
            return dxil_native_const_i32(emitter, 0, out);
        case DXIL_IR_EXPR_LOAD:
            return emit_expr_value(emitter, body, function,
                expr->inner, depth + 1, out);
        case DXIL_IR_EXPR_ADDRESS_OF:
        case DXIL_IR_EXPR_DEREF:
            return DXIL_UNSUPPORTED_CONSTRUCT;
        case DXIL_IR_EXPR_UNARY: {
            DxilValueIndex operand = 0;
            DxilStatus status = emit_expr_value(emitter, body,
                function, expr->operand, depth + 1, &operand);
            if (status != DXIL_OK) {
                return status;
            }
            return emit_unary_op(emitter, body, expr->unary_op,
                operand, expr->ty, out);
        }
        case DXIL_IR_EXPR_BINARY: {
            DxilValueIndex lhs = 0;
            DxilValueIndex rhs = 0;
            DxilStatus status = emit_expr_value(emitter, body,
                function, expr->lhs, depth + 1, &lhs);
            if (status != DXIL_OK) {
                return status;
            }
            status = emit_expr_value(emitter, body, function,
                expr->rhs, depth + 1, &rhs);
            if (status != DXIL_OK) {
                return status;
            }
            return emit_binary_op(emitter, body, expr->binary_op,
                lhs, rhs, expr->ty, out);
        }
        case DXIL_IR_EXPR_CALL:
            if (expr->call_kind == DXIL_IR_CALL_BUILTIN) {
                return emit_builtin_call(emitter, body, function,
                    expr->name, expr->args, depth + 1, out);
            }
            return dxil_native_const_i32(emitter, 0, out);
        case DXIL_IR_EXPR_CONSTRUCT:
            return dxil_native_const_i32(emitter, 0, out);
        case DXIL_IR_EXPR_MEMBER: {
            DxilValueIndex discarded = 0;
            DxilStatus status = emit_expr_value(emitter, body,
                function, expr->base, depth + 1, &discarded);
            if (status != DXIL_OK) {
                return status;
            }
            return dxil_native_const_i32(emitter, 0, out);
        }
        case DXIL_IR_EXPR_INDEX: {
            DxilValueIndex discarded = 0;
            DxilStatus status = emit_expr_value(emitter, body,
                function, expr->base, depth + 1, &discarded);
            if (status != DXIL_OK) {
                return status;
            }
            status = emit_expr_value(emitter, body, function,
                expr->index_expr, depth + 1, &discarded);
            if (status != DXIL_OK) {
                return status;
            }
            return dxil_native_const_i32(emitter, 0, out);
        }
    }
    return DXIL_INVALID_IR;
}

static DxilStatus emit_function_stmts(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, const DxilIrFunction *function,
    DxilIrStmtId stmt_id, unsigned depth) {
    const DxilIrStmt *stmt = NULL;
    uint32_t i = 0;
    DxilStatus status = DXIL_OK;
    if (depth > DXIL_NATIVE_MAX_DEPTH) {
        return DXIL_INVALID_IR;
    }
    if (stmt_id >= function->stmt_count) {
        return DXIL_INVALID_IR;
    }
    stmt = &function->stmts[stmt_id];
    switch (stmt->kind) {
        case DXIL_IR_STMT_BLOCK:
            for (i = 0; i < stmt->range.len; i++) {
                status = emit_function_stmts(emitter, body, function,
                    function->stmt_children[stmt->range.start + i],
                    depth + 1);
                if (status != DXIL_OK) {
                    return status;
                }
            }
            return DXIL_OK;
        case DXIL_IR_STMT_LOCAL_DECL: {
            DxilValueIndex discarded = 0;
            if (!stmt->has_initializer) {
                return DXIL_OK;
            }
            return emit_expr_value(emitter, body, function,
                stmt->initializer, depth + 1, &discarded);
        }
        case DXIL_IR_STMT_EXPR: {
            DxilValueIndex discarded = 0;
            return emit_expr_value(emitter, body, function,
                stmt->expr, depth + 1, &discarded);
        }
        case DXIL_IR_STMT_ASSIGN: {
            DxilValueIndex discarded = 0;
            return emit_expr_value(emitter, body, function,
                stmt->assign_rhs, depth + 1, &discarded);
        }
        case DXIL_IR_STMT_RETURN: {
            DxilValueIndex discarded = 0;
            if (!stmt->has_return_value) {
                return DXIL_OK;
            }
            return emit_expr_value(emitter, body, function,
                stmt->return_value, depth + 1, &discarded);
        }
        case DXIL_IR_STMT_IF: {
            DxilValueIndex discarded = 0;
            status = emit_expr_value(emitter, body, function,
                stmt->cond, depth + 1, &discarded);
            if (status != DXIL_OK) {
                return status;
            }
            status = emit_function_stmts(emitter, body, function,
                stmt->then_block, depth + 1);
            if (status != DXIL_OK) {
                return status;
            }
            if (stmt->has_else_block) {
                return emit_function_stmts(emitter, body, function,
                    stmt->else_block, depth + 1);
            }
            return DXIL_OK;
        }
        case DXIL_IR_STMT_LOOP:
            if (stmt->has_init) {
                status = emit_function_stmts(emitter, body, function,
                    stmt->init, depth + 1);
                if (status != DXIL_OK) {
                    return status;
                }
            }
            if (stmt->has_cond) {
                DxilValueIndex discarded = 0;
                status = emit_expr_value(emitter, body, function,
                    stmt->cond, depth + 1, &discarded);
                if (status != DXIL_OK) {
                    return status;
                }
            }
            status = emit_function_stmts(emitter, body, function,
                stmt->body, depth + 1);
            if (status != DXIL_OK) {
                return status;
            }
            if (stmt->has_continuing) {
                return emit_function_stmts(emitter, body, function,
                    stmt->continuing, depth + 1);
            }
            return DXIL_OK;
        case DXIL_IR_STMT_SWITCH: {
            DxilValueIndex discarded = 0;
            status = emit_expr_value(emitter, body, function,
                stmt->switch_expr, depth + 1, &discarded);
            if (status != DXIL_OK) {
                return status;
            }
            for (i = 0; i < stmt->cases.len; i++) {
                status = emit_function_stmts(emitter, body, function,
                    function->switch_cases[stmt->cases.start + i]
                        .body,
                    depth + 1);
                if (status != DXIL_OK) {
                    return status;
                }
            }
            return DXIL_OK;
        }
        case DXIL_IR_STMT_BREAK:
        case DXIL_IR_STMT_CONTINUE:
        case DXIL_IR_STMT_DISCARD:
            return DXIL_OK;
    }
    return DXIL_INVALID_IR;
}

static DxilStatus emit_compute_body(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, const DxilIrFunction *function) {
    uint32_t i = 0;
    uint32_t component = 0;
    DxilStatus status = DXIL_OK;
    for (i = 0; i < function->param_count; i++) {
        const DxilIrParam *param = &function->params[i];
        DxilParamBuiltin mapped = DXIL_BUILTIN_OTHER;
        uint32_t opcode = 0;
        uint32_t call_count = 0;
        DxilValueIndex discarded = 0;
        if (!param->has_io) {
            continue;
        }
        (void)param_builtin_to_mapping(param->io.builtin, &mapped);
        if (!dxil_compute_param_opcode(mapped, &opcode, &call_count)) {
            continue;
        }
        if (mapped == DXIL_BUILTIN_LOCAL_INVOCATION_INDEX) {
            status = dxil_native_dx_op_call(emitter, body, opcode, NULL, 0,
                &discarded);
            if (status != DXIL_OK) {
                return status;
            }
        } else {
            for (component = 0; component < 3; component++) {
                DxilValueIndex comp_const = 0;
                status = dxil_native_const_u32(emitter, component, &comp_const);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_native_dx_op_call(emitter, body, opcode,
                    &comp_const, 1, &discarded);
                if (status != DXIL_OK) {
                    return status;
                }
            }
        }
    }
    status = emit_function_stmts(emitter, body, function,
        function->root_stmt, 0);
    if (status != DXIL_OK) {
        return status;
    }
    {
        DxilBodyOp ret;
        memset(&ret, 0, sizeof(ret));
        ret.kind = DXIL_OP_RET_VOID;
        return dxil_body_add_void_op(body, &ret);
    }
}

static DxilStatus emit_vs_body(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, const DxilIrFunction *function) {
    uint32_t i = 0;
    DxilStatus status = DXIL_OK;
    for (i = 0; i < function->param_count; i++) {
        const DxilIrParam *param = &function->params[i];
        DxilParamBuiltin mapped = DXIL_BUILTIN_OTHER;
        DxilValueIndex discarded = 0;
        if (!param->has_io) {
            continue;
        }
        (void)param_builtin_to_mapping(param->io.builtin, &mapped);
        if (!dxil_vertex_param_loads_input(mapped)) {
            continue;
        }
        status = dxil_native_dx_op_call(emitter, body, DXIL_OP_LOAD_INPUT,
            NULL, 0, &discarded);
        if (status != DXIL_OK) {
            return status;
        }
    }
    status = emit_function_stmts(emitter, body, function,
        function->root_stmt, 0);
    if (status != DXIL_OK) {
        return status;
    }
    {
        DxilBodyOp ret;
        memset(&ret, 0, sizeof(ret));
        ret.kind = DXIL_OP_RET_VOID;
        return dxil_body_add_void_op(body, &ret);
    }
}

static DxilStatus emit_ps_body(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, const DxilIrFunction *function) {
    uint32_t i = 0;
    DxilStatus status = DXIL_OK;
    for (i = 0; i < function->param_count; i++) {
        const DxilIrParam *param = &function->params[i];
        DxilParamBuiltin mapped = DXIL_BUILTIN_OTHER;
        DxilValueIndex discarded = 0;
        if (!param->has_io) {
            continue;
        }
        (void)param_builtin_to_mapping(param->io.builtin, &mapped);
        if (!dxil_fragment_param_loads_input(param->io.has_location,
                mapped)) {
            continue;
        }
        status = dxil_native_dx_op_call(emitter, body, DXIL_OP_LOAD_INPUT,
            NULL, 0, &discarded);
        if (status != DXIL_OK) {
            return status;
        }
    }
    status = emit_function_stmts(emitter, body, function,
        function->root_stmt, 0);
    if (status != DXIL_OK) {
        return status;
    }
    {
        DxilBodyOp ret;
        memset(&ret, 0, sizeof(ret));
        ret.kind = DXIL_OP_RET_VOID;
        return dxil_body_add_void_op(body, &ret);
    }
}

DxilStatus dxil_native_emit_entry_body(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, const DxilIrFunction *function,
    DxilIrStage stage) {
    switch (stage) {
        case DXIL_IR_STAGE_COMPUTE:
            return emit_compute_body(emitter, body, function);
        case DXIL_IR_STAGE_VERTEX:
            return emit_vs_body(emitter, body, function);
        case DXIL_IR_STAGE_FRAGMENT:
            return emit_ps_body(emitter, body, function);
    }
    return DXIL_INVALID_IR;
}

