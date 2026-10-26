/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TARGET_SOURCE_FACTS_H
#define DOE_TARGET_SOURCE_FACTS_H
#include "../src/internal.h"
#include <string.h>

/* Read-only structural facts over the shared, semantically admitted syntax.
 * This does not evaluate constants, infer intervals, or mutate source/IR. */
typedef struct TargetSourceLoopGuard {
    uint32_t limit, step;
    bool inclusive;
} TargetSourceLoopGuard;

static inline bool target_source_same_name(const Source *source, NameSpan a, NameSpan b) {
    return a.length == b.length && a.offset <= source->length &&
        a.length <= source->length - a.offset && b.offset <= source->length &&
        b.length <= source->length - b.offset &&
        memcmp(source->source + a.offset, source->source + b.offset, a.length) == 0;
}
static inline bool target_source_name_expr(const Source *source, uint32_t expression, NameSpan name) {
    const CompilerWorkspace *c = source->compiler;
    return expression < c->expression_count && c->expressions[expression].kind == EXPRESSION_NAME &&
        target_source_same_name(source, c->expressions[expression].value.name, name);
}
/* A direct unsigned counter inside an unchanged for-body is bounded by its
 * dominating condition. Return expressions for the canonical constant owner
 * to evaluate; callers retain address-space and target elision policy. The
 * positive update shape deliberately matches the retained simple-for case. */
static inline bool target_source_loop_guard(const Source *source, const FunctionRecord *function,
    uint32_t statement, NameSpan counter, uint32_t declaration_offset, uint32_t cursor,
    TargetSourceLoopGuard *out) {
    const CompilerWorkspace *c = source->compiler;
    if (function == NULL || statement >= c->statement_count || cursor >= c->statement_count ||
        cursor < function->statement_start || cursor >= function->statement_end) return false;
    const Statement *loop = &c->statements[cursor];
    if (loop->kind != STATEMENT_FOR || statement < loop->body_start || statement >= loop->end ||
        loop->end > c->statement_count || loop->initializer >= c->statement_count ||
        loop->update >= c->statement_count || loop->value.condition >= c->expression_count) return false;
    const Statement *initial = &c->statements[loop->initializer];
    if (initial->kind == STATEMENT_VAR) {
        if (initial->source_offset != declaration_offset ||
            (initial->value.local.flags & LOCAL_INITIALIZED) == 0u ||
            !target_source_same_name(source, initial->value.local.name, counter)) return false;
    } else if (initial->kind != STATEMENT_ASSIGN || declaration_offset >= loop->source_offset ||
        initial->value.assignment.kind != TARGET_ROOT || initial->value.assignment.operation != UPDATE_SET ||
        !target_source_same_name(source, initial->value.assignment.root, counter)) return false;
    for (uint32_t i = loop->body_start; i < loop->end; ++i) {
        const Statement *body = &c->statements[i];
        if ((body->kind == STATEMENT_ASSIGN &&
             target_source_same_name(source, body->value.assignment.root, counter)) ||
            ((body->kind == STATEMENT_VAR || body->kind == STATEMENT_LET || body->kind == STATEMENT_CONST) &&
             target_source_same_name(source, body->value.local.name, counter))) return false;
    }
    /* A pointer to this local can outlive the guard and invalidate its fact.
     * Reject address-taking throughout the function, including before loops. */
    if (function->expression_end > c->expression_count) return false;
    for (uint32_t i = function->expression_start; i < function->expression_end; ++i) {
        const Expression *expression = &c->expressions[i];
        if (expression->kind == EXPRESSION_ADDRESS &&
            target_source_name_expr(source, expression->left, counter)) return false;
    }
    const Statement *update = &c->statements[loop->update];
    if (update->kind != STATEMENT_ASSIGN || update->value.assignment.kind != TARGET_ROOT ||
        !target_source_same_name(source, update->value.assignment.root, counter)) return false;
    const Assignment *assignment = &update->value.assignment;
    if (assignment->operation == UPDATE_INCREMENT) out->step = NO_ID;
    else if (assignment->operation == UPDATE_ADD) out->step = assignment->expression;
    else if (assignment->operation == UPDATE_SET && assignment->expression < c->expression_count) {
        const Expression *sum = &c->expressions[assignment->expression];
        if (sum->kind != EXPRESSION_ADD || !target_source_name_expr(source, sum->left, counter)) return false;
        out->step = sum->right;
    } else return false;
    const Expression *condition = &c->expressions[loop->value.condition];
    if (condition->kind != EXPRESSION_COMPARE) return false;
    if (target_source_name_expr(source, condition->left, counter) &&
        (condition->value.comparison == COMPARE_LESS || condition->value.comparison == COMPARE_LESS_EQUAL)) {
        out->limit = condition->right;
        out->inclusive = condition->value.comparison == COMPARE_LESS_EQUAL;
    } else if (target_source_name_expr(source, condition->right, counter) &&
        (condition->value.comparison == COMPARE_GREATER || condition->value.comparison == COMPARE_GREATER_EQUAL)) {
        out->limit = condition->left;
        out->inclusive = condition->value.comparison == COMPARE_GREATER_EQUAL;
    } else return false;
    return out->limit < c->expression_count && (out->step == NO_ID || out->step < c->expression_count);
}
#endif
