/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"

static bool marker(MetalEmitter *e, uint32_t cursor, IrOpcode opcode) {
    if (cursor < e->function_end && e->module->instructions[cursor].opcode == opcode) return true;
    metal_error(e, "Metal structured IR is missing its required boundary");
    return false;
}
static void selection(MetalEmitter *e, uint32_t *cursor, unsigned depth, uint32_t loop, bool continuing) {
    const IrInstruction *ir = &e->module->instructions[(*cursor)++];
    metal_indent(e);
    metal_text(e, "if (");
    metal_value(e, ir->operands.scalar.left);
    metal_text(e, ") {\n");
    ++e->indent;
    metal_block(e, cursor, depth + 1u, loop, continuing);
    --e->indent;
    if (!marker(e, *cursor, IR_ELSE)) return;
    ++*cursor;
    metal_line(e, "} else {");
    ++e->indent;
    metal_block(e, cursor, depth + 1u, loop, continuing);
    --e->indent;
    if (!marker(e, *cursor, IR_END_IF)) return;
    ++*cursor;
    metal_line(e, "}");
}
static void short_circuit(MetalEmitter *e, uint32_t *cursor, unsigned depth, uint32_t loop, bool continuing) {
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
    metal_indent(e);
    metal_value(e, end->result);
    metal_text(e, " = ");
    metal_value(e, begin->operands.scalar.left);
    metal_text(e, ";\n");
    metal_indent(e);
    metal_text(e, begin->operands.scalar.literal != 0u ? "if (!" : "if (");
    metal_value(e, begin->operands.scalar.left);
    metal_text(e, ") {\n");
    ++e->indent;
    metal_block(e, cursor, depth + 1u, loop, continuing);
    if (*cursor != finish) {
        metal_error(e, "Metal short-circuit boundaries are inconsistent");
        return;
    }
    metal_indent(e);
    metal_value(e, end->result);
    metal_text(e, " = ");
    metal_value(e, end->operands.scalar.right);
    metal_text(e, ";\n");
    --e->indent;
    metal_line(e, "}");
    ++*cursor;
}
static void loop_body(MetalEmitter *e, uint32_t *cursor, unsigned depth) {
    const uint32_t loop = (*cursor)++;
    metal_line(e, "while (true) {");
    ++e->indent;
    metal_block(e, cursor, depth + 1u, loop, false);
    if (!marker(e, *cursor, IR_LOOP_TEST)) return;
    const IrInstruction *test = &e->module->instructions[(*cursor)++];
    if (test->operands.scalar.left != 0u) {
        metal_indent(e);
        metal_text(e, "if (!");
        metal_value(e, test->operands.scalar.left);
        metal_text(e, ") break;\n");
    }
    metal_indent(e);
    metal_format(e, "bool break%u = false;\n", loop);
    metal_line(e, "do {");
    ++e->indent;
    metal_block(e, cursor, depth + 1u, loop, false);
    --e->indent;
    if (!marker(e, *cursor, IR_LOOP_CONTINUING)) return;
    ++*cursor;
    metal_line(e, "} while (false);");
    metal_indent(e);
    metal_format(e, "if (break%u) break;\n", loop);
    metal_block(e, cursor, depth + 1u, loop, true);
    if (!marker(e, *cursor, IR_END_LOOP)) return;
    ++*cursor;
    --e->indent;
    metal_line(e, "}");
}
void metal_block(MetalEmitter *e, uint32_t *cursor, unsigned depth, uint32_t loop, bool continuing) {
    if (depth > DOE_METAL_MAX_DEPTH) {
        metal_error(e, "Metal structured control exceeds target traversal budget");
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
        if (ir->opcode == IR_ACCESS_LOAD || ir->opcode == IR_ACCESS_STORE || ir->opcode == IR_VALUE_ACCESS) {
            metal_access(e, ir);
            continue;
        }
        if (ir->opcode == IR_HANDLE) {
            metal_handle(e, ir);
            continue;
        }
        if (ir->opcode == IR_IMAGE) {
            metal_image(e, ir);
            continue;
        }
        metal_indent(e);
        switch (ir->opcode) {
            case IR_WORKGROUP_BARRIER:
                metal_text(e, "threadgroup_barrier(mem_flags::mem_threadgroup);\n");
                break;
            case IR_LOCAL_STORE:
                metal_format(e, "l%u = ", ir->operands.scalar.binding);
                metal_value(e, ir->operands.scalar.right);
                metal_text(e, ";\n");
                break;
            case IR_RETURN:
                metal_text(e, "return");
                if (ir->type != IR_VOID) {
                    metal_text(e, " ");
                    metal_value(e, ir->operands.scalar.left);
                }
                metal_text(e, ";\n");
                break;
            case IR_BREAK: case IR_CONTINUE: case IR_BREAK_IF:
                if (loop == NO_ID) {
                    metal_error(e, "Metal loop control has no enclosing loop");
                    break;
                }
                if (ir->opcode == IR_BREAK_IF) {
                    if (!continuing) {
                        metal_error(e, "Metal break-if is outside continuing");
                        break;
                    }
                    metal_text(e, "if (");
                    metal_value(e, ir->operands.scalar.left);
                    metal_text(e, ") break;\n");
                } else {
                    if (continuing) {
                        metal_error(e, "Metal unconditional loop control in continuing is unsupported");
                        break;
                    }
                    if (ir->opcode == IR_BREAK) metal_format(e, "break%u = true; ", loop);
                    /* The do-once body funnels every continue through continuing. */
                    metal_text(e, "break;\n");
                }
                break;
            case IR_STAGE_INPUT: case IR_ARRAY_LENGTH: case IR_INPUT: case IR_CONSTANT: case IR_ADD: case IR_SUBTRACT: case IR_MULTIPLY:
            case IR_COMPARE: case IR_NOT: case IR_LOCAL_LOAD: case IR_NEGATE: case IR_COMPLEMENT:
            case IR_DIVIDE: case IR_REMAINDER: case IR_BIT_AND: case IR_BIT_OR: case IR_BIT_XOR:
            case IR_SHIFT_LEFT: case IR_SHIFT_RIGHT: case IR_CONVERT: case IR_BITCAST: case IR_CALL:
            case IR_BUILTIN: case IR_COMPOSITE: case IR_SWIZZLE:
                if (ir->result != 0u) {
                    metal_value(e, ir->result);
                    metal_text(e, " = ");
                } else if (ir->opcode != IR_CALL) {
                    metal_error(e, "Metal expression is missing a result identity");
                    break;
                }
                metal_expression(e, ir);
                metal_text(e, ";\n");
                break;
            case IR_HANDLE: case IR_IMAGE: case IR_ROBUST_LOAD: case IR_UNIFORM_LOAD: case IR_ROBUST_STORE: case IR_MAX:
            case IR_IF: case IR_ELSE: case IR_END_IF: case IR_SHORT_BEGIN: case IR_SHORT_END:
            case IR_LOOP_BEGIN: case IR_LOOP_TEST: case IR_LOOP_CONTINUING: case IR_END_LOOP:
            case IR_ACCESS_LOAD: case IR_ACCESS_STORE: case IR_VALUE_ACCESS:
            default:
                metal_error(e, "Metal IR operation has no implemented lowering at this boundary");
                break;
        }
    }
}
