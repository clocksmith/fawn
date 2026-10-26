/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_source_internal.h"
#include "../sema.h"
#include <stdlib.h>
#include <string.h>

static uint32_t statement(TsirSourceBridge *, TsirSourceFunction *, uint32_t, unsigned);
static uint32_t push(TsirSourceBridge *b, TsirSourceFunction *f, TsirIrStmt node) {
    if (f->view.stmt_count == f->stmt_cap) {
        (void)tsir_source_fail(b, 0u, "TSIR statement adaptation capacity exceeded");
        return TSIR_IR_NONE;
    }
    uint32_t id = (uint32_t)f->view.stmt_count++;
    f->stmts[id] = node;
    return id;
}
static uint32_t block_scope(TsirSourceBridge *b, TsirSourceFunction *f, uint32_t first, uint32_t end, unsigned depth, bool retain_scope) {
    size_t scope = f->scope_count;
    uint32_t count = 0u;
    for (uint32_t i = first; i < end;) {
        if (b->compiler->statements[i].end <= i) {
            (void)tsir_source_fail(b, 0u, "invalid typed statement range");
            return TSIR_IR_NONE;
        }
        ++count;
        i = b->compiler->statements[i].end;
    }
    if (count > f->stmt_cap - f->view.stmt_child_count) {
        (void)tsir_source_fail(b, 0u, "TSIR block edge capacity exceeded");
        return TSIR_IR_NONE;
    }
    uint32_t start = (uint32_t)f->view.stmt_child_count;
    f->view.stmt_child_count += count;
    uint32_t slot = start;
    for (uint32_t i = first; b->status == TSIR_OK && i < end; i = b->compiler->statements[i].end)
        f->children[slot++] = statement(b, f, i, depth + 1u);
    if (!retain_scope) f->scope_count = scope;
    return push(b, f, (TsirIrStmt){.kind = TSIR_IR_STMT_BLOCK, .range_start = start, .range_len = count});
}
static uint32_t block(TsirSourceBridge *b, TsirSourceFunction *f, uint32_t first, uint32_t end, unsigned depth) {
    return block_scope(b, f, first, end, depth, false);
}
/* Retained TSIR walkers inspect the switch selector but not case control flow.
 * Still lower each case's selectors and body into the expression inventory:
 * binding discovery observes every global reference, even inside a case. This
 * projection is for TSIR recovery, not an executable WGSL IR replacement. */
static uint32_t switch_statement(TsirSourceBridge *b, TsirSourceFunction *f,
    const Statement *s, unsigned depth) {
    for (uint32_t first = s->body_start; b->status == TSIR_OK && first < s->end;) {
        const Statement *clause = &b->compiler->statements[first];
        if (clause->kind != STATEMENT_CASE || clause->body_start <= first ||
            clause->body_start > clause->end || clause->end > s->end) {
            (void)tsir_source_fail(b, s->source_offset, "invalid typed switch clause range");
            return TSIR_IR_NONE;
        }
        (void)block(b, f, clause->body_start, clause->end, depth + 1u);
        for (uint32_t selector = first; b->status == TSIR_OK && selector < clause->body_start; ++selector) {
            const Statement *item = &b->compiler->statements[selector];
            if (item->kind != STATEMENT_CASE) {
                (void)tsir_source_fail(b, item->source_offset, "invalid typed switch selector");
                return TSIR_IR_NONE;
            }
            if (item->value.condition != NO_ID)
                (void)tsir_source_expr(b, f, item->value.condition, false, 0u);
        }
        first = clause->end;
    }
    uint32_t selector = tsir_source_expr(b, f, s->value.condition, false, 0u);
    return push(b, f, (TsirIrStmt){.kind = TSIR_IR_STMT_SWITCH, .expr = selector});
}
static TsirIrAssignOp assignment(UpdateKind kind) {
    switch (kind) {
        case UPDATE_ADD: case UPDATE_INCREMENT: return TSIR_IR_ASSIGN_ADD;
        case UPDATE_SUBTRACT: case UPDATE_DECREMENT: return TSIR_IR_ASSIGN_SUB;
        case UPDATE_MULTIPLY: return TSIR_IR_ASSIGN_MUL;
        case UPDATE_DIVIDE: return TSIR_IR_ASSIGN_DIV;
        case UPDATE_REMAINDER: return TSIR_IR_ASSIGN_REM;
        case UPDATE_BIT_AND: return TSIR_IR_ASSIGN_BIT_AND;
        case UPDATE_BIT_OR: return TSIR_IR_ASSIGN_BIT_OR;
        case UPDATE_BIT_XOR: return TSIR_IR_ASSIGN_BIT_XOR;
        case UPDATE_SHIFT_LEFT: return TSIR_IR_ASSIGN_SHIFT_LEFT;
        case UPDATE_SHIFT_RIGHT: return TSIR_IR_ASSIGN_SHIFT_RIGHT;
        case UPDATE_SET: return TSIR_IR_ASSIGN;
    }
    return TSIR_IR_ASSIGN;
}
static uint32_t statement(TsirSourceBridge *b, TsirSourceFunction *f, uint32_t id, unsigned depth) {
    if (b->status != TSIR_OK) return TSIR_IR_NONE;
    if (id >= b->compiler->statement_count || depth > 96u) {
        (void)tsir_source_fail(b, 0u, "TSIR statement traversal exceeded its bound");
        return TSIR_IR_NONE;
    }
    const Statement *s = &b->compiler->statements[id];
    TsirIrStmt o = {0};
    switch (s->kind) {
        case STATEMENT_LET: case STATEMENT_VAR: case STATEMENT_CONST: {
            const LocalSyntax *l = &s->value.local;
            o.kind = TSIR_IR_STMT_LOCAL_DECL;
            o.has_initializer = (l->flags & LOCAL_INITIALIZED) != 0u;
            if (o.has_initializer) o.initializer = tsir_source_expr(b, f, l->expression, false, 0u);
            TypeId ty = o.has_initializer ? b->compiler->expressions[l->expression].resolved_type : IR_VOID;
            if ((l->flags & LOCAL_ANNOTATED) != 0u) {
                LocalScope empty = {0};
                if (!sema_type_reference(&b->source, l->annotation, &empty, &ty)) {
                    b->status = TSIR_ERR_UNSUPPORTED;
                    return TSIR_IR_NONE;
                }
            } else if (s->kind != STATEMENT_CONST) {
                if (ty == IR_ABSTRACT_INT) ty = IR_I32;
                if (ty == IR_ABSTRACT_FLOAT) ty = IR_F32;
            }
            if (f->view.local_count == f->local_cap) {
                (void)tsir_source_fail(b, s->source_offset, "TSIR local capacity exceeded");
                return TSIR_IR_NONE;
            }
            o.local = (uint32_t)f->view.local_count++;
            o.is_const = s->kind != STATEMENT_VAR;
            f->locals[o.local] = (TsirIrLocal){.name = tsir_source_name(b, l->name), .ty = ty, .mutable = s->kind == STATEMENT_VAR};
            f->local_names[o.local] = l->name;
            f->scope[f->scope_count++] = o.local;
            break;
        }
        case STATEMENT_ASSIGN: {
            const Assignment *a = &s->value.assignment;
            if (a->kind == TARGET_ROOT && source_span_is(&b->source, a->root, "_")) {
                o.kind = TSIR_IR_STMT_EXPR;
                o.expr = tsir_source_expr(b, f, a->expression, false, 0u);
                break;
            }
            o.kind = TSIR_IR_STMT_ASSIGN;
            if (a->kind == TARGET_ROOT) o.assign_lhs = tsir_source_root(b, f, a->root, true);
            else if (a->kind == TARGET_REFERENCE) o.assign_lhs = tsir_source_expr(b, f, a->path, true, 0u);
            else {
                uint32_t base = a->path == NO_ID ? tsir_source_root(b, f, a->root, true) :
                    tsir_source_expr(b, f, a->path, true, 0u);
                TsirIrExpr target = {0};
                if (a->kind == TARGET_INDEX) {
                    target.kind = TSIR_IR_EXPR_INDEX;
                    target.index_base = base;
                    target.index_index = tsir_source_expr(b, f, a->index, false, 0u);
                } else {
                    target.kind = TSIR_IR_EXPR_MEMBER;
                    target.member_base = base;
                    target.member_field_name = tsir_source_name(b, source_name_at(&b->source, a->index));
                }
                o.assign_lhs = tsir_source_push_expr(b, f, target);
            }
            o.assign_op = assignment(a->operation);
            if (a->expression != NO_ID) o.assign_rhs = tsir_source_expr(b, f, a->expression, false, 0u);
            else o.assign_rhs = tsir_source_push_expr(b, f, (TsirIrExpr){.kind = TSIR_IR_EXPR_INT_LIT,
                .ty = IR_U32, .int_value = 1u});
            break;
        }
        case STATEMENT_IF:
            o.kind = TSIR_IR_STMT_IF;
            o.if_cond = tsir_source_expr(b, f, s->value.condition, false, 0u);
            o.if_then = block(b, f, s->body_start, s->else_start, depth + 1u);
            o.has_if_else = s->else_start < s->end;
            if (o.has_if_else) o.if_else = block(b, f, s->else_start, s->end, depth + 1u);
            break;
        case STATEMENT_FOR: case STATEMENT_WHILE: case STATEMENT_LOOP: {
            size_t scope = f->scope_count;
            o.kind = TSIR_IR_STMT_LOOP;
            o.loop_kind = s->kind == STATEMENT_FOR ? TSIR_IR_LOOP_FOR : s->kind == STATEMENT_WHILE ? TSIR_IR_LOOP_WHILE : TSIR_IR_LOOP_BARE;
            o.has_loop_init = s->initializer != NO_ID;
            if (o.has_loop_init) o.loop_init = statement(b, f, s->initializer, depth + 1u);
            o.has_loop_cond = s->value.condition != NO_ID;
            if (o.has_loop_cond) o.loop_cond = tsir_source_expr(b, f, s->value.condition, false, 0u);
            o.has_loop_continuing = s->update != NO_ID;
            if (o.has_loop_continuing) o.loop_continuing = statement(b, f, s->update, depth + 1u);
            uint32_t body_end = s->end;
            if (s->kind == STATEMENT_LOOP) {
                for (uint32_t child = s->body_start; child < s->end;
                    child = b->compiler->statements[child].end) {
                    if (b->compiler->statements[child].kind == STATEMENT_CONTINUING) {
                        body_end = child;
                        break;
                    }
                }
            }
            /* WGSL continuing can refer to declarations in the loop body.
             * Keep those names until the separately represented continuation
             * has been lowered, then restore the surrounding lexical scope. */
            o.loop_body = block_scope(b, f, s->body_start, body_end, depth + 1u,
                body_end != s->end);
            if (body_end != s->end) {
                o.has_loop_continuing = true;
                o.loop_continuing = statement(b, f, body_end, depth + 1u);
            }
            f->scope_count = scope;
            break;
        }
        case STATEMENT_CONTINUING:
        case STATEMENT_BLOCK: return block(b, f, s->body_start, s->end, depth + 1u);
        case STATEMENT_RETURN:
            o.kind = TSIR_IR_STMT_RETURN;
            o.has_return_expr = s->value.condition != NO_ID;
            if (o.has_return_expr) o.return_expr = tsir_source_expr(b, f, s->value.condition, false, 0u);
            break;
        case STATEMENT_BREAK: o.kind = TSIR_IR_STMT_BREAK; break;
        case STATEMENT_CONTINUE: o.kind = TSIR_IR_STMT_CONTINUE; break;
        case STATEMENT_DISCARD: o.kind = TSIR_IR_STMT_DISCARD; break;
        case STATEMENT_CALL:
            o.kind = TSIR_IR_STMT_EXPR;
            o.expr = tsir_source_expr(b, f, s->value.condition, false, 0u);
            break;
        case STATEMENT_EMPTY: return push(b, f, (TsirIrStmt){.kind = TSIR_IR_STMT_BLOCK});
        case STATEMENT_BREAK_IF: {
            uint32_t break_id = push(b, f, (TsirIrStmt){.kind = TSIR_IR_STMT_BREAK});
            if (f->view.stmt_child_count == f->stmt_cap) {
                (void)tsir_source_fail(b, s->source_offset, "TSIR break-if edge capacity exceeded");
                return TSIR_IR_NONE;
            }
            uint32_t child = (uint32_t)f->view.stmt_child_count++;
            f->children[child] = break_id;
            o.kind = TSIR_IR_STMT_IF;
            o.if_then = push(b, f, (TsirIrStmt){.kind = TSIR_IR_STMT_BLOCK,
                .range_start = child, .range_len = 1u});
            o.if_cond = tsir_source_expr(b, f, s->value.condition, false, 0u);
            break;
        }
        case STATEMENT_SWITCH: return switch_statement(b, f, s, depth);
        case STATEMENT_CASE:
            (void)tsir_source_fail(b, s->source_offset, "typed statement is outside the TSIR source adapter");
            return TSIR_IR_NONE;
    }
    return push(b, f, o);
}
bool tsir_source_function(TsirSourceBridge *b, TsirSourceFunction *f, uint32_t index) {
    const FunctionRecord *s = &b->compiler->functions[index];
    b->compiler->current_function = index;
    f->syntax = s;
    f->expr_cap = (size_t)(s->expression_end - s->expression_start) * 16u + 64u;
    f->stmt_cap = (size_t)(s->statement_end - s->statement_start) * 4u + 16u;
    f->local_cap = (size_t)(s->statement_end - s->statement_start) + 1u;
    f->exprs = calloc(f->expr_cap, sizeof(*f->exprs));
    f->args = calloc(f->expr_cap, sizeof(*f->args));
    f->stmts = calloc(f->stmt_cap, sizeof(*f->stmts));
    f->children = calloc(f->stmt_cap, sizeof(*f->children));
    f->locals = calloc(f->local_cap, sizeof(*f->locals));
    f->local_names = calloc(f->local_cap, sizeof(*f->local_names));
    f->scope = calloc(f->local_cap, sizeof(*f->scope));
    const EntryRecord *entry = (s->flags & FUNCTION_ENTRY) != 0u ? &b->compiler->entries[s->entry_index] : NULL;
    f->view.param_count = entry ? entry->input_count : s->formal_count;
    f->params = calloc(f->view.param_count + 1u, sizeof(*f->params));
    if (!f->exprs || !f->args || !f->stmts || !f->children || !f->locals || !f->local_names || !f->scope || !f->params) return false;
    f->view.name = tsir_source_name(b, s->name);
    f->view.return_type = s->result_type;
    f->view.params = f->params;
    f->view.locals = f->locals;
    f->view.exprs = f->exprs;
    f->view.expr_args = f->args;
    f->view.stmts = f->stmts;
    f->view.stmt_children = f->children;
    for (size_t i = 0u; i < f->view.param_count; ++i) {
        TsirIrParam *p = &f->params[i];
        if (entry) {
            const EntryInput *input = &b->compiler->entry_inputs[entry->input_first + i];
            p->name = tsir_source_name(b, input->name);
            p->has_io = true;
            p->builtin = input->kind == INPUT_GLOBAL_INVOCATION ? TSIR_IR_BUILTIN_GLOBAL_INVOCATION_ID : TSIR_IR_BUILTIN_OTHER;
            p->ty = IR_U32;
            if (!input_scalar(input->kind)) {
                for (size_t t = 0; t < b->module.type_count; ++t)
                    if (b->types[t].kind == TSIR_IR_TYPE_VECTOR && b->types[t].elem == IR_U32 && b->types[t].len == 3u) p->ty = (uint32_t)t;
            }
        } else {
            const FormalRecord *formal = &b->compiler->formals[s->formal_start + i];
            p->name = tsir_source_name(b, formal->name);
            p->ty = formal->type.resolved.type;
        }
    }
    for (size_t i = 0u; i < 3u; ++i) f->view.workgroup_size[i] = entry ? entry->state.resolved.workgroup_size[i] : 1u;
    f->view.root_stmt = block(b, f, s->statement_start, s->statement_end, 0u);
    return b->status == TSIR_OK;
}
