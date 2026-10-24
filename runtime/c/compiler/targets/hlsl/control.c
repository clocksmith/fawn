/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"

static bool marker(HlslEmitter *e, uint32_t cursor, IrOpcode opcode) {
    if (cursor < e->function_end && e->module->instructions[cursor].opcode == opcode) return true;
    hlsl_error(e, "Hlsl structured IR is missing its required boundary");
    return false;
}
static void selection(HlslEmitter *e, uint32_t *cursor, unsigned depth, uint32_t loop, bool continuing) {
    const IrInstruction *ir = &e->module->instructions[(*cursor)++];
    hlsl_indent(e);
    hlsl_text(e, "if (");
    hlsl_value(e, ir->operands.scalar.left);
    hlsl_text(e, ") {\n");
    ++e->indent;
    hlsl_block(e, cursor, depth + 1u, loop, continuing);
    --e->indent;
    if (!marker(e, *cursor, IR_ELSE)) return;
    ++*cursor;
    hlsl_line(e, "} else {");
    ++e->indent;
    hlsl_block(e, cursor, depth + 1u, loop, continuing);
    --e->indent;
    if (!marker(e, *cursor, IR_END_IF)) return;
    ++*cursor;
    hlsl_line(e, "}");
}
static void short_circuit(HlslEmitter *e, uint32_t *cursor, unsigned depth, uint32_t loop, bool continuing) {
    const IrInstruction *begin = &e->module->instructions[(*cursor)++];
    const uint32_t start = *cursor;
    unsigned nesting = 0u;
    uint32_t finish = start;
    for (; finish < e->function_end; ++finish) {
        const IrOpcode opcode = e->module->instructions[finish].opcode;
        if (opcode == IR_SHORT_BEGIN) ++nesting;
        if (opcode == IR_SHORT_END) {
            if (nesting == 0u) break;
            --nesting;
        }
    }
    if (!marker(e, finish, IR_SHORT_END)) return;
    const IrInstruction *end = &e->module->instructions[finish];
    hlsl_indent(e);
    hlsl_value(e, end->result);
    hlsl_text(e, " = ");
    hlsl_value(e, begin->operands.scalar.left);
    hlsl_text(e, ";\n");
    hlsl_indent(e);
    hlsl_text(e, begin->operands.scalar.literal != 0u ? "if (!" : "if (");
    hlsl_value(e, begin->operands.scalar.left);
    hlsl_text(e, ") {\n");
    ++e->indent;
    hlsl_block(e, cursor, depth + 1u, loop, continuing);
    if (*cursor != finish) {
        hlsl_error(e, "Hlsl short-circuit boundaries are inconsistent");
        return;
    }
    hlsl_indent(e);
    hlsl_value(e, end->result);
    hlsl_text(e, " = ");
    hlsl_value(e, end->operands.scalar.right);
    hlsl_text(e, ";\n");
    --e->indent;
    hlsl_line(e, "}");
    ++*cursor;
}
static void loop_body(HlslEmitter *e, uint32_t *cursor, unsigned depth) {
    const uint32_t loop = (*cursor)++;
    hlsl_line(e, "while (true) {");
    ++e->indent;
    hlsl_block(e, cursor, depth + 1u, loop, false);
    if (!marker(e, *cursor, IR_LOOP_TEST)) return;
    const IrInstruction *test = &e->module->instructions[(*cursor)++];
    if (test->operands.scalar.left != 0u) {
        hlsl_indent(e);
        hlsl_text(e, "if (!");
        hlsl_value(e, test->operands.scalar.left);
        hlsl_text(e, ") break;\n");
    }
    hlsl_indent(e);
    hlsl_format(e, "bool break%u = false;\n", loop);
    hlsl_line(e, "do {");
    ++e->indent;
    hlsl_block(e, cursor, depth + 1u, loop, false);
    --e->indent;
    if (!marker(e, *cursor, IR_LOOP_CONTINUING)) return;
    ++*cursor;
    hlsl_line(e, "} while (false);");
    hlsl_indent(e);
    hlsl_format(e, "if (break%u) break;\n", loop);
    hlsl_block(e, cursor, depth + 1u, loop, true);
    if (!marker(e, *cursor, IR_END_LOOP)) return;
    ++*cursor;
    --e->indent;
    hlsl_line(e, "}");
}
void hlsl_block(HlslEmitter *e, uint32_t *cursor, unsigned depth, uint32_t loop, bool continuing) {
    if (depth > DOE_HLSL_MAX_DEPTH) {
        hlsl_error(e, "Hlsl structured control exceeds target traversal budget");
        return;
    }
    while (*cursor < e->function_end && !e->failed) {
        const IrInstruction *ir = &e->module->instructions[*cursor];
        e->offset = ir->source_offset;
        if (ir->opcode == IR_ELSE || ir->opcode == IR_END_IF || ir->opcode == IR_SHORT_END ||
            ir->opcode == IR_LOOP_TEST || ir->opcode == IR_LOOP_CONTINUING || ir->opcode == IR_END_LOOP) return;
        if (ir->opcode == IR_IF) {
            selection(e, cursor, depth, loop, continuing);
            continue;
        }
        if (ir->opcode == IR_SHORT_BEGIN) {
            short_circuit(e, cursor, depth, loop, continuing);
            continue;
        }
        if (ir->opcode == IR_LOOP_BEGIN) {
            loop_body(e, cursor, depth);
            continue;
        }
        ++*cursor;
        if (ir->opcode == IR_ACCESS_LOAD || ir->opcode == IR_ACCESS_STORE || ir->opcode == IR_VALUE_ACCESS ||
            atomic_opcode(ir->opcode)) {
            hlsl_access(e, ir);
            continue;
        }
        if (ir->opcode == IR_HANDLE) {
            hlsl_handle(e, ir);
            continue;
        }
        if (ir->opcode == IR_IMAGE) {
            hlsl_image(e, ir);
            continue;
        }
        if (ir->opcode == IR_COMPOSITE) {
            hlsl_composite(e, ir);
            continue;
        }
        hlsl_indent(e);
        switch (ir->opcode) {
            case IR_WORKGROUP_BARRIER:
                hlsl_text(e, "GroupMemoryBarrierWithGroupSync();\n");
                break;
            case IR_LOCAL_STORE:
                hlsl_format(e, "l%u = ", ir->operands.scalar.binding);
                hlsl_value(e, ir->operands.scalar.right);
                hlsl_text(e, ";\n");
                break;
            case IR_RETURN:
                hlsl_text(e, "return");
                if (ir->type != IR_VOID) {
                    hlsl_text(e, " ");
                    hlsl_value(e, ir->operands.scalar.left);
                }
                hlsl_text(e, ";\n");
                break;
            case IR_BREAK: case IR_CONTINUE: case IR_BREAK_IF:
                if (loop == NO_ID) {
                    hlsl_error(e, "Hlsl loop control has no enclosing loop");
                    break;
                }
                if (ir->opcode == IR_BREAK_IF) {
                    if (!continuing) {
                        hlsl_error(e, "Hlsl break-if is outside continuing");
                        break;
                    }
                    hlsl_text(e, "if (");
                    hlsl_value(e, ir->operands.scalar.left);
                    hlsl_text(e, ") break;\n");
                } else {
                    if (continuing) {
                        hlsl_error(e, "Hlsl unconditional loop control in continuing is unsupported");
                        break;
                    }
                    if (ir->opcode == IR_BREAK) hlsl_format(e, "break%u = true; ", loop);
                    /* The do-once body funnels every continue through continuing. */
                    hlsl_text(e, "break;\n");
                }
                break;
            case IR_STAGE_INPUT: case IR_ARRAY_LENGTH: case IR_INPUT: case IR_CONSTANT: case IR_ADD: case IR_SUBTRACT: case IR_MULTIPLY:
            case IR_COMPARE: case IR_NOT: case IR_LOCAL_LOAD: case IR_NEGATE: case IR_COMPLEMENT:
            case IR_DIVIDE: case IR_REMAINDER: case IR_BIT_AND: case IR_BIT_OR: case IR_BIT_XOR:
            case IR_SHIFT_LEFT: case IR_SHIFT_RIGHT: case IR_CONVERT: case IR_BITCAST: case IR_CALL:
            case IR_BUILTIN: case IR_COMPOSITE: case IR_SWIZZLE: case IR_SELECT:
                if (ir->result != 0u) {
                    hlsl_value(e, ir->result);
                    hlsl_text(e, " = ");
                } else if (ir->opcode != IR_CALL) {
                    hlsl_error(e, "Hlsl expression is missing a result identity");
                    break;
                }
                hlsl_expression(e, ir);
                hlsl_text(e, ";\n");
                break;
            case IR_DISCARD:
            case IR_ROBUST_LOAD: case IR_UNIFORM_LOAD: case IR_ROBUST_STORE: case IR_MAX:
            case IR_IF: case IR_ELSE: case IR_END_IF: case IR_SHORT_BEGIN: case IR_SHORT_END:
            case IR_LOOP_BEGIN: case IR_LOOP_TEST: case IR_LOOP_CONTINUING: case IR_END_LOOP:
            case IR_ACCESS_LOAD: case IR_ACCESS_STORE: case IR_VALUE_ACCESS:
            case IR_HANDLE: case IR_IMAGE:
            case IR_DEFERRED_OVERRIDE:
            case IR_ADDRESS:
            case IR_ATOMIC_LOAD: case IR_ATOMIC_STORE: case IR_ATOMIC_ADD: case IR_ATOMIC_SUBTRACT:
            case IR_ATOMIC_MAX: case IR_ATOMIC_MIN: case IR_ATOMIC_AND: case IR_ATOMIC_OR:
            case IR_ATOMIC_XOR: case IR_ATOMIC_EXCHANGE: case IR_ATOMIC_COMPARE_EXCHANGE:
            default:
                hlsl_error(e, "Hlsl IR operation has no implemented lowering at this boundary");
                break;
        }
    }
}
