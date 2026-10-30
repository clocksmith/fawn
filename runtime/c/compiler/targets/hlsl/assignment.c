/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Definite assignment for HLSL value locals. A value needs no zero initializer
 * only when its first assignment is written unconditionally at its own statement
 * (hlsl_define) and every later reference lies after it, in the same structured
 * region or one nested inside it. Every hlsl_block call is one region: if/else
 * arms, switch cases, short-circuit right sides and the loop header, body and
 * continuing are separate regions, so a value defined in a loop body and read in
 * its continuing block (reachable through continue) keeps its initializer. Any
 * reference before the definition, outside its region, or without a recorded
 * definition keeps the initializer, as does any function the bounded state
 * cannot cover. The analysis walks the same emission code without output. */
#include "private.h"

static bool local_index(const HlslAssignment *a, uint32_t id, uint32_t *index) {
    if (a == NULL || !a->valid || id == 0u || id - 1u < a->first || id - 1u - a->first >= a->count) return false;
    *index = id - 1u - a->first;
    return true;
}
void hlsl_define(HlslEmitter *e, uint32_t id) {
    HlslAssignment *a = e->assignment;
    uint32_t index;
    if (!local_index(a, id, &index) || a->defined[index] != 0u) return;
    if (a->depth == 0u || a->sequence == UINT32_MAX) { a->valid = false; return; }
    a->defined[index] = ++a->sequence;
    a->region[index] = a->stack[a->depth - 1u];
}
void hlsl_assignment_use(HlslEmitter *e, uint32_t id) {
    HlslAssignment *a = e->assignment;
    uint32_t index;
    if (!local_index(a, id, &index)) return;
    if (a->defined[index] == 0u) { a->unsafe[index] = true; return; }
    for (uint32_t i = 0u; i < a->depth; ++i)
        if (a->stack[i] == a->region[index]) return;
    a->unsafe[index] = true;
}
bool hlsl_assignment_enter(HlslEmitter *e) {
    HlslAssignment *a = e->assignment;
    if (a == NULL) return true;
    if (a->depth >= DOE_HLSL_MAX_DEPTH + 2u || a->regions == UINT32_MAX) { a->valid = false; return false; }
    a->stack[a->depth++] = ++a->regions;
    return true;
}
void hlsl_assignment_leave(HlslEmitter *e) {
    HlslAssignment *a = e->assignment;
    if (a != NULL && a->depth != 0u) --a->depth;
}
void hlsl_assignment_analyze(HlslEmitter *e, uint32_t function, HlslAssignment *a) {
    const FunctionRecord *f = &e->module->functions[function];
    a->valid = false;
    if (f->instruction_end - f->instruction_start > HLSL_ASSIGNMENT_VALUES) return;
    a->first = f->instruction_start;
    a->count = f->instruction_end - f->instruction_start;
    a->sequence = a->regions = a->depth = 0u;
    for (uint32_t i = 0u; i < a->count; ++i) {
        a->defined[i] = 0u;
        a->unsafe[i] = false;
    }
    a->valid = true;
    /* The walk writes no text and leaves no emitter state behind. */
    const HlslEmitter saved = *e;
    e->output = NULL;
    e->position = 0u;
    e->assignment = a;
    e->function_end = f->instruction_end;
    uint32_t cursor = f->instruction_start;
    hlsl_block(e, &cursor, 0u, NO_ID, false);
    const bool walked = !e->failed && cursor == f->instruction_end;
    *e = saved;
    if (!walked) a->valid = false;
}
bool hlsl_assigned(const HlslAssignment *a, uint32_t id) {
    uint32_t index;
    return local_index(a, id, &index) && a->defined[index] != 0u && !a->unsafe[index];
}
