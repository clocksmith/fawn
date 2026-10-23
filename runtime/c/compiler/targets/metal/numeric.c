/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include "../../src/builtin.h"

void metal_numeric_helpers(MetalEmitter *e) {
    metal_text(e,
        "inline uint doe_div(uint a, uint b) { return a / (b == 0u ? 1u : b); }\n"
        "inline uint doe_rem(uint a, uint b) { return b == 0u ? 0u : a % b; }\n"
        "inline int doe_div(int a, int b) {\n"
        "    bool bad = b == 0 || (a == as_type<int>(0x80000000u) && b == -1);\n"
        "    return a / (bad ? 1 : b);\n"
        "}\n"
        "inline int doe_rem(int a, int b) {\n"
        "    bool bad = b == 0 || (a == as_type<int>(0x80000000u) && b == -1);\n"
        "    return bad ? 0 : a % (bad ? 1 : b);\n"
        "}\n"
        "inline int doe_i32(float a) {\n"
        "    float finite = isnan(a) ? 0.0f : a;\n"
        "    return int(clamp(finite, -2147483648.0f, 2147483520.0f));\n"
        "}\n"
        "inline uint doe_u32(float a) {\n"
        "    float finite = isnan(a) ? 0.0f : a;\n"
        "    return uint(clamp(finite, 0.0f, 4294967040.0f));\n"
        "}\n");
}
static void component(MetalEmitter *e, uint32_t value, uint32_t lane) {
    const TypeId type = metal_value_type(e, value);
    metal_value(e, value);
    if (type_vector_width(e->module, type) != 1u) metal_format(e, "[%u]", lane);
}
static void scalar_conversion(MetalEmitter *e, TypeId from, TypeId to, uint32_t value, uint32_t lane) {
    if (from == IR_F32 && (to == IR_I32 || to == IR_U32)) {
        metal_text(e, to == IR_I32 ? "doe_i32(" : "doe_u32(");
    } else if ((from == IR_I32 && to == IR_U32) || (from == IR_U32 && to == IR_I32)) {
        metal_text(e, to == IR_I32 ? "as_type<int>(" : "as_type<uint>(");
    } else {
        metal_type(e, to);
        metal_text(e, "(");
    }
    component(e, value, lane);
    metal_text(e, ")");
}
static const char *binary_operator(IrOpcode opcode) {
    static const char *const operators[] = {
        [IR_ADD] = "+", [IR_SUBTRACT] = "-", [IR_MULTIPLY] = "*",
        [IR_DIVIDE] = "/", [IR_REMAINDER] = "%", [IR_BIT_AND] = "&",
        [IR_BIT_OR] = "|", [IR_BIT_XOR] = "^", [IR_SHIFT_LEFT] = "<<",
        [IR_SHIFT_RIGHT] = ">>"
    };
    const unsigned index = (unsigned)opcode;
    return index < sizeof(operators) / sizeof(operators[0]) ? operators[index] : NULL;
}
static void scalar_operation(MetalEmitter *e, const IrInstruction *ir, uint32_t lane) {
    const TypeId leaf = type_leaf(e->module, ir->type);
    const uint32_t left = ir->operands.scalar.left;
    const uint32_t right = ir->operands.scalar.right;
    if (ir->opcode == IR_CONVERT) {
        scalar_conversion(e, type_leaf(e->module, ir->operands.scalar.binding), leaf, left, lane);
        return;
    }
    if (ir->opcode == IR_DIVIDE || ir->opcode == IR_REMAINDER) {
        if (leaf != IR_F32) {
            metal_text(e, ir->opcode == IR_DIVIDE ? "doe_div(" : "doe_rem(");
        } else if (ir->opcode == IR_REMAINDER) metal_text(e, "fmod(");
        else metal_text(e, "(");
        component(e, left, lane);
        metal_text(e, leaf == IR_F32 && ir->opcode == IR_DIVIDE ? " / " : ", ");
        component(e, right, lane);
        metal_text(e, ")");
        return;
    }
    const bool wrap = leaf == IR_I32 && (ir->opcode == IR_ADD || ir->opcode == IR_SUBTRACT ||
        ir->opcode == IR_MULTIPLY || ir->opcode == IR_NEGATE || ir->opcode == IR_SHIFT_LEFT);
    if (wrap) metal_text(e, "as_type<int>(");
    metal_text(e, "(");
    if (ir->opcode == IR_NEGATE) metal_text(e, wrap ? "0u - " : "-");
    if (ir->opcode == IR_NOT) metal_text(e, "!");
    if (ir->opcode == IR_COMPLEMENT) metal_text(e, "~");
    if (wrap) metal_text(e, "as_type<uint>(");
    component(e, left, lane);
    if (wrap) metal_text(e, ")");
    const char *operation = binary_operator(ir->opcode);
    if (operation != NULL) {
        metal_format(e, " %s ", operation);
        if (ir->opcode == IR_SHIFT_LEFT || ir->opcode == IR_SHIFT_RIGHT) metal_text(e, "(");
        if (wrap && ir->opcode != IR_SHIFT_LEFT) metal_text(e, "as_type<uint>(");
        component(e, right, lane);
        if (wrap && ir->opcode != IR_SHIFT_LEFT) metal_text(e, ")");
        if (ir->opcode == IR_SHIFT_LEFT || ir->opcode == IR_SHIFT_RIGHT) metal_text(e, " & 31u)");
    }
    metal_text(e, ")");
    if (wrap) metal_text(e, ")");
}
static void numeric_operation(MetalEmitter *e, const IrInstruction *ir) {
    const uint32_t width = type_vector_width(e->module, ir->type);
    if (width != 1u) {
        metal_type(e, ir->type);
        metal_text(e, "(");
    }
    for (uint32_t lane = 0u; lane < width; ++lane) {
        if (lane != 0u) metal_text(e, ", ");
        scalar_operation(e, ir, lane);
    }
    if (width != 1u) metal_text(e, ")");
}
static void builtin_expression(MetalEmitter *e, const IrInstruction *ir) {
    const NumericBuiltin *builtin = builtin_kind((NumericBuiltinKind)ir->operands.scalar.literal);
    if (builtin == NULL) {
        metal_error(e, "Metal builtin identity has no registered semantics");
        return;
    }
    /* A source builtin registry is not a target support table. New shared
     * operations must never become unchecked MSL names by default. */
    const NumericBuiltinKind kind = builtin->kind;
    const bool supported = kind == BUILTIN_CEIL || kind == BUILTIN_MIN || kind == BUILTIN_MAX || kind == BUILTIN_CLAMP ||
        kind == BUILTIN_EXP || kind == BUILTIN_TANH || kind == BUILTIN_SQRT ||
        kind == BUILTIN_DOT || kind == BUILTIN_LENGTH || kind == BUILTIN_DISTANCE ||
        kind == BUILTIN_NORMALIZE || kind == BUILTIN_CROSS;
    if (!supported) {
        metal_error(e, "Metal builtin has no implemented lowering");
        return;
    }
    const TypeId operand_type = metal_value_type(e, ir->operands.scalar.left);
    const TypeId leaf = type_leaf(e->module, operand_type);
    if (builtin->kind == BUILTIN_DOT && leaf != IR_F32) {
        const uint32_t width = type_vector_width(e->module, operand_type);
        if (leaf == IR_I32) metal_text(e, "as_type<int>(");
        metal_text(e, "(");
        for (uint32_t lane = 0u; lane < width; ++lane) {
            if (lane != 0u) metal_text(e, " + ");
            metal_text(e, "uint(");
            component(e, ir->operands.scalar.left, lane);
            metal_text(e, ") * uint(");
            component(e, ir->operands.scalar.right, lane);
            metal_text(e, ")");
        }
        metal_text(e, ")");
        if (leaf == IR_I32) metal_text(e, ")");
        return;
    }
    if (builtin->kind == BUILTIN_MIN || builtin->kind == BUILTIN_MAX || builtin->kind == BUILTIN_CLAMP) {
        const uint32_t width = type_vector_width(e->module, ir->type);
        if (width != 1u) {
            metal_type(e, ir->type);
            metal_text(e, "(");
        }
        for (uint32_t lane = 0u; lane < width; ++lane) {
            if (lane != 0u) metal_text(e, ", ");
            /* Ordered comparisons match retained min/max NaN and signed-zero
             * selection; native fmin/fmax may select a different operand. */
            if (builtin->kind == BUILTIN_CLAMP) metal_text(e, "(");
            metal_text(e, "(");
            component(e, ir->operands.scalar.left, lane);
            metal_text(e, builtin->kind == BUILTIN_MIN ? " < " : " > ");
            component(e, ir->operands.scalar.right, lane);
            metal_text(e, " ? ");
            component(e, ir->operands.scalar.left, lane);
            metal_text(e, " : ");
            component(e, ir->operands.scalar.right, lane);
            metal_text(e, ")");
            if (builtin->kind == BUILTIN_CLAMP) {
                metal_text(e, " < ");
                component(e, ir->operands.scalar.binding, lane);
                metal_text(e, " ? (");
                component(e, ir->operands.scalar.left, lane);
                metal_text(e, " > ");
                component(e, ir->operands.scalar.right, lane);
                metal_text(e, " ? ");
                component(e, ir->operands.scalar.left, lane);
                metal_text(e, " : ");
                component(e, ir->operands.scalar.right, lane);
                metal_text(e, ") : ");
                component(e, ir->operands.scalar.binding, lane);
                metal_text(e, ")");
            }
        }
        if (width != 1u) metal_text(e, ")");
        return;
    }
    metal_format(e, "%s(", builtin->name);
    metal_value(e, ir->operands.scalar.left);
    if (builtin->arity > 1u) {
        metal_text(e, ", ");
        metal_value(e, ir->operands.scalar.right);
    }
    metal_text(e, ")");
}
void metal_expression(MetalEmitter *e, const IrInstruction *ir) {
    switch (ir->opcode) {
        case IR_CONSTANT:
            metal_constant(e, ir->type,
                ir->operands.scalar.left != 0u ? ir->operands.scalar.binding : ir->operands.scalar.literal,
                ir->operands.scalar.left != 0u, 0u);
            break;
        case IR_ADD: case IR_SUBTRACT: case IR_MULTIPLY: case IR_DIVIDE: case IR_REMAINDER:
        case IR_BIT_AND: case IR_BIT_OR: case IR_BIT_XOR: case IR_SHIFT_LEFT: case IR_SHIFT_RIGHT:
        case IR_NEGATE: case IR_COMPLEMENT: case IR_NOT: case IR_CONVERT:
            numeric_operation(e, ir);
            break;
        case IR_BITCAST:
            metal_text(e, "as_type<");
            metal_type(e, ir->type);
            metal_text(e, ">(");
            metal_value(e, ir->operands.scalar.left);
            metal_text(e, ")");
            break;
        case IR_BUILTIN: builtin_expression(e, ir); break;
        case IR_COMPARE: {
            static const char *const operators[] = {"==", "!=", "<", "<=", ">", ">="};
            if (ir->operands.scalar.literal > COMPARE_GREATER_EQUAL) {
                metal_error(e, "Metal comparison identity is invalid");
                break;
            }
            metal_text(e, "(");
            metal_value(e, ir->operands.scalar.left);
            metal_format(e, " %s ", operators[ir->operands.scalar.literal]);
            metal_value(e, ir->operands.scalar.right);
            metal_text(e, ")");
            break;
        }
        case IR_LOCAL_LOAD: metal_format(e, "l%u", ir->operands.scalar.binding); break;
        case IR_STAGE_INPUT: metal_stage_input(e, ir); break;
        case IR_ARRAY_LENGTH: metal_array_length(e, ir); break;
        case IR_INPUT:
            if (ir->operands.scalar.binding >= e->module->entry_input_count) {
                metal_error(e, "Metal builtin input identity is invalid");
                break;
            }
            if ((unsigned)e->module->entry_inputs[ir->operands.scalar.binding].kind > INPUT_LOCAL_INDEX) {
                metal_error(e, "Metal builtin input has no implemented lowering");
                break;
            }
            metal_format(e, "ctx.input%u", (unsigned)e->module->entry_inputs[ir->operands.scalar.binding].kind);
            if (ir->operands.scalar.literal != NO_ID &&
                e->module->entry_inputs[ir->operands.scalar.binding].kind != INPUT_LOCAL_INDEX)
                metal_format(e, "[%u]", ir->operands.scalar.literal);
            break;
        case IR_SWIZZLE:
            metal_value(e, ir->operands.scalar.left);
            metal_text(e, ".");
            for (uint32_t i = 0u; i < ir->operands.scalar.right; ++i) {
                const char component_name[] = {"xyzw"[(ir->operands.scalar.literal >> (2u * i)) & 3u], '\0'};
                metal_text(e, component_name);
            }
            break;
        case IR_COMPOSITE: {
            const uint32_t first = ir->operands.composite.first, count = ir->operands.composite.count;
            if (first > e->module->operand_count || count > e->module->operand_count - first) {
                metal_error(e, "Metal composite operand range is invalid");
                break;
            }
            metal_type(e, ir->type);
            const bool vector = type_vector_width(e->module, ir->type) != 1u;
            metal_text(e, vector ? "(" : "{");
            for (uint32_t i = 0u; i < count; ++i) {
                if (i != 0u) metal_text(e, ", ");
                metal_value(e, e->module->typed_operands[first + i]);
            }
            metal_text(e, vector ? ")" : "}");
            break;
        }
        case IR_CALL: {
            const uint32_t first = ir->operands.scalar.left, count = ir->operands.scalar.right;
            if (first > e->module->operand_count || count > e->module->operand_count - first ||
                ir->operands.scalar.binding >= e->module->function_count) {
                metal_error(e, "Metal call target or argument range is invalid");
                break;
            }
            metal_format(e, "doe_fn%u(ctx", ir->operands.scalar.binding);
            metal_handle_arguments(e, ir->operands.scalar.binding, false);
            for (uint32_t i = 0u; i < count; ++i) {
                metal_text(e, ", ");
                metal_value(e, e->module->typed_operands[first + i]);
            }
            metal_text(e, ")");
            break;
        }
        case IR_HANDLE: case IR_IMAGE:
        case IR_ROBUST_LOAD: case IR_UNIFORM_LOAD: case IR_MAX: case IR_ROBUST_STORE:
        case IR_DISCARD:
        case IR_IF: case IR_ELSE: case IR_END_IF: case IR_RETURN: case IR_SHORT_BEGIN:
        case IR_SHORT_END: case IR_LOCAL_STORE: case IR_LOOP_BEGIN: case IR_LOOP_TEST:
        case IR_LOOP_CONTINUING: case IR_END_LOOP: case IR_BREAK: case IR_CONTINUE:
        case IR_BREAK_IF: case IR_ACCESS_LOAD: case IR_ACCESS_STORE: case IR_VALUE_ACCESS:
        case IR_WORKGROUP_BARRIER:
        default: metal_error(e, "Metal IR expression has no implemented lowering"); break;
    }
}
