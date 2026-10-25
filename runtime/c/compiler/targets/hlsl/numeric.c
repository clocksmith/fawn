/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include "../../src/builtin.h"

void hlsl_numeric_helpers(HlslEmitter *e) {
    hlsl_text(e,
        "inline uint doe_div(uint a, uint b) { return a / (b == 0u ? 1u : b); }\n"
        "inline uint doe_rem(uint a, uint b) { return b == 0u ? 0u : a % (b == 0u ? 1u : b); }\n"
        "inline int doe_div(int a, int b) {\n"
        "    bool bad = b == 0 || (a == asint(0x80000000u) && b == -1);\n"
        "    return a / (bad ? 1 : b);\n"
        "}\n"
        "inline int doe_rem(int a, int b) {\n"
        "    bool bad = b == 0 || (a == asint(0x80000000u) && b == -1);\n"
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
static void component(HlslEmitter *e, uint32_t value, uint32_t lane) {
    const TypeId type = hlsl_value_type(e, value);
    hlsl_value(e, value);
    if (type_vector_width(e->module, type) != 1u) hlsl_format(e, "[%u]", lane);
}
static void scalar_conversion(HlslEmitter *e, TypeId from, TypeId to, uint32_t value, uint32_t lane) {
    if ((from == IR_F32 || from == IR_F16) && (to == IR_I32 || to == IR_U32)) {
        hlsl_text(e, to == IR_I32 ? "doe_i32(" : "doe_u32(");
    } else if ((from == IR_I32 && to == IR_U32) || (from == IR_U32 && to == IR_I32)) {
        hlsl_text(e, to == IR_I32 ? "asint(" : "asuint(");
    } else {
        hlsl_type(e, to);
        hlsl_text(e, "(");
    }
    component(e, value, lane);
    hlsl_text(e, ")");
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
static void scalar_operation(HlslEmitter *e, const IrInstruction *ir, uint32_t lane) {
    const TypeId leaf = type_leaf(e->module, ir->type);
    const uint32_t left = ir->operands.scalar.left;
    const uint32_t right = ir->operands.scalar.right;
    if (ir->opcode == IR_CONVERT) {
        scalar_conversion(e, type_leaf(e->module, ir->operands.scalar.binding), leaf, left, lane);
        return;
    }
    if (ir->opcode == IR_DIVIDE || ir->opcode == IR_REMAINDER) {
        if ((leaf != IR_F32 && leaf != IR_F16)) {
            hlsl_text(e, ir->opcode == IR_DIVIDE ? "doe_div(" : "doe_rem(");
        } else if (ir->opcode == IR_REMAINDER) hlsl_text(e, "fmod(");
        else hlsl_text(e, "(");
        component(e, left, lane);
        hlsl_text(e, (leaf == IR_F32 || leaf == IR_F16) && ir->opcode == IR_DIVIDE ? " / " : ", ");
        component(e, right, lane);
        hlsl_text(e, ")");
        return;
    }
    const bool wrap = leaf == IR_I32 && (ir->opcode == IR_ADD || ir->opcode == IR_SUBTRACT ||
        ir->opcode == IR_MULTIPLY || ir->opcode == IR_NEGATE || ir->opcode == IR_SHIFT_LEFT);
    if (wrap) hlsl_text(e, "asint(");
    hlsl_text(e, "(");
    if (ir->opcode == IR_NEGATE) hlsl_text(e, wrap ? "0u - " : "-");
    if (ir->opcode == IR_NOT) hlsl_text(e, "!");
    if (ir->opcode == IR_COMPLEMENT) hlsl_text(e, "~");
    if (wrap) hlsl_text(e, "asuint(");
    component(e, left, lane);
    if (wrap) hlsl_text(e, ")");
    const char *operation = binary_operator(ir->opcode);
    if (operation != NULL) {
        hlsl_format(e, " %s ", operation);
        if (ir->opcode == IR_SHIFT_LEFT || ir->opcode == IR_SHIFT_RIGHT) hlsl_text(e, "(");
        if (wrap && ir->opcode != IR_SHIFT_LEFT) hlsl_text(e, "asuint(");
        component(e, right, lane);
        if (wrap && ir->opcode != IR_SHIFT_LEFT) hlsl_text(e, ")");
        if (ir->opcode == IR_SHIFT_LEFT || ir->opcode == IR_SHIFT_RIGHT) hlsl_text(e, " & 31u)");
    }
    hlsl_text(e, ")");
    if (wrap) hlsl_text(e, ")");
}
static void numeric_operation(HlslEmitter *e, const IrInstruction *ir) {
    if ((ir->opcode == IR_ADD || ir->opcode == IR_SUBTRACT || ir->opcode == IR_MULTIPLY) &&
        (type_matrix(e->module, ir->operands.scalar.binding) ||
         type_matrix(e->module, ir->operands.scalar.literal))) {
        const bool product = ir->opcode == IR_MULTIPLY &&
            ir->operands.scalar.binding != IR_F32 && ir->operands.scalar.literal != IR_F32;
        /* HLSL rows represent WGSL columns, so (A B)^T = B^T A^T.
         * HLSL '*' is componentwise; only scalar products may use it here. */
        hlsl_text(e, product ? "mul(" : "(");
        hlsl_value(e, product ? ir->operands.scalar.right : ir->operands.scalar.left);
        if (product) hlsl_text(e, ", ");
        else hlsl_format(e, " %s ", binary_operator(ir->opcode));
        hlsl_value(e, product ? ir->operands.scalar.left : ir->operands.scalar.right);
        hlsl_text(e, ")");
        return;
    }
    const uint32_t width = type_vector_width(e->module, ir->type);
    if (width != 1u) {
        hlsl_type(e, ir->type);
        hlsl_text(e, "(");
    }
    for (uint32_t lane = 0u; lane < width; ++lane) {
        if (lane != 0u) hlsl_text(e, ", ");
        scalar_operation(e, ir, lane);
    }
    if (width != 1u) hlsl_text(e, ")");
}
static void subgroup_expression(HlslEmitter *e, const IrInstruction *ir, NumericBuiltinKind kind) {
    const char *intrinsic = NULL;
    bool inclusive = false;
    bool product = false;
    switch (kind) {
        case BUILTIN_SUBGROUP_ADD: intrinsic = "WaveActiveSum"; break;
        case BUILTIN_SUBGROUP_MIN: intrinsic = "WaveActiveMin"; break;
        case BUILTIN_SUBGROUP_MAX: intrinsic = "WaveActiveMax"; break;
        case BUILTIN_SUBGROUP_MUL: intrinsic = "WaveActiveProduct"; break;
        case BUILTIN_SUBGROUP_AND: intrinsic = "WaveActiveBitAnd"; break;
        case BUILTIN_SUBGROUP_OR: intrinsic = "WaveActiveBitOr"; break;
        case BUILTIN_SUBGROUP_XOR: intrinsic = "WaveActiveBitXor"; break;
        case BUILTIN_SUBGROUP_EXCLUSIVE_ADD: intrinsic = "WavePrefixSum"; break;
        case BUILTIN_SUBGROUP_INCLUSIVE_ADD: intrinsic = "WavePrefixSum"; inclusive = true; break;
        case BUILTIN_SUBGROUP_EXCLUSIVE_MUL: intrinsic = "WavePrefixProduct"; break;
        case BUILTIN_SUBGROUP_INCLUSIVE_MUL:
            intrinsic = "WavePrefixProduct";
            inclusive = true;
            product = true;
            break;
        case BUILTIN_FMA: case BUILTIN_STEP: case BUILTIN_SMOOTHSTEP: case BUILTIN_SATURATE:
        case BUILTIN_MIX: case BUILTIN_COUNT_ONE_BITS:
        case BUILTIN_REVERSE_BITS: case BUILTIN_COUNT_LEADING_ZEROS: case BUILTIN_COUNT_TRAILING_ZEROS:
        case BUILTIN_FIRST_LEADING_BIT: case BUILTIN_FIRST_TRAILING_BIT: case BUILTIN_UNPACK4X_U8:
        case BUILTIN_EXTRACT_BITS: case BUILTIN_INSERT_BITS:
        case BUILTIN_PACK4X_I8: case BUILTIN_PACK4X_U8: case BUILTIN_PACK4X_I8_CLAMP:
        case BUILTIN_PACK4X_U8_CLAMP: case BUILTIN_UNPACK4X_I8:
        case BUILTIN_DOT4_I8_PACKED: case BUILTIN_DOT4_U8_PACKED:
        case BUILTIN_ABS: case BUILTIN_SIGN: case BUILTIN_FLOOR: case BUILTIN_ROUND:
        case BUILTIN_TRUNC: case BUILTIN_FRACT: case BUILTIN_SIN: case BUILTIN_COS:
        case BUILTIN_TAN: case BUILTIN_ASIN: case BUILTIN_ACOS: case BUILTIN_ATAN:
        case BUILTIN_ATAN2: case BUILTIN_POW:
        case BUILTIN_SINH: case BUILTIN_COSH: case BUILTIN_ASINH: case BUILTIN_ACOSH:
        case BUILTIN_ATANH: case BUILTIN_LOG: case BUILTIN_LOG2: case BUILTIN_EXP2:
        case BUILTIN_INVERSE_SQRT: case BUILTIN_DEGREES: case BUILTIN_RADIANS:
        case BUILTIN_CEIL: case BUILTIN_TRANSPOSE: case BUILTIN_DETERMINANT:
        case BUILTIN_MIN: case BUILTIN_MAX: case BUILTIN_CLAMP: case BUILTIN_EXP:
        case BUILTIN_TANH: case BUILTIN_SQRT: case BUILTIN_DOT: case BUILTIN_LENGTH:
        case BUILTIN_DISTANCE: case BUILTIN_NORMALIZE: case BUILTIN_CROSS: case BUILTIN_REFLECT:
        case BUILTIN_FACE_FORWARD: case BUILTIN_REFRACT: case BUILTIN_ALL: case BUILTIN_ANY:
        default: hlsl_error(e, "HLSL subgroup builtin has no wave mapping"); return;
    }
    e->uses_subgroups = true;
    const bool wrap = type_leaf(e->module, ir->type) == IR_I32 &&
        kind != BUILTIN_SUBGROUP_MIN && kind != BUILTIN_SUBGROUP_MAX;
    if (wrap) hlsl_text(e, "asint(");
    if (inclusive) hlsl_text(e, "(");
    hlsl_format(e, "%s(", intrinsic);
    if (wrap) hlsl_text(e, "asuint(");
    hlsl_value(e, ir->operands.scalar.left);
    if (wrap) hlsl_text(e, ")");
    hlsl_text(e, ")");
    if (inclusive) {
        hlsl_text(e, product ? " * " : " + ");
        if (wrap) hlsl_text(e, "asuint(");
        hlsl_value(e, ir->operands.scalar.left);
        if (wrap) hlsl_text(e, ")");
        hlsl_text(e, ")");
    }
    if (wrap) hlsl_text(e, ")");
}
/* Repeated operands below are immutable IR snapshots, not replayed source
 * expressions. Component lowering avoids signed overflow and target fract clamps. */
static void unary_component(HlslEmitter *e, const IrInstruction *ir, NumericBuiltinKind kind) {
    const TypeId leaf = type_leaf(e->module, ir->type);
    const uint32_t width = type_vector_width(e->module, ir->type);
    if (width != 1u) {
        hlsl_type(e, ir->type);
        hlsl_text(e, "(");
    }
    for (uint32_t lane = 0u; lane < width; ++lane) {
        if (lane != 0u) hlsl_text(e, ", ");
        if (kind == BUILTIN_FRACT) {
            hlsl_text(e, "(");
            component(e, ir->operands.scalar.left, lane);
            hlsl_text(e, " - floor(");
            component(e, ir->operands.scalar.left, lane);
            hlsl_text(e, "))");
        } else if (kind == BUILTIN_SIGN) {
            hlsl_text(e, "(");
            component(e, ir->operands.scalar.left, lane);
            hlsl_text(e, (leaf == IR_F32 || leaf == IR_F16) ? " > 0.0f ? 1.0f : (" : " > 0 ? 1 : (");
            component(e, ir->operands.scalar.left, lane);
            hlsl_text(e, (leaf == IR_F32 || leaf == IR_F16) ? " < 0.0f ? -1.0f : 0.0f))" : " < 0 ? -1 : 0))");
        } else if (leaf == IR_I32) {
            hlsl_text(e, "(");
            component(e, ir->operands.scalar.left, lane);
            hlsl_text(e, " < 0 ? asint(0u - asuint(");
            component(e, ir->operands.scalar.left, lane);
            hlsl_text(e, ")) : ");
            component(e, ir->operands.scalar.left, lane);
            hlsl_text(e, ")");
        } else {
            if ((leaf == IR_F32 || leaf == IR_F16)) hlsl_text(e, "abs(");
            component(e, ir->operands.scalar.left, lane);
            if ((leaf == IR_F32 || leaf == IR_F16)) hlsl_text(e, ")");
        }
    }
    if (width != 1u) hlsl_text(e, ")");
}
static void integer_builtin(HlslEmitter *e, const IrInstruction *ir, NumericBuiltinKind kind) {
    if (kind == BUILTIN_REVERSE_BITS && type_leaf(e->module, ir->type) == IR_I32) hlsl_text(e, "asint");
    else hlsl_type(e, ir->type);
    hlsl_text(e, "(");
    if (kind == BUILTIN_COUNT_LEADING_ZEROS) hlsl_text(e, "31u - ");
    if (kind == BUILTIN_COUNT_TRAILING_ZEROS) hlsl_text(e, "min(");
    const char *name = kind == BUILTIN_COUNT_ONE_BITS ? "countbits" : kind == BUILTIN_REVERSE_BITS ? "reversebits" :
        kind == BUILTIN_FIRST_TRAILING_BIT || kind == BUILTIN_COUNT_TRAILING_ZEROS ? "firstbitlow" : "firstbithigh";
    hlsl_format(e, "%s(", name);
    const bool unsigned_input = kind == BUILTIN_COUNT_LEADING_ZEROS || kind == BUILTIN_COUNT_TRAILING_ZEROS ||
        kind == BUILTIN_REVERSE_BITS;
    if (unsigned_input) hlsl_text(e, "asuint(");
    hlsl_value(e, ir->operands.scalar.left);
    if (unsigned_input) hlsl_text(e, ")");
    hlsl_text(e, ")");
    if (kind == BUILTIN_COUNT_TRAILING_ZEROS) hlsl_text(e, ", 32u)");
    hlsl_text(e, ")");
}
static void builtin_expression(HlslEmitter *e, const IrInstruction *ir) {
    const NumericBuiltin *builtin = builtin_kind((NumericBuiltinKind)ir->operands.scalar.literal);
    if (builtin == NULL) {
        hlsl_error(e, "Hlsl builtin identity has no registered semantics");
        return;
    }
    if (builtin->family == BUILTIN_COMPONENT_INTEGER) {
        integer_builtin(e, ir, builtin->kind);
        return;
    }
    if (builtin->family == BUILTIN_BOOLEAN_REDUCTION) {
        const TypeId operand = hlsl_value_type(e, ir->operands.scalar.left);
        if (type_leaf(e->module, operand) != IR_BOOL || ir->type != IR_BOOL ||
            (builtin->kind != BUILTIN_ALL && builtin->kind != BUILTIN_ANY)) {
            hlsl_error(e, "boolean reduction requires boolean operands and scalar result");
            return;
        }
        if (operand != IR_BOOL) hlsl_text(e, builtin->kind == BUILTIN_ALL ? "all(" : "any(");
        hlsl_value(e, ir->operands.scalar.left);
        if (operand != IR_BOOL) hlsl_text(e, ")");
        return;
    }
    if (builtin_subgroup(builtin)) {
        subgroup_expression(e, ir, builtin->kind);
        return;
    }
    if (builtin->kind == BUILTIN_ABS || builtin->kind == BUILTIN_SIGN || builtin->kind == BUILTIN_FRACT) {
        unary_component(e, ir, builtin->kind);
        return;
    }
    const TypeId operand_type = hlsl_value_type(e, ir->operands.scalar.left);
    const TypeId leaf = type_leaf(e->module, operand_type);
    if (builtin->kind == BUILTIN_DOT && (leaf != IR_F32 && leaf != IR_F16)) {
        const uint32_t width = type_vector_width(e->module, operand_type);
        if (leaf == IR_I32) hlsl_text(e, "asint(");
        hlsl_text(e, "(");
        for (uint32_t lane = 0u; lane < width; ++lane) {
            if (lane != 0u) hlsl_text(e, " + ");
            hlsl_text(e, "uint(");
            component(e, ir->operands.scalar.left, lane);
            hlsl_text(e, ") * uint(");
            component(e, ir->operands.scalar.right, lane);
            hlsl_text(e, ")");
        }
        hlsl_text(e, ")");
        if (leaf == IR_I32) hlsl_text(e, ")");
        return;
    }
    if (builtin->kind == BUILTIN_MIN || builtin->kind == BUILTIN_MAX || builtin->kind == BUILTIN_CLAMP) {
        const uint32_t width = type_vector_width(e->module, ir->type);
        if (width != 1u) {
            hlsl_type(e, ir->type);
            hlsl_text(e, "(");
        }
        for (uint32_t lane = 0u; lane < width; ++lane) {
            if (lane != 0u) hlsl_text(e, ", ");
            /* Ordered comparisons match retained min/max NaN and signed-zero
             * selection; native fmin/fmax may select a different operand. */
            if (builtin->kind == BUILTIN_CLAMP) hlsl_text(e, "(");
            hlsl_text(e, "(");
            component(e, ir->operands.scalar.left, lane);
            hlsl_text(e, builtin->kind == BUILTIN_MIN ? " < " : " > ");
            component(e, ir->operands.scalar.right, lane);
            hlsl_text(e, " ? ");
            component(e, ir->operands.scalar.left, lane);
            hlsl_text(e, " : ");
            component(e, ir->operands.scalar.right, lane);
            hlsl_text(e, ")");
            if (builtin->kind == BUILTIN_CLAMP) {
                hlsl_text(e, " < ");
                component(e, ir->operands.scalar.binding, lane);
                hlsl_text(e, " ? (");
                component(e, ir->operands.scalar.left, lane);
                hlsl_text(e, " > ");
                component(e, ir->operands.scalar.right, lane);
                hlsl_text(e, " ? ");
                component(e, ir->operands.scalar.left, lane);
                hlsl_text(e, " : ");
                component(e, ir->operands.scalar.right, lane);
                hlsl_text(e, ") : ");
                component(e, ir->operands.scalar.binding, lane);
                hlsl_text(e, ")");
            }
        }
        if (width != 1u) hlsl_text(e, ")");
        return;
    }
    const char *intrinsic = NULL;
    switch (builtin->kind) {
        case BUILTIN_FLOOR: intrinsic = "floor"; break;
        case BUILTIN_ROUND: intrinsic = "round"; break;
        case BUILTIN_TRUNC: intrinsic = "trunc"; break;
        case BUILTIN_SIN: intrinsic = "sin"; break;
        case BUILTIN_COS: intrinsic = "cos"; break;
        case BUILTIN_TAN: intrinsic = "tan"; break;
        case BUILTIN_ASIN: intrinsic = "asin"; break;
        case BUILTIN_ACOS: intrinsic = "acos"; break;
        case BUILTIN_ATAN: intrinsic = "atan"; break;
        case BUILTIN_ATAN2: intrinsic = "atan2"; break;
        case BUILTIN_POW: intrinsic = "pow"; break;
        case BUILTIN_SINH: intrinsic = "sinh"; break;
        case BUILTIN_COSH: intrinsic = "cosh"; break;
        case BUILTIN_LOG: intrinsic = "log"; break;
        case BUILTIN_LOG2: intrinsic = "log2"; break;
        case BUILTIN_EXP2: intrinsic = "exp2"; break;
        case BUILTIN_INVERSE_SQRT: intrinsic = "rsqrt"; break;
        case BUILTIN_DEGREES: intrinsic = "degrees"; break;
        case BUILTIN_RADIANS: intrinsic = "radians"; break;
        case BUILTIN_CEIL: intrinsic = "ceil"; break;
        case BUILTIN_EXP: intrinsic = "exp"; break;
        case BUILTIN_TANH: intrinsic = "tanh"; break;
        case BUILTIN_SQRT: intrinsic = "sqrt"; break;
        case BUILTIN_DOT: intrinsic = "dot"; break;
        case BUILTIN_LENGTH: intrinsic = "length"; break;
        case BUILTIN_DISTANCE: intrinsic = "distance"; break;
        case BUILTIN_NORMALIZE: intrinsic = "normalize"; break;
        case BUILTIN_CROSS: intrinsic = "cross"; break;
        case BUILTIN_MIX: intrinsic = "lerp"; break;
        case BUILTIN_FMA: intrinsic = "mad"; break;
        case BUILTIN_STEP: intrinsic = "step"; break;
        case BUILTIN_REFLECT: intrinsic = "reflect"; break;
        case BUILTIN_FACE_FORWARD: intrinsic = "faceforward"; break;
        case BUILTIN_REFRACT: intrinsic = "refract"; break;
        case BUILTIN_TRANSPOSE: intrinsic = "transpose"; break;
        case BUILTIN_DETERMINANT: intrinsic = "determinant"; break;
        case BUILTIN_SMOOTHSTEP: case BUILTIN_SATURATE:
        case BUILTIN_COUNT_ONE_BITS:
        case BUILTIN_REVERSE_BITS: case BUILTIN_COUNT_LEADING_ZEROS: case BUILTIN_COUNT_TRAILING_ZEROS:
        case BUILTIN_FIRST_LEADING_BIT: case BUILTIN_FIRST_TRAILING_BIT: case BUILTIN_UNPACK4X_U8:
        case BUILTIN_EXTRACT_BITS: case BUILTIN_INSERT_BITS:
        case BUILTIN_PACK4X_I8: case BUILTIN_PACK4X_U8: case BUILTIN_PACK4X_I8_CLAMP:
        case BUILTIN_PACK4X_U8_CLAMP: case BUILTIN_UNPACK4X_I8:
        case BUILTIN_DOT4_I8_PACKED: case BUILTIN_DOT4_U8_PACKED:
        case BUILTIN_ABS: case BUILTIN_SIGN: case BUILTIN_FRACT: case BUILTIN_ALL: case BUILTIN_ANY:
        case BUILTIN_ASINH: case BUILTIN_ACOSH: case BUILTIN_ATANH:
        case BUILTIN_MIN: case BUILTIN_MAX: case BUILTIN_CLAMP:
        case BUILTIN_SUBGROUP_ADD: case BUILTIN_SUBGROUP_MIN: case BUILTIN_SUBGROUP_MAX:
        case BUILTIN_SUBGROUP_MUL: case BUILTIN_SUBGROUP_AND: case BUILTIN_SUBGROUP_OR:
        case BUILTIN_SUBGROUP_XOR: case BUILTIN_SUBGROUP_EXCLUSIVE_ADD:
        case BUILTIN_SUBGROUP_INCLUSIVE_ADD: case BUILTIN_SUBGROUP_EXCLUSIVE_MUL:
        case BUILTIN_SUBGROUP_INCLUSIVE_MUL:
        default: hlsl_error(e, "HLSL builtin has no explicitly mapped intrinsic"); return;
    }
    hlsl_format(e, "%s(", intrinsic);
    hlsl_value(e, ir->operands.scalar.left);
    if (builtin->arity > 1u) {
        hlsl_text(e, ", ");
        hlsl_value(e, ir->operands.scalar.right);
    }
    if (builtin->arity > 2u) {
        hlsl_text(e, ", ");
        hlsl_value(e, ir->operands.scalar.binding);
    }
    hlsl_text(e, ")");
}
static void select_expression(HlslEmitter *e, const IrInstruction *ir) {
    const uint32_t width = type_vector_width(e->module, ir->type);
    if (width > 1u) {
        hlsl_type(e, ir->type);
        hlsl_text(e, "(");
    }
    for (uint32_t i = 0u; i < width; ++i) {
        if (i != 0u) hlsl_text(e, ", ");
        hlsl_text(e, "(");
        hlsl_value(e, ir->operands.scalar.binding);
        if (width > 1u) hlsl_format(e, ".%c", "xyzw"[i]);
        hlsl_text(e, " ? ");
        hlsl_value(e, ir->operands.scalar.right);
        if (width > 1u) hlsl_format(e, ".%c", "xyzw"[i]);
        hlsl_text(e, " : ");
        hlsl_value(e, ir->operands.scalar.left);
        if (width > 1u) hlsl_format(e, ".%c", "xyzw"[i]);
        hlsl_text(e, ")");
    }
    if (width > 1u) hlsl_text(e, ")");
}
void hlsl_expression(HlslEmitter *e, const IrInstruction *ir) {
    switch (ir->opcode) {
        case IR_SELECT:
            select_expression(e, ir);
            break;
        case IR_CONSTANT:
            hlsl_constant(e, ir->type,
                ir->operands.scalar.left != 0u ? ir->operands.scalar.binding : ir->operands.scalar.literal,
                ir->operands.scalar.left != 0u);
            break;
        case IR_ADD: case IR_SUBTRACT: case IR_MULTIPLY: case IR_DIVIDE: case IR_REMAINDER:
        case IR_BIT_AND: case IR_BIT_OR: case IR_BIT_XOR: case IR_SHIFT_LEFT: case IR_SHIFT_RIGHT:
        case IR_NEGATE: case IR_COMPLEMENT: case IR_NOT: case IR_CONVERT:
            numeric_operation(e, ir);
            break;
        case IR_BITCAST: {
            const TypeId leaf = type_leaf(e->module, ir->type);
            if (leaf != IR_I32 && leaf != IR_U32 && (leaf != IR_F32 && leaf != IR_F16)) {
                hlsl_error(e, "HLSL bitcast destination has no bit-preserving intrinsic");
                break;
            }
            hlsl_text(e, leaf == IR_I32 ? "asint(" : leaf == IR_U32 ? "asuint(" : "asfloat(");
            hlsl_value(e, ir->operands.scalar.left);
            hlsl_text(e, ")");
            break;
        }
        case IR_BUILTIN: builtin_expression(e, ir); break;
        case IR_COMPARE: {
            static const char *const operators[] = {"==", "!=", "<", "<=", ">", ">="};
            if (ir->operands.scalar.literal > COMPARE_GREATER_EQUAL) {
                hlsl_error(e, "Hlsl comparison identity is invalid");
                break;
            }
            hlsl_text(e, "(");
            hlsl_value(e, ir->operands.scalar.left);
            hlsl_format(e, " %s ", operators[ir->operands.scalar.literal]);
            hlsl_value(e, ir->operands.scalar.right);
            hlsl_text(e, ")");
            break;
        }
        case IR_LOCAL_LOAD: hlsl_format(e, "l%u", ir->operands.scalar.binding); break;
        case IR_STAGE_INPUT: hlsl_stage_input(e, ir); break;
        case IR_ARRAY_LENGTH: hlsl_array_length(e, ir); break;
        case IR_INPUT:
            if (ir->operands.scalar.binding >= e->module->entry_input_count) {
                hlsl_error(e, "Hlsl builtin input identity is invalid");
                break;
            }
            hlsl_format(e, "ctx.input%u", (unsigned)e->module->entry_inputs[ir->operands.scalar.binding].kind);
            if (ir->operands.scalar.literal != NO_ID &&
                !input_scalar(e->module->entry_inputs[ir->operands.scalar.binding].kind))
                hlsl_format(e, "[%u]", ir->operands.scalar.literal);
            break;
        case IR_SWIZZLE:
            hlsl_value(e, ir->operands.scalar.left);
            hlsl_text(e, ".");
            for (uint32_t i = 0u; i < ir->operands.scalar.right; ++i) {
                const char component_name[] = {"xyzw"[(ir->operands.scalar.literal >> (2u * i)) & 3u], '\0'};
                hlsl_text(e, component_name);
            }
            break;
        case IR_COMPOSITE:
            hlsl_error(e, "HLSL composite requires ordered member assignments");
            break;
        case IR_CALL: {
            const uint32_t first = ir->operands.scalar.left, count = ir->operands.scalar.right;
            if (first > e->module->operand_count || count > e->module->operand_count - first ||
                ir->operands.scalar.binding >= e->module->function_count) {
                hlsl_error(e, "Hlsl call target or argument range is invalid");
                break;
            }
            hlsl_format(e, "doe_fn%u(ctx", ir->operands.scalar.binding);
            for (uint32_t i = 0u; i < count; ++i) {
                hlsl_text(e, ", ");
                hlsl_value(e, e->module->typed_operands[first + i]);
            }
            hlsl_text(e, ")");
            break;
        }
        case IR_ROBUST_LOAD: case IR_UNIFORM_LOAD: case IR_MAX: case IR_ROBUST_STORE:
        case IR_DISCARD:
        case IR_IF: case IR_ELSE: case IR_END_IF: case IR_RETURN: case IR_SHORT_BEGIN:
        case IR_SHORT_END: case IR_LOCAL_STORE: case IR_LOOP_BEGIN: case IR_LOOP_TEST:
        case IR_LOOP_CONTINUING: case IR_END_LOOP: case IR_BREAK: case IR_CONTINUE:
        case IR_BREAK_IF: case IR_ACCESS_LOAD: case IR_ACCESS_STORE: case IR_VALUE_ACCESS:
        case IR_WORKGROUP_BARRIER: case IR_HANDLE: case IR_IMAGE:
        case IR_DEFERRED_OVERRIDE:
        case IR_ADDRESS:
        case IR_ATOMIC_LOAD: case IR_ATOMIC_STORE: case IR_ATOMIC_ADD: case IR_ATOMIC_SUBTRACT:
        case IR_ATOMIC_MAX: case IR_ATOMIC_MIN: case IR_ATOMIC_AND: case IR_ATOMIC_OR:
        case IR_ATOMIC_XOR: case IR_ATOMIC_EXCHANGE: case IR_ATOMIC_COMPARE_EXCHANGE:
        default: hlsl_error(e, "Hlsl IR expression has no implemented lowering"); break;
    }
}
