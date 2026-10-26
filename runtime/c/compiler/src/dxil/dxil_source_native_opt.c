/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_source_native_opt.h"
#include "../numeric.h"

/* Representation adaptation of retained ir_opt_rewrite / ir_const_eval. No
 * expression evaluator lives here: only alias discovery and operand rewrites. */
static bool scalar(const DxilIrModule *m, DxilIrTypeId id, DxilIrScalar *out) {
    if (id >= m->type_count || m->types[id].kind != DXIL_IR_TYPE_SCALAR) return false;
    *out = m->types[id].scalar;
    return true;
}
static bool constant(const DxilIrModule *m, const DxilIrFunction *f,
    uint32_t id, bool boolean, uint64_t *out) {
    for (unsigned depth = 0u; depth < 64u && id < f->expr_count; ++depth) {
        const DxilIrExpr *e = &f->exprs[id];
        if ((!boolean && e->kind == DXIL_IR_EXPR_INT_LIT) ||
            (boolean && e->kind == DXIL_IR_EXPR_BOOL_LIT)) {
            *out = boolean ? (e->bool_value ? 1u : 0u) : e->uint_value;
            return true;
        }
        if (e->kind == DXIL_IR_EXPR_LOAD) { id = e->inner; continue; }
        if (e->kind == DXIL_IR_EXPR_CONSTRUCT && e->args.len == 1u &&
            e->args.start < f->expr_arg_count) { id = f->expr_args[e->args.start]; continue; }
        if (e->kind == DXIL_IR_EXPR_LOCAL_REF && e->index < f->local_count &&
            !f->locals[e->index].mutable) {
            uint32_t initializer = DXIL_IR_INVALID_ID;
            for (uint32_t i = 0u; i < f->stmt_count; ++i) {
                const DxilIrStmt *s = &f->stmts[i];
                if (s->kind == DXIL_IR_STMT_LOCAL_DECL && s->local == e->index && s->is_const) {
                    if (s->has_initializer) initializer = s->initializer;
                    break;
                }
            }
            id = initializer;
            continue;
        }
        if (e->kind == DXIL_IR_EXPR_GLOBAL_REF && e->index < m->global_count) {
            const DxilIrGlobal *g = &m->globals[e->index];
            if (g->class == DXIL_IR_GLOBAL_CONST && g->has_initializer &&
                g->init_kind == (boolean ? DXIL_IR_CONST_BOOL : DXIL_IR_CONST_INT)) {
                *out = boolean ? (g->init_bool ? 1u : 0u) : g->init_int;
                return true;
            }
        }
        return false;
    }
    return false;
}
static bool operation(DxilIrBinaryOp op, ExpressionKind *out) {
    switch (op) {
        case DXIL_IR_BINARY_ADD: *out = EXPRESSION_ADD; return true;
        case DXIL_IR_BINARY_SUB: *out = EXPRESSION_SUBTRACT; return true;
        case DXIL_IR_BINARY_MUL: *out = EXPRESSION_MULTIPLY; return true;
        case DXIL_IR_BINARY_DIV: *out = EXPRESSION_DIVIDE; return true;
        case DXIL_IR_BINARY_REM: *out = EXPRESSION_REMAINDER; return true;
        case DXIL_IR_BINARY_BIT_AND: *out = EXPRESSION_BIT_AND; return true;
        case DXIL_IR_BINARY_BIT_OR: *out = EXPRESSION_BIT_OR; return true;
        case DXIL_IR_BINARY_BIT_XOR: *out = EXPRESSION_BIT_XOR; return true;
        case DXIL_IR_BINARY_SHIFT_LEFT: *out = EXPRESSION_SHIFT_LEFT; return true;
        case DXIL_IR_BINARY_SHIFT_RIGHT: *out = EXPRESSION_SHIFT_RIGHT; return true;
        case DXIL_IR_BINARY_EQUAL: case DXIL_IR_BINARY_NOT_EQUAL:
        case DXIL_IR_BINARY_LESS: case DXIL_IR_BINARY_LESS_EQUAL:
        case DXIL_IR_BINARY_GREATER: case DXIL_IR_BINARY_GREATER_EQUAL:
        case DXIL_IR_BINARY_LOGICAL_AND: case DXIL_IR_BINARY_LOGICAL_OR: return false;
    }
    return false;
}
static uint32_t identity(DxilIrBinaryOp op, uint64_t value, bool left, bool boolean) {
    if (boolean) {
        if ((op == DXIL_IR_BINARY_LOGICAL_AND && value != 0u) ||
            (op == DXIL_IR_BINARY_LOGICAL_OR && value == 0u)) return left ? 1u : 0u;
        return DXIL_IR_INVALID_ID;
    }
    switch (op) {
        case DXIL_IR_BINARY_ADD: case DXIL_IR_BINARY_BIT_OR: case DXIL_IR_BINARY_BIT_XOR:
            if (value == 0u) return left ? 1u : 0u;
            break;
        case DXIL_IR_BINARY_MUL:
            if (value == 0u) return left ? 0u : 1u;
            if (value == 1u) return left ? 1u : 0u;
            break;
        case DXIL_IR_BINARY_BIT_AND:
            if (value == 0u) return left ? 0u : 1u;
            break;
        case DXIL_IR_BINARY_SUB: case DXIL_IR_BINARY_SHIFT_LEFT: case DXIL_IR_BINARY_SHIFT_RIGHT:
            if (!left && value == 0u) return 0u;
            break;
        case DXIL_IR_BINARY_DIV:
            if (!left && value == 1u) return 0u;
            break;
        case DXIL_IR_BINARY_REM: case DXIL_IR_BINARY_EQUAL: case DXIL_IR_BINARY_NOT_EQUAL:
        case DXIL_IR_BINARY_LESS: case DXIL_IR_BINARY_LESS_EQUAL:
        case DXIL_IR_BINARY_GREATER: case DXIL_IR_BINARY_GREATER_EQUAL:
        case DXIL_IR_BINARY_LOGICAL_AND: case DXIL_IR_BINARY_LOGICAL_OR: break;
    }
    return DXIL_IR_INVALID_ID;
}
static bool reference(uint32_t *id, const uint32_t *replacements, uint32_t count) {
    if (*id >= count) return false;
    for (uint32_t n = 0u; n < count; ++n) {
        uint32_t next = replacements[*id];
        if (next == *id) return true;
        if (next >= count) return false;
        *id = next;
    }
    return false;
}
static bool range(DxilIrFunction *f, DxilIrRange r, const uint32_t *replacements) {
    if (r.start > f->expr_arg_count || r.len > f->expr_arg_count - r.start) return false;
    for (uint32_t i = 0u; i < r.len; ++i)
        if (!reference(&f->expr_args[r.start + i], replacements, f->expr_count)) return false;
    return true;
}
static bool rewrite_expr(DxilIrFunction *f, DxilIrExpr *e, const uint32_t *r) {
    switch (e->kind) {
        case DXIL_IR_EXPR_LOAD: case DXIL_IR_EXPR_ADDRESS_OF: case DXIL_IR_EXPR_DEREF:
            return reference(&e->inner, r, f->expr_count);
        case DXIL_IR_EXPR_UNARY: return reference(&e->operand, r, f->expr_count);
        case DXIL_IR_EXPR_BINARY:
            return reference(&e->lhs, r, f->expr_count) && reference(&e->rhs, r, f->expr_count);
        case DXIL_IR_EXPR_CALL: case DXIL_IR_EXPR_CONSTRUCT: return range(f, e->args, r);
        case DXIL_IR_EXPR_MEMBER: return reference(&e->base, r, f->expr_count);
        case DXIL_IR_EXPR_INDEX:
            return reference(&e->base, r, f->expr_count) && reference(&e->index_expr, r, f->expr_count);
        case DXIL_IR_EXPR_BOOL_LIT: case DXIL_IR_EXPR_INT_LIT: case DXIL_IR_EXPR_FLOAT_LIT:
        case DXIL_IR_EXPR_PARAM_REF: case DXIL_IR_EXPR_LOCAL_REF: case DXIL_IR_EXPR_GLOBAL_REF: return true;
    }
    return false;
}
static bool effect(uint32_t id, uint32_t current, const bool *effects) {
    return id >= current || effects[id];
}
static bool expression_effect(const DxilIrFunction *f, const DxilIrExpr *e,
    uint32_t current, const bool *effects) {
    switch (e->kind) {
        case DXIL_IR_EXPR_CALL: return true;
        case DXIL_IR_EXPR_LOAD: case DXIL_IR_EXPR_ADDRESS_OF: case DXIL_IR_EXPR_DEREF:
            return effect(e->inner, current, effects);
        case DXIL_IR_EXPR_UNARY: return effect(e->operand, current, effects);
        case DXIL_IR_EXPR_BINARY: return effect(e->lhs, current, effects) || effect(e->rhs, current, effects);
        case DXIL_IR_EXPR_CONSTRUCT:
            for (uint32_t i = 0u; i < e->args.len; ++i)
                if (effect(f->expr_args[e->args.start + i], current, effects)) return true;
            return false;
        case DXIL_IR_EXPR_MEMBER: return effect(e->base, current, effects);
        case DXIL_IR_EXPR_INDEX: return effect(e->base, current, effects) || effect(e->index_expr, current, effects);
        case DXIL_IR_EXPR_BOOL_LIT: case DXIL_IR_EXPR_INT_LIT: case DXIL_IR_EXPR_FLOAT_LIT:
        case DXIL_IR_EXPR_PARAM_REF: case DXIL_IR_EXPR_LOCAL_REF: case DXIL_IR_EXPR_GLOBAL_REF: return false;
    }
    return true;
}
static bool rewrite_stmt(DxilIrFunction *f, DxilIrStmt *s, const uint32_t *r) {
    switch (s->kind) {
        case DXIL_IR_STMT_LOCAL_DECL: return !s->has_initializer || reference(&s->initializer, r, f->expr_count);
        case DXIL_IR_STMT_EXPR: return reference(&s->expr, r, f->expr_count);
        case DXIL_IR_STMT_ASSIGN:
            return reference(&s->assign_lhs, r, f->expr_count) && reference(&s->assign_rhs, r, f->expr_count);
        case DXIL_IR_STMT_RETURN: return !s->has_return_value || reference(&s->return_value, r, f->expr_count);
        case DXIL_IR_STMT_IF: return reference(&s->cond, r, f->expr_count);
        case DXIL_IR_STMT_LOOP: return !s->has_cond || reference(&s->cond, r, f->expr_count);
        case DXIL_IR_STMT_SWITCH: return reference(&s->switch_expr, r, f->expr_count);
        case DXIL_IR_STMT_BLOCK: case DXIL_IR_STMT_BREAK: case DXIL_IR_STMT_CONTINUE:
        case DXIL_IR_STMT_DISCARD: return true;
    }
    return false;
}
static bool fold(DxilSourceBridge *b, DxilIrFunction *f, DxilIrExpr *e, DxilIrScalar type) {
    const IrType numeric_type = type == DXIL_IR_SCALAR_I32 ? IR_I32 : IR_U32;
    uint64_t left = 0u, right = 0u;
    NumericValue result;
    if (e->kind == DXIL_IR_EXPR_UNARY) {
        if ((e->unary_op != DXIL_IR_UNARY_BIT_NOT &&
            !(e->unary_op == DXIL_IR_UNARY_NEG && type == DXIL_IR_SCALAR_I32)) ||
            !constant(b->module, f, e->operand, false, &left)) return false;
        if (!numeric_unary(&b->source, 0u, e->unary_op == DXIL_IR_UNARY_NEG ?
            EXPRESSION_NEGATE : EXPRESSION_COMPLEMENT,
            (NumericValue){.bits = (uint32_t)left, .type = numeric_type}, &result)) return false;
    } else {
        ExpressionKind op;
        if (!operation(e->binary_op, &op) ||
            !constant(b->module, f, e->lhs, false, &left) ||
            !constant(b->module, f, e->rhs, false, &right)) return false;
        const IrType right_type = op == EXPRESSION_SHIFT_LEFT || op == EXPRESSION_SHIFT_RIGHT ? IR_U32 : numeric_type;
        if (!numeric_runtime_integer_binary(&b->source, op,
            (NumericValue){.bits = left, .type = numeric_type},
            (NumericValue){.bits = right, .type = right_type}, &result)) return false;
    }
    e->kind = DXIL_IR_EXPR_INT_LIT;
    e->uint_value = result.bits;
    return true;
}
bool dxil_source_native_optimize(DxilSourceBridge *b, DxilSourceFunction *function) {
    if (b == NULL || function == NULL || function->view == NULL || b->module == NULL) return false;
    if (b->status != DXIL_OK) return false;
    DxilIrFunction *f = function->view;
    if (f->expr_count > DXIL_IR_MAX_EXPRS || f->stmt_count > DXIL_IR_MAX_STMTS ||
        f->expr_arg_count > DXIL_IR_MAX_EXPR_ARGS || f->local_count > DXIL_IR_MAX_LOCALS ||
        f->switch_selector_count > DXIL_IR_MAX_SWITCH_SELECTORS) goto invalid;
    uint32_t replacements[DXIL_IR_MAX_EXPRS];
    bool effects[DXIL_IR_MAX_EXPRS] = {false};
    for (uint32_t i = 0u; i < f->expr_count; ++i) replacements[i] = i;
    for (uint32_t i = 0u; i < f->expr_count; ++i) {
        DxilIrExpr *e = &f->exprs[i];
        if (!rewrite_expr(f, e, replacements)) goto invalid;
        effects[i] = expression_effect(f, e, i, effects);
        DxilIrScalar type;
        if (!scalar(b->module, e->ty, &type)) continue;
        if ((type == DXIL_IR_SCALAR_I32 || type == DXIL_IR_SCALAR_U32) &&
            (e->kind == DXIL_IR_EXPR_BINARY || e->kind == DXIL_IR_EXPR_UNARY) && fold(b, f, e, type)) continue;
        if (e->kind != DXIL_IR_EXPR_BINARY || !(type == DXIL_IR_SCALAR_I32 ||
            type == DXIL_IR_SCALAR_U32 || type == DXIL_IR_SCALAR_ABSTRACT_INT || type == DXIL_IR_SCALAR_BOOL)) continue;
        for (unsigned side = 0u; side < 2u; ++side) {
            uint64_t value;
            if (!constant(b->module, f, side == 0u ? e->lhs : e->rhs, type == DXIL_IR_SCALAR_BOOL, &value)) continue;
            const uint32_t slot = identity(e->binary_op, value, side == 0u, type == DXIL_IR_SCALAR_BOOL);
            if (slot == DXIL_IR_INVALID_ID) continue;
            uint32_t replacement = slot == 0u ? e->lhs : e->rhs;
            uint32_t discarded = slot == 0u ? e->rhs : e->lhs;
            if (effect(discarded, i, effects)) continue;
            if (f->exprs[replacement].ty == e->ty && f->exprs[replacement].category == e->category) {
                replacements[i] = replacement;
                break;
            }
        }
    }
    for (uint32_t i = 0u; i < f->stmt_count; ++i)
        if (!rewrite_stmt(f, &f->stmts[i], replacements)) goto invalid;
    for (uint32_t i = 0u; i < f->switch_selector_count; ++i)
        if (!reference(&f->switch_selectors[i], replacements, f->expr_count)) goto invalid;
    return b->status == DXIL_OK;
invalid:
    return dxil_source_native_fail(b, 0u, DXIL_INVALID_IR, "invalid projected optimizer reference");
}
