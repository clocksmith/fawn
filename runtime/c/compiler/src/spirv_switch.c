/* SPDX-License-Identifier: MIT
 * Typed switch control consumes the same function block emitter and output pool. */
#include "spirv_internal.h"

bool spirv_emit_switch(Emitter *e, const IrProgram *program, size_t *cursor,
                       unsigned depth, EmitLoop *loop, bool *live) {
    const IrInstruction *begin = &program->instructions[(*cursor)++];
    if (depth >= e->source->limits->control_depth + e->source->limits->expression_depth)
        return spirv_emit_error(e, begin->source_offset, "switch target traversal depth exceeded");
    const bool active = *live;
    const uint32_t outer_merge = e->switch_merge;
    size_t finish = *cursor, values = 0u;
    uint32_t groups = 0u, nested = 0u;
    for (; finish < e->function_end; ++finish) {
        const IrInstruction *ir = &program->instructions[finish];
        if (ir->opcode == IR_SWITCH_BEGIN) ++nested;
        else if (ir->opcode == IR_END_SWITCH) {
            if (!nested) break;
            --nested;
        } else if (!nested) {
            if (ir->opcode == IR_SWITCH_CASE && !ir->operands.scalar.right) ++values;
            if (ir->opcode == IR_END_CASE) ++groups;
        }
    }
    if (finish == e->function_end || !groups)
        return spirv_emit_error(e, begin->source_offset, "switch boundaries are missing");
    uint32_t labels = 0u, merge = 0u;
    if (active) {
        labels = spirv_reserve_ids(e, groups);
        merge = spirv_reserve_ids(e, 1u);
        EMIT(OP_SELECTION_MERGE, merge, 0u);
        uint32_t *words;
        if (values > (SIZE_MAX - 2u) / 2u || !spirv_instruction_space(e, OP_SWITCH, 2u + values * 2u, &words))
            return false;
        words[0] = e->values[begin->operands.scalar.left];
        words[1] = merge;
        size_t output = 2u;
        uint32_t group = 0u;
        nested = 0u;
        for (size_t i = *cursor; i < finish; ++i) {
            const IrInstruction *ir = &program->instructions[i];
            if (ir->opcode == IR_SWITCH_BEGIN) ++nested;
            else if (ir->opcode == IR_END_SWITCH) --nested;
            else if (!nested && ir->opcode == IR_END_CASE) ++group;
            else if (!nested && ir->opcode == IR_SWITCH_CASE) {
                if (ir->operands.scalar.right) words[1] = labels + group;
                else {
                    words[output++] = ir->operands.scalar.literal;
                    words[output++] = labels + group;
                }
            }
        }
    }
    e->switch_merge = merge;
    uint32_t group = 0u;
    while (*cursor < finish) {
        if (program->instructions[*cursor].opcode != IR_SWITCH_CASE)
            return spirv_emit_error(e, begin->source_offset, "switch clause header is missing");
        while (*cursor < finish && program->instructions[*cursor].opcode == IR_SWITCH_CASE) ++*cursor;
        if (active) { EMIT(OP_LABEL, labels + group); e->current_label = labels + group; }
        bool body_live = active;
        if (!spirv_emit_block(e, program, cursor, depth + 1u, loop, &body_live)) return false;
        if (*cursor >= finish || program->instructions[*cursor].opcode != IR_END_CASE)
            return spirv_emit_error(e, begin->source_offset, "switch clause end is missing");
        ++*cursor;
        ++group;
        if (body_live) { EMIT(OP_BRANCH, merge); }
    }
    ++*cursor;
    e->switch_merge = outer_merge;
    *live = active && (begin->operands.scalar.literal & 1u) != 0u;
    if (active) {
        EMIT(OP_LABEL, merge);
        e->current_label = merge;
        if (!*live && !spirv_instruction(e, OP_UNREACHABLE, NULL, 0u)) return false;
    }
    return !e->failed;
}
