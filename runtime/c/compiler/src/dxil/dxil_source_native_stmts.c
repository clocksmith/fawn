/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_source_native_internal.h"
#include "dxil_source_native_bounds.h"
#include "dxil_source_native_opt.h"
#include "../sema.h"
#include <stdlib.h>
#include <string.h>

static uint32_t statement(DxilSourceBridge *, DxilSourceFunction *, uint32_t, unsigned);
static uint32_t push(DxilSourceBridge *b, DxilSourceFunction *f, DxilIrStmt node) {
    if (f->view->stmt_count == DXIL_IR_MAX_STMTS) {
        (void)dxil_source_native_fail(b, 0u, DXIL_OUTPUT_TOO_LARGE, "native DXIL statement adaptation capacity exceeded");
        return DXIL_IR_INVALID_ID;
    }
    uint32_t id = (uint32_t)f->view->stmt_count++;
    f->view->stmts[id] = node;
    return id;
}
static uint32_t block_scope(DxilSourceBridge *b, DxilSourceFunction *f, uint32_t first, uint32_t end, unsigned depth, bool retain_scope) {
    uint32_t scope = f->scope_count;
    uint32_t count = 0u;
    for (uint32_t i = first; i < end;) {
        if (b->compiler->statements[i].end <= i) {
            (void)dxil_source_native_fail(b, 0u, DXIL_OUTPUT_TOO_LARGE, "invalid typed statement range");
            return DXIL_IR_INVALID_ID;
        }
        ++count;
        i = b->compiler->statements[i].end;
    }
    if (count > DXIL_IR_MAX_STMTS - f->view->stmt_child_count) {
        (void)dxil_source_native_fail(b, 0u, DXIL_OUTPUT_TOO_LARGE, "native DXIL block edge capacity exceeded");
        return DXIL_IR_INVALID_ID;
    }
    uint32_t start = (uint32_t)f->view->stmt_child_count;
    f->view->stmt_child_count += count;
    uint32_t slot = start;
    for (uint32_t i = first; b->status == DXIL_OK && i < end; i = b->compiler->statements[i].end)
        f->view->stmt_children[slot++] = statement(b, f, i, depth + 1u);
    if (!retain_scope) f->scope_count = scope;
    return push(b, f, (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK, .range.start = start, .range.len = count});
}
static uint32_t block(DxilSourceBridge *b, DxilSourceFunction *f, uint32_t first, uint32_t end, unsigned depth) {
    return block_scope(b, f, first, end, depth, false);
}
static bool assignment_type(DxilSourceBridge *b, DxilSourceFunction *f,
    const Assignment *assignment, DxilIrExpr *target) {
    if (target->base >= f->view->expr_count) return false;
    TypeId type = f->view->exprs[target->base].ty;
    if (type < IR_PRIMITIVE_COUNT || type - IR_PRIMITIVE_COUNT >= b->compiler->type_count)
        return dxil_source_native_fail(b, assignment->root.offset, DXIL_INVALID_IR, "invalid typed assignment aggregate");
    const TypeRecord *record = &b->compiler->types[type - IR_PRIMITIVE_COUNT];
    target->category = DXIL_IR_EXPR_REF;
    if (assignment->kind == TARGET_INDEX) {
        target->ty = dxil_source_native_type(b, record->element);
        return b->status == DXIL_OK;
    }
    const NameSpan member = source_name_at(&b->source, assignment->index);
    if (record->kind == TYPE_STRUCT) {
        for (uint32_t i = 0u; i < record->count; ++i) {
            const MemberRecord *field = &b->compiler->members[record->element + i];
            if (source_spans_equal(&b->source, member, field->name)) {
                target->field_index = i;
                target->ty = dxil_source_native_type(b, field->type);
                return b->status == DXIL_OK;
            }
        }
    } else if (record->kind == TYPE_VECTOR && member.length == 1u) {
        const char character = b->source.source[member.offset];
        const char *lane = strchr("xyzw", character);
        if (lane != NULL) target->field_index = (uint32_t)(lane - "xyzw");
        else {
            lane = strchr("rgba", character);
            if (lane == NULL) return false;
            target->field_index = (uint32_t)(lane - "rgba");
        }
        target->ty = record->element;
        return true;
    } else if (record->kind == TYPE_ATOMIC_RESULT || record->kind == TYPE_FREXP_RESULT || record->kind == TYPE_MODF_RESULT) {
        const bool first = source_span_is(&b->source, member,
            record->kind == TYPE_ATOMIC_RESULT ? "old_value" : "fract");
        target->field_index = first ? 0u : 1u;
        target->ty = first ? record->element : record->kind == TYPE_ATOMIC_RESULT ? IR_BOOL : record->state.result.second;
        return true;
    }
    return dxil_source_native_fail(b, member.offset, DXIL_INVALID_IR, "missing typed assignment member");
}
static uint32_t switch_statement(DxilSourceBridge *b, DxilSourceFunction *f,
    const Statement *s, unsigned depth) {
    DxilIrStmt output = {.kind = DXIL_IR_STMT_SWITCH};
    output.switch_expr = dxil_source_native_expr(b, f, s->value.condition, false, 0u);
    for (uint32_t first = s->body_start; first < s->end;) {
        const Statement *clause = &b->compiler->statements[first];
        if (clause->kind != STATEMENT_CASE || clause->body_start <= first ||
            clause->body_start > clause->end || clause->end > s->end) {
            (void)dxil_source_native_fail(b, s->source_offset, DXIL_INVALID_IR, "invalid typed switch range");
            return DXIL_IR_INVALID_ID;
        }
        ++output.cases.len;
        first = clause->end;
    }
    if (output.cases.len > DXIL_IR_MAX_SWITCH_CASES - f->view->switch_case_count) {
        (void)dxil_source_native_fail(b, s->source_offset, DXIL_OUTPUT_TOO_LARGE, "native DXIL switch case capacity exceeded");
        return DXIL_IR_INVALID_ID;
    }
    output.cases.start = f->view->switch_case_count;
    f->view->switch_case_count += output.cases.len;
    uint32_t next = output.cases.start;
    for (uint32_t first = s->body_start; b->status == DXIL_OK && first < s->end;) {
        const Statement *clause = &b->compiler->statements[first];
        DxilIrSwitchCase *item = &f->view->switch_cases[next++];
        item->selectors.start = f->view->switch_selector_count;
        for (uint32_t selector = first; selector < clause->body_start; ++selector) {
            const Statement *entry = &b->compiler->statements[selector];
            if (entry->value.condition == NO_ID) item->is_default = true;
            else {
                if (f->view->switch_selector_count == DXIL_IR_MAX_SWITCH_SELECTORS) {
                    (void)dxil_source_native_fail(b, entry->source_offset, DXIL_OUTPUT_TOO_LARGE, "native DXIL switch selector capacity exceeded");
                    return DXIL_IR_INVALID_ID;
                }
                f->view->switch_selectors[f->view->switch_selector_count++] =
                    dxil_source_native_expr(b, f, entry->value.condition, false, 0u);
                ++item->selectors.len;
            }
        }
        item->body = block(b, f, clause->body_start, clause->end, depth + 1u);
        first = clause->end;
    }
    return push(b, f, output);
}
static DxilIrAssignOp assignment(UpdateKind kind) {
    switch (kind) {
        case UPDATE_ADD: case UPDATE_INCREMENT: return DXIL_IR_ASSIGN_ADD;
        case UPDATE_SUBTRACT: case UPDATE_DECREMENT: return DXIL_IR_ASSIGN_SUB;
        case UPDATE_MULTIPLY: return DXIL_IR_ASSIGN_MUL;
        case UPDATE_DIVIDE: return DXIL_IR_ASSIGN_DIV;
        case UPDATE_REMAINDER: return DXIL_IR_ASSIGN_REM;
        case UPDATE_BIT_AND: return DXIL_IR_ASSIGN_BIT_AND;
        case UPDATE_BIT_OR: return DXIL_IR_ASSIGN_BIT_OR;
        case UPDATE_BIT_XOR: return DXIL_IR_ASSIGN_BIT_XOR;
        case UPDATE_SHIFT_LEFT: return DXIL_IR_ASSIGN_SHIFT_LEFT;
        case UPDATE_SHIFT_RIGHT: return DXIL_IR_ASSIGN_SHIFT_RIGHT;
        case UPDATE_SET: return DXIL_IR_ASSIGN_ASSIGN;
    }
    return DXIL_IR_ASSIGN_ASSIGN;
}
static uint32_t statement_impl(DxilSourceBridge *, DxilSourceFunction *, uint32_t, unsigned);
static uint32_t statement(DxilSourceBridge *b, DxilSourceFunction *f, uint32_t id, unsigned depth) {
    const uint32_t saved = b->source_statement;
    b->source_statement = id;
    const uint32_t result = statement_impl(b, f, id, depth);
    b->source_statement = saved;
    return result;
}
static uint32_t statement_impl(DxilSourceBridge *b, DxilSourceFunction *f, uint32_t id, unsigned depth) {
    if (b->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    if (id >= b->compiler->statement_count || depth > DXIL_IR_MAX_STMTS) {
        (void)dxil_source_native_fail(b, 0u, DXIL_OUTPUT_TOO_LARGE, "native DXIL statement traversal exceeded its bound");
        return DXIL_IR_INVALID_ID;
    }
    const Statement *s = &b->compiler->statements[id];
    DxilIrStmt o = {0};
    switch (s->kind) {
        case STATEMENT_LET: case STATEMENT_VAR: case STATEMENT_CONST: {
            const LocalSyntax *l = &s->value.local;
            o.kind = DXIL_IR_STMT_LOCAL_DECL;
            o.has_initializer = (l->flags & LOCAL_INITIALIZED) != 0u;
            if (o.has_initializer) o.initializer = dxil_source_native_expr(b, f, l->expression, false, 0u);
            TypeId ty = o.has_initializer ? b->compiler->expressions[l->expression].resolved_type : IR_VOID;
            if ((l->flags & LOCAL_ANNOTATED) != 0u) {
                LocalScope empty = {0};
                if (!sema_type_reference(&b->source, l->annotation, &empty, &ty)) {
                    b->status = DXIL_UNSUPPORTED_CONSTRUCT;
                    return DXIL_IR_INVALID_ID;
                }
            } else if (s->kind != STATEMENT_CONST) {
                if (ty == IR_ABSTRACT_INT) ty = IR_I32;
                if (ty == IR_ABSTRACT_FLOAT) ty = IR_F32;
            }
            if (f->view->local_count == DXIL_IR_MAX_LOCALS) {
                (void)dxil_source_native_fail(b, s->source_offset, DXIL_OUTPUT_TOO_LARGE, "native DXIL local capacity exceeded");
                return DXIL_IR_INVALID_ID;
            }
            o.local = (uint32_t)f->view->local_count++;
            o.is_const = s->kind != STATEMENT_VAR;
            f->view->locals[o.local] = (DxilIrLocal){.name = dxil_source_native_name(b, l->name), .ty = ty, .mutable = s->kind == STATEMENT_VAR};
            f->local_names[o.local] = l->name;
            f->bindings[o.local] = (LocalBinding){.name = l->name, .type = ty,
                .declaration_offset = s->source_offset,
                .flags = s->kind == STATEMENT_CONST ? LOCAL_CONSTANT : s->kind == STATEMENT_VAR ? LOCAL_MUTABLE : 0u};
            if (s->kind == STATEMENT_VAR) {
                /* Preserve the canonical mutable identity used by address and
                 * reference metadata when the shared evaluator revisits a value. */
                bool found = false;
                for (uint32_t i = f->syntax->instruction_start; i < f->syntax->instruction_end; ++i) {
                    const IrInstruction *instruction = &b->compiler->instructions[i];
                    if (instruction->opcode == IR_LOCAL_STORE && instruction->source_offset == l->name.offset) {
                        f->bindings[o.local].local_id = instruction->operands.scalar.binding;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    (void)dxil_source_native_fail(b, l->name.offset, DXIL_INVALID_IR,
                        "native local has no canonical mutable identity");
                    return DXIL_IR_INVALID_ID;
                }
            }
            if (s->kind == STATEMENT_CONST &&
                !dxil_source_native_constant(b, f, l->expression, &f->bindings[o.local].constant_value))
                return DXIL_IR_INVALID_ID;
            if (o.has_initializer && type_pointer(b->compiler, ty)) {
                ExpressionValue pointer;
                if (!dxil_source_native_value(b, f, l->expression, &pointer)) return DXIL_IR_INVALID_ID;
                f->bindings[o.local].pointer_value = pointer.pointer;
            }
            f->scope[f->scope_count++] = o.local;
            break;
        }
        case STATEMENT_ASSIGN: {
            const Assignment *a = &s->value.assignment;
            if (a->kind == TARGET_ROOT && source_span_is(&b->source, a->root, "_")) {
                o.kind = DXIL_IR_STMT_EXPR;
                o.expr = dxil_source_native_expr(b, f, a->expression, false, 0u);
                break;
            }
            o.kind = DXIL_IR_STMT_ASSIGN;
            if (a->kind == TARGET_ROOT) o.assign_lhs = dxil_source_native_root(b, f, a->root, true);
            else if (a->kind == TARGET_REFERENCE) o.assign_lhs = dxil_source_native_expr(b, f, a->path, true, 0u);
            else {
                uint32_t base = a->path == NO_ID ? dxil_source_native_root(b, f, a->root, true) :
                    dxil_source_native_expr(b, f, a->path, true, 0u);
                DxilIrExpr target = {0};
                if (a->kind == TARGET_INDEX) {
                    target.kind = DXIL_IR_EXPR_INDEX;
                    target.base = base;
                    target.index_expr = dxil_source_native_expr(b, f, a->index, false, 0u);
                } else {
                    target.kind = DXIL_IR_EXPR_MEMBER;
                    target.base = base;
                    target.field_name = dxil_source_native_name(b, source_name_at(&b->source, a->index));
                }
                if (!assignment_type(b, f, a, &target)) return DXIL_IR_INVALID_ID;
                o.assign_lhs = dxil_source_native_push_expr(b, f, target);
                if (a->kind == TARGET_INDEX &&
                    dxil_source_native_bound_index(b, f, o.assign_lhs, a->index) == DXIL_IR_INVALID_ID) return DXIL_IR_INVALID_ID;
            }
            o.assign_op = assignment(a->operation);
            if (a->expression != NO_ID) o.assign_rhs = dxil_source_native_expr(b, f, a->expression, false, 0u);
            else o.assign_rhs = dxil_source_native_push_expr(b, f, (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT,
                .ty = IR_U32, .uint_value = 1u});
            break;
        }
        case STATEMENT_IF:
            o.kind = DXIL_IR_STMT_IF;
            o.cond = dxil_source_native_expr(b, f, s->value.condition, false, 0u);
            o.then_block = block(b, f, s->body_start, s->else_start, depth + 1u);
            o.has_else_block = s->else_start < s->end;
            if (o.has_else_block) o.else_block = block(b, f, s->else_start, s->end, depth + 1u);
            break;
        case STATEMENT_FOR: case STATEMENT_WHILE: case STATEMENT_LOOP: {
            uint32_t scope = f->scope_count;
            o.kind = DXIL_IR_STMT_LOOP;
            o.loop_kind = s->kind == STATEMENT_FOR ? DXIL_IR_LOOP_FOR : s->kind == STATEMENT_WHILE ? DXIL_IR_LOOP_WHILE : DXIL_IR_LOOP_LOOP;
            o.has_init = s->initializer != NO_ID;
            if (o.has_init) o.init = statement(b, f, s->initializer, depth + 1u);
            o.has_cond = s->value.condition != NO_ID;
            if (o.has_cond) o.cond = dxil_source_native_expr(b, f, s->value.condition, false, 0u);
            o.has_continuing = s->update != NO_ID;
            if (o.has_continuing) o.continuing = statement(b, f, s->update, depth + 1u);
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
            o.body = block_scope(b, f, s->body_start, body_end, depth + 1u,
                body_end != s->end);
            if (body_end != s->end) {
                o.has_continuing = true;
                o.continuing = statement(b, f, body_end, depth + 1u);
            }
            f->scope_count = scope;
            break;
        }
        case STATEMENT_CONTINUING:
        case STATEMENT_BLOCK: return block(b, f, s->body_start, s->end, depth + 1u);
        case STATEMENT_RETURN:
            o.kind = DXIL_IR_STMT_RETURN;
            o.has_return_value = s->value.condition != NO_ID;
            if (o.has_return_value) o.return_value = dxil_source_native_expr(b, f, s->value.condition, false, 0u);
            break;
        case STATEMENT_BREAK: o.kind = DXIL_IR_STMT_BREAK; break;
        case STATEMENT_CONTINUE: o.kind = DXIL_IR_STMT_CONTINUE; break;
        case STATEMENT_DISCARD: o.kind = DXIL_IR_STMT_DISCARD; break;
        case STATEMENT_CALL:
            o.kind = DXIL_IR_STMT_EXPR;
            o.expr = dxil_source_native_expr(b, f, s->value.condition, false, 0u);
            break;
        case STATEMENT_EMPTY: return push(b, f, (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK});
        case STATEMENT_BREAK_IF: {
            uint32_t break_id = push(b, f, (DxilIrStmt){.kind = DXIL_IR_STMT_BREAK});
            if (f->view->stmt_child_count == DXIL_IR_MAX_STMTS) {
                (void)dxil_source_native_fail(b, s->source_offset, DXIL_OUTPUT_TOO_LARGE, "native DXIL break-if edge capacity exceeded");
                return DXIL_IR_INVALID_ID;
            }
            uint32_t child = (uint32_t)f->view->stmt_child_count++;
            f->view->stmt_children[child] = break_id;
            o.kind = DXIL_IR_STMT_IF;
            o.then_block = push(b, f, (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK,
                .range.start = child, .range.len = 1u});
            o.cond = dxil_source_native_expr(b, f, s->value.condition, false, 0u);
            break;
        }
        case STATEMENT_SWITCH: return switch_statement(b, f, s, depth);
        case STATEMENT_CASE:
            (void)dxil_source_native_fail(b, s->source_offset, DXIL_OUTPUT_TOO_LARGE, "typed statement is outside the native DXIL source adapter");
            return DXIL_IR_INVALID_ID;
    }
    return push(b, f, o);
}
bool dxil_source_native_function(DxilSourceBridge *b, uint32_t index) {
    DxilSourceFunction function = {.view = &b->module->functions[index],
        .syntax = &b->compiler->functions[index]};
    b->compiler->current_function = index;
    function.view->root_stmt = block(b, &function, function.syntax->statement_start,
        function.syntax->statement_end, 0u);
    return b->status == DXIL_OK && dxil_source_native_optimize(b, &function);
}
