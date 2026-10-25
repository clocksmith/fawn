/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "spirv_internal.h"
#include <string.h>

static uint32_t comparison_opcode(Comparison comparison, TypeId type) {
    if (type == IR_BOOL)
        return comparison == COMPARE_EQUAL ? OP_LOGICAL_EQUAL : OP_LOGICAL_NOT_EQUAL;
    if ((type == IR_F32 || type == IR_F16)) {
        switch (comparison) {
            case COMPARE_EQUAL: return OP_FORD_EQUAL;
            case COMPARE_NOT_EQUAL: return OP_FUNORD_NOT_EQUAL;
            case COMPARE_LESS: return OP_FORD_LESS_THAN;
            case COMPARE_LESS_EQUAL: return OP_FORD_LESS_EQUAL;
            case COMPARE_GREATER: return OP_FORD_GREATER_THAN;
            case COMPARE_GREATER_EQUAL: return OP_FORD_GREATER_EQUAL;
        }
    }
    switch (comparison) {
        case COMPARE_EQUAL: return OP_IEQUAL;
        case COMPARE_NOT_EQUAL: return OP_INOT_EQUAL;
        case COMPARE_LESS: return type == IR_I32 ? OP_SLESS_THAN : OP_ULESS_THAN;
        case COMPARE_LESS_EQUAL: return type == IR_I32 ? OP_SLESS_EQUAL : OP_ULESS_EQUAL;
        case COMPARE_GREATER: return type == IR_I32 ? OP_SGREATER_THAN : OP_UGREATER_THAN;
        case COMPARE_GREATER_EQUAL: return type == IR_I32 ? OP_SGREATER_EQUAL : OP_UGREATER_EQUAL;
    }
    return 0u;
}
/* At most nine target identities including the result. Invalid integer inputs
 * select a safe denominator before division, never after an invalid operation. */
static bool emit_division(Emitter *e, TypeId value_type, IrOpcode operation,
                          uint32_t result, uint32_t left, uint32_t right) {
    const bool remainder = operation == IR_REMAINDER;
    if ((value_type == IR_F32 || value_type == IR_F16)) {
        EMIT(remainder ? OP_FREM : OP_FDIV, spirv_scalar_type(value_type), result, left, right);
        return true;
    }
    const bool is_signed = value_type == IR_I32;
    const uint32_t type = spirv_scalar_type(value_type), zero = is_signed ? ID_ZERO_I32 : ID_ZERO_U32;
    uint32_t invalid = spirv_reserve_ids(e, 1u);
    EMIT(OP_IEQUAL, ID_BOOL, invalid, right, zero);
    if (is_signed) {
        const uint32_t minimum = spirv_reserve_ids(e, 1u), negative_one = spirv_reserve_ids(e, 1u);
        const uint32_t overflow = spirv_reserve_ids(e, 1u), combined = spirv_reserve_ids(e, 1u);
        EMIT(OP_IEQUAL, ID_BOOL, minimum, left, ID_MIN_I32);
        EMIT(OP_IEQUAL, ID_BOOL, negative_one, right, ID_NEG_ONE_I32);
        EMIT(OP_LOGICAL_AND, ID_BOOL, overflow, minimum, negative_one);
        EMIT(OP_LOGICAL_OR, ID_BOOL, combined, invalid, overflow);
        invalid = combined;
    }
    const uint32_t safe = spirv_reserve_ids(e, 1u), quotient = spirv_reserve_ids(e, 1u);
    EMIT(OP_SELECT, type, safe, invalid, is_signed ? ID_ONE_I32 : ID_ONE_U32, right);
    EMIT(remainder ? (is_signed ? OP_SREM : OP_UMOD) : (is_signed ? OP_SDIV : OP_UDIV), type, quotient, left, safe);
    EMIT(OP_SELECT, type, result, invalid, remainder ? zero : left, quotient);
    return true;
}
/* Seven target identities including result. Ordered clamps and explicit NaN
 * normalization ensure OpConvertFTo[US] receives only in-range finite input. */
static bool emit_conversion(Emitter *e, TypeId from, TypeId to, uint32_t result, uint32_t input) {
    if (from == to) { EMIT(OP_COPY_OBJECT, spirv_scalar_type(to), result, input); return true; }
    if ((from == IR_F32 || from == IR_F16) && (to == IR_F32 || to == IR_F16)) {
        EMIT(OP_FCONVERT, spirv_scalar_type(to), result, input);
        return true;
    }
    if (from == IR_F16 && (to == IR_I32 || to == IR_U32)) {
        const uint32_t widened = spirv_reserve_ids(e, 1u);
        EMIT(OP_FCONVERT, ID_F32, widened, input);
        return emit_conversion(e, IR_F32, to, result, widened);
    }
    if (to == IR_BOOL) {
        EMIT((from == IR_F32 || from == IR_F16) ? OP_FUNORD_NOT_EQUAL : OP_INOT_EQUAL, ID_BOOL, result, input,
             from == IR_F16 ? ID_ZERO_F16 : from == IR_F32 ? ID_ZERO_F32 : from == IR_I32 ? ID_ZERO_I32 : ID_ZERO_U32);
    } else if (from == IR_BOOL) {
        EMIT(OP_SELECT, spirv_scalar_type(to), result, input,
             to == IR_F16 ? ID_ONE_F16 : to == IR_F32 ? ID_ONE_F32 : to == IR_I32 ? ID_ONE_I32 : ID_ONE_U32,
             to == IR_F16 ? ID_ZERO_F16 : to == IR_F32 ? ID_ZERO_F32 : to == IR_I32 ? ID_ZERO_I32 : ID_ZERO_U32);
    } else if (from == IR_F32) {
        const uint32_t nan = spirv_reserve_ids(e, 1u), finite = spirv_reserve_ids(e, 1u);
        const uint32_t low = spirv_reserve_ids(e, 1u), lower = spirv_reserve_ids(e, 1u);
        const uint32_t high = spirv_reserve_ids(e, 1u), clamped = spirv_reserve_ids(e, 1u);
        const uint32_t minimum = to == IR_I32 ? ID_F32_I32_MIN : ID_ZERO_F32;
        const uint32_t maximum = to == IR_I32 ? ID_F32_I32_MAX : ID_F32_U32_MAX;
        EMIT(OP_IS_NAN, ID_BOOL, nan, input);
        EMIT(OP_SELECT, ID_F32, finite, nan, ID_ZERO_F32, input);
        EMIT(OP_FORD_LESS_THAN, ID_BOOL, low, finite, minimum);
        EMIT(OP_SELECT, ID_F32, lower, low, minimum, finite);
        EMIT(OP_FORD_GREATER_THAN, ID_BOOL, high, lower, maximum);
        EMIT(OP_SELECT, ID_F32, clamped, high, maximum, lower);
        EMIT(to == IR_I32 ? OP_CONVERT_F_TO_S : OP_CONVERT_F_TO_U, spirv_scalar_type(to), result, clamped);
    } else if (to == IR_F32 || to == IR_F16) {
        EMIT(from == IR_I32 ? OP_CONVERT_S_TO_F : OP_CONVERT_U_TO_F, spirv_scalar_type(to), result, input);
    } else { EMIT(OP_BITCAST, spirv_scalar_type(to), result, input); }
    return true;
}
TypeId spirv_value_type(const CompilerWorkspace *c, uint32_t id) {
    if (id > c->limits.instructions) {
        const uint32_t formal = id - c->limits.instructions - 1u;
        return formal < c->formal_count ? c->formals[formal].type.resolved.type : IR_VOID;
    }
    return id != 0u && id <= c->instruction_count ? c->instructions[id - 1u].type : IR_VOID;
}
static bool extract_lane(Emitter *e, TypeId type, uint32_t input, uint32_t lane, uint32_t *result) {
    if (type_vector_width(e->program, type) == 1u) { *result = input; return true; }
    *result = spirv_reserve_ids(e, 1u);
    EMIT(OP_COMPOSITE_EXTRACT, spirv_scalar_type(type_leaf(e->program, type)), *result, input, lane);
    return true;
}
static bool vector_construct(Emitter *e, TypeId type, uint32_t result, const uint32_t lanes[4], uint32_t width) {
    uint32_t *words = NULL;
    if (!spirv_instruction_space(e, OP_COMPOSITE_CONSTRUCT, (size_t)width + 2u, &words)) return false;
    words[0] = spirv_target_type(e, type); words[1] = result;
    for (uint32_t i = 0u; i < width; ++i) words[i + 2u] = lanes[i];
    return true;
}
static bool emit_component_builtin(Emitter *e, NumericBuiltinKind kind, TypeId leaf,
                                   uint32_t result, uint32_t left, uint32_t right, uint32_t upper) {
    const uint32_t condition = spirv_reserve_ids(e, 1u);
    EMIT(comparison_opcode(kind == BUILTIN_MIN ? COMPARE_LESS : COMPARE_GREATER, leaf),
         ID_BOOL, condition, left, right);
    const uint32_t intermediate = kind == BUILTIN_CLAMP ? spirv_reserve_ids(e, 1u) : result;
    EMIT(OP_SELECT, spirv_scalar_type(leaf), intermediate, condition, left, right);
    if (kind == BUILTIN_CLAMP) {
        /* Clamp with reversed runtime bounds is not GLSL Clamp: avoid poison
         * by emitting the specified min(max(value, lower), upper) composition. */
        const uint32_t below = spirv_reserve_ids(e, 1u);
        EMIT(comparison_opcode(COMPARE_LESS, leaf), ID_BOOL, below, intermediate, upper);
        EMIT(OP_SELECT, spirv_scalar_type(leaf), result, below, intermediate, upper);
    }
    return true;
}

static bool emit_integer_dot(Emitter *e, TypeId operand_type, uint32_t result,
                             uint32_t left, uint32_t right) {
    const uint32_t width = type_vector_width(e->program, operand_type);
    const uint32_t products = spirv_reserve_ids(e, 1u);
    const uint32_t leaf = spirv_scalar_type(type_leaf(e->program, operand_type));
    EMIT(OP_IMUL, spirv_target_type(e, operand_type), products, left, right);
    uint32_t partial = 0u;
    for (uint32_t i = 0u; i < width; ++i) {
        uint32_t lane;
        if (!extract_lane(e, operand_type, products, i, &lane)) return false;
        if (i == 0u) partial = lane;
        else {
            const uint32_t sum = i + 1u == width ? result : spirv_reserve_ids(e, 1u);
            EMIT(OP_IADD, leaf, sum, partial, lane);
            partial = sum;
        }
    }
    return true;
}

static bool emit_numeric_builtin(Emitter *e, const IrInstruction *ir) {
    const NumericBuiltin *builtin = builtin_kind((NumericBuiltinKind)ir->operands.scalar.literal);
    if (builtin == NULL) return spirv_emit_error(e, ir->source_offset, "invalid numeric builtin identity");
    const uint32_t result = e->values[ir->result];
    const uint32_t left = e->values[ir->operands.scalar.left];
    const uint32_t right = builtin->arity > 1u ? e->values[ir->operands.scalar.right] : 0u;
    const TypeId operand_type = builtin->arity != 0u ? spirv_value_type(e->program, ir->operands.scalar.left) : IR_BOOL;
    const TypeId leaf = type_leaf(e->program, operand_type);
    if (builtin->family == BUILTIN_DERIVATIVE) {
        EMIT(207u + (uint32_t)builtin->kind - (uint32_t)BUILTIN_DPDX, spirv_target_type(e, ir->type), result, left);
        return true;
    }
    if (builtin->family == BUILTIN_FLOAT_PACK) return spirv_float_pack(e, ir, builtin);
    if (builtin_subgroup(builtin)) return spirv_subgroup_builtin(e, ir, builtin, leaf);
    if (builtin->family == BUILTIN_COMPONENT_INTEGER)
        return spirv_integer_builtin(e, ir, builtin);
    if (builtin->family == BUILTIN_BOOLEAN_REDUCTION) {
        if (leaf != IR_BOOL || ir->type != IR_BOOL ||
            (builtin->kind != BUILTIN_ALL && builtin->kind != BUILTIN_ANY))
            return spirv_emit_error(e, ir->source_offset, "boolean reduction requires boolean operands and scalar result");
        if (operand_type == IR_BOOL) { EMIT(OP_COPY_OBJECT, ID_BOOL, result, left); }
        else { EMIT(builtin->kind == BUILTIN_ALL ? OP_ALL : OP_ANY, ID_BOOL, result, left); }
        return true;
    }
    if (builtin->kind == BUILTIN_ABS && leaf == IR_U32) {
        EMIT(OP_COPY_OBJECT, spirv_target_type(e, ir->type), result, left);
        return true;
    }
    const uint32_t extended = builtin_extended_instruction(builtin, leaf);
    if (extended != 0u) {
        if (builtin->arity == 1u) {
            EMIT(OP_EXT_INST, spirv_target_type(e, ir->type), result, e->math_import,
                 extended, left);
        } else if (builtin->arity == 2u) {
            EMIT(OP_EXT_INST, spirv_target_type(e, ir->type), result, e->math_import,
                 extended, left, right);
        } else {
            EMIT(OP_EXT_INST, spirv_target_type(e, ir->type), result, e->math_import,
                 extended, left, right, e->values[ir->operands.scalar.binding]);
        }
        return true;
    }
    if (builtin->kind == BUILTIN_TRANSPOSE) {
        EMIT(OP_TRANSPOSE, spirv_target_type(e, ir->type), result, left);
        return true;
    }
    if (builtin->kind == BUILTIN_DOT) {
        if ((leaf == IR_F32 || leaf == IR_F16)) {
            EMIT(OP_DOT, spirv_target_type(e, ir->type), result, left, right);
            return true;
        }
        return emit_integer_dot(e, operand_type, result, left, right);
    }
    const uint32_t upper = builtin->arity > 2u ? e->values[ir->operands.scalar.binding] : 0u;
    const uint32_t width = type_vector_width(e->program, operand_type);
    if (width == 1u)
        return emit_component_builtin(e, builtin->kind, leaf, result, left, right, upper);
    uint32_t lanes[4];
    for (uint32_t i = 0u; i < width; ++i) {
        uint32_t a, b, c = 0u;
        if (!extract_lane(e, operand_type, left, i, &a) ||
            !extract_lane(e, operand_type, right, i, &b)) return false;
        if (builtin->kind == BUILTIN_CLAMP && !extract_lane(e, operand_type, upper, i, &c)) return false;
        lanes[i] = spirv_reserve_ids(e, 1u);
        if (!emit_component_builtin(e, builtin->kind, leaf, lanes[i], a, b, c)) return false;
    }
    return vector_construct(e, ir->type, result, lanes, width);
}

static bool emit_shift(Emitter *e, TypeId type, IrOpcode opcode, uint32_t result, uint32_t left, uint32_t right) {
    const uint32_t masked = spirv_reserve_ids(e, 1u);
    EMIT(OP_BIT_AND, ID_U32, masked, right, ID_SHIFT_MASK);
    EMIT(opcode == IR_SHIFT_LEFT ? OP_SHIFT_LEFT : type == IR_I32 ? OP_SHIFT_RIGHT_ARITHMETIC : OP_SHIFT_RIGHT_LOGICAL,
         spirv_scalar_type(type), result, left, masked);
    return true;
}
static bool emit_safe_operation(Emitter *e, const IrInstruction *ir) {
    const uint32_t result = e->values[ir->result];
    const uint32_t left = e->values[ir->operands.scalar.left];
    const uint32_t right = e->values[ir->operands.scalar.right];
    const TypeId leaf = type_leaf(e->program, ir->type);
    const uint32_t width = type_vector_width(e->program, ir->type);
    if (width == 1u) {
        if (ir->opcode == IR_CONVERT) return emit_conversion(e, ir->operands.scalar.binding, ir->type, result, left);
        if (ir->opcode == IR_SHIFT_LEFT || ir->opcode == IR_SHIFT_RIGHT)
            return emit_shift(e, ir->type, ir->opcode, result, left, right);
        return emit_division(e, ir->type, ir->opcode, result, left, right);
    }
    uint32_t lanes[4] = {0};
    const TypeId left_type = ir->operands.scalar.binding;
    const TypeId right_type = ir->operands.scalar.literal;
    for (uint32_t i = 0u; i < width; ++i) {
        uint32_t a, b;
        if (!extract_lane(e, left_type, left, i, &a)) return false;
        lanes[i] = spirv_reserve_ids(e, 1u);
        if (ir->opcode == IR_CONVERT) {
            if (!emit_conversion(e, type_leaf(e->program, left_type), leaf, lanes[i], a)) return false;
        } else {
            if (!extract_lane(e, right_type, right, i, &b)) return false;
            if (ir->opcode == IR_SHIFT_LEFT || ir->opcode == IR_SHIFT_RIGHT) {
                if (!emit_shift(e, leaf, ir->opcode, lanes[i], a, b)) return false;
            } else if (!emit_division(e, leaf, ir->opcode, lanes[i], a, b)) return false;
        }
    }
    return vector_construct(e, ir->type, result, lanes, width);
}
static bool emit_arithmetic(Emitter *e, const IrInstruction *ir) {
    if (type_matrix(e->program, ir->operands.scalar.binding) || type_matrix(e->program, ir->operands.scalar.literal))
        return spirv_matrix_arithmetic(e, ir);
    const bool floating = (type_leaf(e->program, ir->type) == IR_F32 || type_leaf(e->program, ir->type) == IR_F16);
    const uint32_t opcode = ir->opcode == IR_SUBTRACT ? (floating ? OP_FSUB : OP_ISUB) :
        ir->opcode == IR_MULTIPLY ? (floating ? OP_FMUL : OP_IMUL) : floating ? OP_FADD : OP_IADD;
    uint32_t left = e->values[ir->operands.scalar.left], right = e->values[ir->operands.scalar.right];
    const uint32_t width = type_vector_width(e->program, ir->type);
    if (width > 1u) {
        const bool scalar_left = type_vector_width(e->program, ir->operands.scalar.binding) == 1u;
        const bool scalar_right = type_vector_width(e->program, ir->operands.scalar.literal) == 1u;
        if (scalar_left || scalar_right) {
            const uint32_t scalar = scalar_left ? left : right, splat = spirv_reserve_ids(e, 1u);
            const uint32_t lanes[4] = {scalar, scalar, scalar, scalar};
            if (!vector_construct(e, ir->type, splat, lanes, width)) return false;
            if (scalar_left) left = splat; else right = splat;
        }
    }
    EMIT(opcode, spirv_target_type(e, ir->type), e->values[ir->result], left, right);
    return true;
}
static bool emit_composite(Emitter *e, const IrInstruction *ir) {
    const CompilerWorkspace *c = e->program;
    const uint32_t first = ir->operands.composite.first, count = ir->operands.composite.count;
    if (first > c->operand_count || count > c->operand_count - first)
        return spirv_emit_error(e, ir->source_offset, "invalid typed constructor range");
    if (ir->operands.composite.kind == 3u) return spirv_matrix_construct(e, ir);
    if (ir->operands.composite.kind == 0u) {
        uint32_t *words = NULL;
        if (!spirv_instruction_space(e, OP_COMPOSITE_CONSTRUCT, (size_t)count + 2u, &words)) return false;
        words[0] = spirv_target_type(e, ir->type); words[1] = e->values[ir->result];
        for (uint32_t i = 0u; i < count; ++i) words[i + 2u] = e->values[c->typed_operands[first + i]];
        return true;
    }
    const uint32_t width = type_vector_width(c, ir->type);
    uint32_t lanes[4] = {0}, used = 0u;
    if (ir->operands.composite.kind == 2u) {
        if (count != 1u) return spirv_emit_error(e, ir->source_offset, "invalid vector splat operand count");
        for (; used < width; ++used) lanes[used] = e->values[c->typed_operands[first]];
    } else {
        for (uint32_t i = 0u; i < count; ++i) {
            const uint32_t operand = c->typed_operands[first + i];
            const TypeId type = spirv_value_type(c, operand);
            const uint32_t n = type_vector_width(c, type);
            if (n > width - used) return spirv_emit_error(e, ir->source_offset, "invalid vector component count");
            for (uint32_t j = 0u; j < n; ++j)
                if (!extract_lane(e, type, e->values[operand], j, &lanes[used++])) return false;
        }
    }
    if (used != width) return spirv_emit_error(e, ir->source_offset, "incomplete vector components");
    return vector_construct(e, ir->type, e->values[ir->result], lanes, width);
}
static bool emit_swizzle(Emitter *e, const IrInstruction *ir) {
    const uint32_t width = ir->operands.scalar.right, input = e->values[ir->operands.scalar.left];
    if (width == 1u) {
        EMIT(OP_COMPOSITE_EXTRACT, spirv_target_type(e, ir->type), e->values[ir->result], input, ir->operands.scalar.literal & 3u);
    } else {
        uint32_t *words = NULL;
        if (!spirv_instruction_space(e, OP_VECTOR_SHUFFLE, (size_t)width + 4u, &words)) return false;
        words[0] = spirv_target_type(e, ir->type); words[1] = e->values[ir->result]; words[2] = input; words[3] = input;
        for (uint32_t i = 0u; i < width; ++i) words[i + 4u] = (ir->operands.scalar.literal >> (2u * i)) & 3u;
    }
    return true;
}
static bool emit_call(Emitter *e, const IrProgram *program, const IrInstruction *ir) {
    const uint32_t callee = ir->operands.scalar.binding, first = ir->operands.scalar.left, count = ir->operands.scalar.right;
    if (callee >= program->function_count || first > program->operand_count || count > program->operand_count - first)
        return spirv_emit_error(e, ir->source_offset, "invalid typed call range");
    uint32_t *words = NULL;
    if (!spirv_instruction_space(e, OP_FUNCTION_CALL, (size_t)count + 3u, &words)) return false;
    words[0] = spirv_target_type(e, ir->type);
    words[1] = ir->result == 0u ? spirv_reserve_ids(e, 1u) : e->values[ir->result];
    words[2] = program->functions[callee].state.target.function;
    for (uint32_t i = 0u; i < count; ++i) words[3u + i] = e->values[program->typed_operands[first + i]];
    return !e->failed;
}
/* Access traversal uses V, not a local array proportional to type/path depth.
 * The reverse chain is retained until its one guarded pointer/extract is emitted. */
static bool access_walk(Emitter *e, uint32_t path) {
    CompilerWorkspace *c = e->program;
    c->walk_count = 0u;
    while (path != NO_ID) {
        if (path >= c->access_count) return spirv_emit_error(e, 0u, "invalid typed access path");
        if (!spirv_push_walk(e, WALK_ACCESS, path)) return false;
        const uint32_t parent = c->accesses[path].parent;
        if (parent != NO_ID && parent >= path) return spirv_emit_error(e, c->accesses[path].source_offset, "invalid access parent order");
        path = parent;
    }
    return true;
}
static bool literal_index(const CompilerWorkspace *c, uint32_t semantic, uint32_t *value) {
    if (semantic == 0u || semantic > c->instruction_count) return false;
    const IrInstruction *ir = &c->instructions[semantic - 1u];
    if (ir->opcode != IR_CONSTANT || ir->operands.scalar.left != 0u || (ir->type != IR_U32 && ir->type != IR_I32)) return false;
    if (ir->type == IR_I32 && ir->operands.scalar.literal > INT32_MAX) return false;
    *value = ir->operands.scalar.literal;
    return true;
}
static bool access_condition(Emitter *e, const IrInstruction *ir, uint32_t *condition) {
    CompilerWorkspace *c = e->program;
    *condition = 0u;
    for (uint32_t i = c->walk_count; i != 0u; --i) {
        WalkItem *w = &c->walk[i - 1u];
        const AccessStep *a = &c->accesses[w->object];
        if (a->kind == ACCESS_MEMBER || a->kind == ACCESS_COMPONENT) {
            w->cursor = spirv_unsigned_constant(e, a->index); continue;
        }
        const TypeRecord *input = &c->types[a->input_type - IR_PRIMITIVE_COUNT];
        uint32_t length;
        if (input->kind == TYPE_RUNTIME_ARRAY) {
            if (ir->operands.access.category != ROOT_RESOURCE)
                return spirv_emit_error(e, a->source_offset, "runtime array access without resource root");
            length = spirv_reserve_ids(e, 1u);
            if (!spirv_runtime_length(e, ir->operands.access.root, length)) return false;
        } else length = spirv_unsigned_constant(e, input->count);
        uint32_t index = e->values[a->index], check;
        if (a->kind == ACCESS_INDEX_I32) {
            const uint32_t nonnegative = spirv_reserve_ids(e, 1u), converted = spirv_reserve_ids(e, 1u), upper = spirv_reserve_ids(e, 1u);
            check = spirv_reserve_ids(e, 1u);
            EMIT(OP_SGREATER_EQUAL, ID_BOOL, nonnegative, index, ID_ZERO_I32);
            EMIT(OP_BITCAST, ID_U32, converted, index);
            EMIT(OP_ULESS_THAN, ID_BOOL, upper, converted, length);
            EMIT(OP_LOGICAL_AND, ID_BOOL, check, nonnegative, upper);
            index = converted;
        } else {
            check = spirv_reserve_ids(e, 1u);
            EMIT(OP_ULESS_THAN, ID_BOOL, check, index, length);
        }
        w->cursor = index;
        if (*condition != 0u) {
            const uint32_t combined = spirv_reserve_ids(e, 1u);
            EMIT(OP_LOGICAL_AND, ID_BOOL, combined, *condition, check);
            *condition = combined;
        } else *condition = check;
    }
    return !e->failed;
}
static bool emit_pointer_access(Emitter *e, const IrInstruction *ir) {
    CompilerWorkspace *c = e->program;
    const uint32_t root = ir->operands.access.root, category = ir->operands.access.category;
    const bool atomic = atomic_opcode(ir->opcode);
    const bool store = ir->opcode == IR_ACCESS_STORE || ir->opcode == IR_ATOMIC_STORE;
    const TypeId pointer_type = atomic ? atomic_element(c, ir) : ir->type;
    const ResourceRecord *resource = category == ROOT_RESOURCE ? &c->resources[root] : NULL;
    const uint32_t storage = resource != NULL ? spirv_resource_storage(resource) : category == ROOT_WORKGROUP ? c->workgroups[root].state.resolved.address_space : 7u;
    const bool wrapped = (resource != NULL && spirv_wrapped_resource(c, resource)) || storage == MODULE_IMMEDIATE;
    const uint32_t local = category == ROOT_VALUE ? ir->operands.access.value : root;
    const uint32_t base = resource != NULL ? resource->target : category == ROOT_WORKGROUP ? c->workgroups[root].target :
        category == ROOT_POINTER ? e->values[c->limits.instructions + 1u + root] : e->local_base + (local - e->local_start);
    if (category == ROOT_VALUE) { EMIT(OP_STORE, base, e->values[root]); }
    if (!access_walk(e, ir->operands.access.path)) return false;
    uint32_t condition;
    if (!access_condition(e, ir, &condition)) return false;
    const uint32_t predecessor = e->current_label;
    uint32_t body = 0u, merge = 0u;
    if (condition != 0u) {
        body = spirv_reserve_ids(e, 1u); merge = spirv_reserve_ids(e, 1u);
        EMIT(OP_SELECTION_MERGE, merge, 0u);
        EMIT(OP_BRANCH_CONDITIONAL, condition, body, merge);
        EMIT(OP_LABEL, body);
        e->current_label = body;
    }
    const uint32_t loaded = store ? 0u : condition == 0u ? e->values[ir->result] : spirv_reserve_ids(e, 1u);
    if (resource != NULL && spirv_uniform_raw(e, resource)) {
        if (store || !spirv_uniform_load(e, ir, loaded)) return false;
    } else {
        uint32_t pointer = base;
        if (c->walk_count != 0u || wrapped) {
            pointer = spirv_reserve_ids(e, 1u);
            uint32_t *words = NULL;
            if (!spirv_instruction_space(e, OP_ACCESS_CHAIN, (size_t)c->walk_count + (wrapped ? 1u : 0u) + 3u, &words)) return false;
            words[0] = spirv_pointer_type(e, pointer_type, storage); words[1] = pointer; words[2] = base;
            uint32_t output = 3u;
            if (wrapped) words[output++] = ID_ZERO_U32;
            for (uint32_t i = c->walk_count; i != 0u; --i) words[output++] = c->walk[i - 1u].cursor;
        }
        if (atomic) {
            if (!spirv_atomic_operation(e, ir, pointer, loaded)) return false;
        }
        else if (category == ROOT_WORKGROUP && storage != MODULE_IMMEDIATE &&
                 spirv_workgroup_type(e, ir->type) != spirv_target_type(e, ir->type)) {
            const uint32_t converted = spirv_reserve_ids(e, 1u);
            if (store) {
                if (!spirv_workgroup_convert(e, ir->type, e->values[ir->operands.access.value], converted, true)) return false;
                EMIT(OP_STORE, pointer, converted);
            } else {
                EMIT(OP_LOAD, spirv_workgroup_type(e, ir->type), converted, pointer);
                if (!spirv_workgroup_convert(e, ir->type, converted, loaded, false)) return false;
            }
        } else if (store) { EMIT(OP_STORE, pointer, e->values[ir->operands.access.value]); }
        else { EMIT(OP_LOAD, spirv_target_type(e, ir->type), loaded, pointer); }
    }
    if (condition != 0u) {
        EMIT(OP_BRANCH, merge);
        EMIT(OP_LABEL, merge);
        if (!store) { EMIT(OP_PHI, spirv_target_type(e, ir->type), e->values[ir->result], loaded, e->current_label, spirv_zero_constant(e, ir->type), predecessor); }
        e->current_label = merge;
    }
    c->walk_count = 0u;
    return !e->failed;
}
static bool emit_value_access(Emitter *e, const IrInstruction *ir) {
    if (ir->operands.access.value != NO_ID) return emit_pointer_access(e, ir);
    CompilerWorkspace *c = e->program;
    if (!access_walk(e, ir->operands.access.path)) return false;
    uint32_t value = e->values[ir->operands.access.root];
    if (c->walk_count == 0u) {
        EMIT(OP_COPY_OBJECT, spirv_target_type(e, ir->type), e->values[ir->result], value);
        return true;
    }
    /* Literal prefixes may extract aggregates without materializing storage.
     * A dynamic vector step is necessarily final because its result is scalar. */
    for (uint32_t i = c->walk_count; i != 0u; --i) {
        const AccessStep *a = &c->accesses[c->walk[i - 1u].object];
        uint32_t index = a->index;
        const bool literal = a->kind == ACCESS_MEMBER || a->kind == ACCESS_COMPONENT || literal_index(c, a->index, &index);
        const uint32_t result = i == 1u ? e->values[ir->result] : spirv_reserve_ids(e, 1u);
        if (literal) {
            EMIT(OP_COMPOSITE_EXTRACT, spirv_target_type(e, a->output_type), result, value, index);
        } else {
            if (i != 1u || c->types[a->input_type - IR_PRIMITIVE_COUNT].kind != TYPE_VECTOR)
                return spirv_emit_error(e, a->source_offset, "dynamic array value access requires Function temporary");
            /* The selected one-step suffix retains the same common guard owner. */
            const uint32_t saved_count = c->walk_count;
            c->walk_count = 1u;
            uint32_t condition;
            if (!access_condition(e, ir, &condition)) return false;
            c->walk_count = saved_count;
            const uint32_t predecessor = e->current_label;
            const uint32_t body = spirv_reserve_ids(e, 1u), merge = spirv_reserve_ids(e, 1u), extracted = spirv_reserve_ids(e, 1u);
            EMIT(OP_SELECTION_MERGE, merge, 0u);
            EMIT(OP_BRANCH_CONDITIONAL, condition, body, merge);
            EMIT(OP_LABEL, body);
            EMIT(OP_VECTOR_EXTRACT_DYNAMIC, spirv_target_type(e, a->output_type), extracted, value, c->walk[0].cursor);
            EMIT(OP_BRANCH, merge);
            EMIT(OP_LABEL, merge);
            EMIT(OP_PHI, spirv_target_type(e, ir->type), result, extracted, body, spirv_zero_constant(e, ir->type), predecessor);
            e->current_label = merge;
        }
        value = result;
    }
    c->walk_count = 0u;
    return !e->failed;
}
typedef struct EmitLoop EmitLoop;
struct EmitLoop {
    EmitLoop *parent;
    uint32_t header, continuing, merge;
    bool continue_live;
};
static bool emit_block(Emitter *e, const IrProgram *program, size_t *cursor,
                       unsigned depth, EmitLoop *loop, bool *live);
static bool emit_selection(Emitter *e, const IrProgram *program, size_t *cursor,
                           unsigned depth, EmitLoop *loop, bool *live) {
    const IrInstruction *test = &program->instructions[(*cursor)++];
    if (depth >= e->source->limits->control_depth + e->source->limits->expression_depth)
        return source_error(e->source, DOE_COMPILER_INPUT_ERROR, test->source_offset,
                            "internal structured IR depth exceeded");
    const bool active = *live;
    uint32_t yes = 0u, no = 0u, merge = 0u;
    if (active) {
        yes = spirv_reserve_ids(e, 1u);
        no = spirv_reserve_ids(e, 1u);
        merge = spirv_reserve_ids(e, 1u);
        EMIT(OP_SELECTION_MERGE, merge, 0u);
        EMIT(OP_BRANCH_CONDITIONAL, e->values[test->operands.scalar.left], yes, no);
        EMIT(OP_LABEL, yes);
        e->current_label = yes;
    }
    bool yes_live = active;
    if (!emit_block(e, program, cursor, depth + 1u, loop, &yes_live)) return false;
    if (*cursor == program->instruction_count || program->instructions[*cursor].opcode != IR_ELSE)
        return source_error(e->source, DOE_COMPILER_INPUT_ERROR, test->source_offset,
                            "internal structured IR missing else boundary");
    ++*cursor;
    if (yes_live) { EMIT(OP_BRANCH, merge); }
    if (active) {
        EMIT(OP_LABEL, no);
        e->current_label = no;
    }
    bool no_live = active;
    if (!emit_block(e, program, cursor, depth + 1u, loop, &no_live)) return false;
    if (*cursor == program->instruction_count || program->instructions[*cursor].opcode != IR_END_IF)
        return source_error(e->source, DOE_COMPILER_INPUT_ERROR, test->source_offset,
                            "internal structured IR missing merge boundary");
    ++*cursor;
    if (no_live) { EMIT(OP_BRANCH, merge); }
    *live = yes_live || no_live;
    if (active) {
        EMIT(OP_LABEL, merge);
        e->current_label = merge;
        /* A syntactically required merge with no predecessors cannot fall through. */
        if (!*live && !spirv_instruction(e, OP_UNREACHABLE, NULL, 0u)) return false;
    }
    return true;
}
static bool expect_marker(Emitter *e, const IrProgram *program, size_t cursor, IrOpcode opcode) {
    if (cursor < program->instruction_count && program->instructions[cursor].opcode == opcode) return true;
    return source_error(e->source, DOE_COMPILER_INPUT_ERROR, e->source->length,
                        "internal structured IR boundary mismatch");
}
static bool emit_short_circuit(Emitter *e, const IrProgram *program, size_t *cursor,
                               unsigned depth, EmitLoop *loop, bool *live) {
    const IrInstruction *begin = &program->instructions[(*cursor)++];
    const bool active = *live;
    uint32_t rhs = 0u, merge = 0u, predecessor = e->current_label;
    if (active) {
        rhs = spirv_reserve_ids(e, 1u); merge = spirv_reserve_ids(e, 1u);
        EMIT(OP_SELECTION_MERGE, merge, 0u);
        EMIT(OP_BRANCH_CONDITIONAL, e->values[begin->operands.scalar.left],
             begin->operands.scalar.literal != 0u ? merge : rhs, begin->operands.scalar.literal != 0u ? rhs : merge);
        EMIT(OP_LABEL, rhs);
        e->current_label = rhs;
    }
    bool rhs_live = active;
    if (!emit_block(e, program, cursor, depth + 1u, loop, &rhs_live) ||
        !expect_marker(e, program, *cursor, IR_SHORT_END)) return false;
    const IrInstruction *end = &program->instructions[(*cursor)++];
    if (active) {
        const uint32_t rhs_predecessor = e->current_label;
        EMIT(OP_BRANCH, merge);
        EMIT(OP_LABEL, merge);
        e->values[end->result] = spirv_reserve_ids(e, 1u);
        EMIT(OP_PHI, ID_BOOL, e->values[end->result], e->values[end->operands.scalar.left], predecessor,
             e->values[end->operands.scalar.right], rhs_predecessor);
        e->current_label = merge;
    }
    return true;
}
static bool emit_loop(Emitter *e, const IrProgram *program, size_t *cursor,
                      unsigned depth, EmitLoop *outer, bool *live) {
    const IrInstruction *begin = &program->instructions[(*cursor)++];
    const bool active = *live;
    EmitLoop loop = { .parent = outer };
    uint32_t test_label = 0u, body_label = 0u;
    if (active) {
        loop.header = spirv_reserve_ids(e, 1u); test_label = spirv_reserve_ids(e, 1u);
        body_label = spirv_reserve_ids(e, 1u); loop.continuing = spirv_reserve_ids(e, 1u);
        loop.merge = spirv_reserve_ids(e, 1u);
        EMIT(OP_BRANCH, loop.header);
        EMIT(OP_LABEL, loop.header);
        EMIT(OP_LOOP_MERGE, loop.merge, loop.continuing, 0u);
        EMIT(OP_BRANCH, test_label);
        EMIT(OP_LABEL, test_label);
        e->current_label = test_label;
    }
    bool test_live = active;
    if (!emit_block(e, program, cursor, depth + 1u, &loop, &test_live) ||
        !expect_marker(e, program, *cursor, IR_LOOP_TEST)) return false;
    const IrInstruction *test = &program->instructions[(*cursor)++];
    if (active) {
        if (test->operands.scalar.left != 0u) { EMIT(OP_BRANCH_CONDITIONAL, e->values[test->operands.scalar.left], body_label, loop.merge); }
        else { EMIT(OP_BRANCH, body_label); }
        EMIT(OP_LABEL, body_label);
        e->current_label = body_label;
    }
    bool body_live = active;
    if (!emit_block(e, program, cursor, depth + 1u, &loop, &body_live) ||
        !expect_marker(e, program, *cursor, IR_LOOP_CONTINUING)) return false;
    ++*cursor;
    if (body_live) { EMIT(OP_BRANCH, loop.continuing); loop.continue_live = true; }
    if (active) {
        EMIT(OP_LABEL, loop.continuing);
        e->current_label = loop.continuing;
        /* A predecessorless continuing block still supplies the one structural
         * back-edge required by SPIR-V. Its source operations remain suppressed. */
    }
    bool tail_live = active && loop.continue_live;
    if (!emit_block(e, program, cursor, depth + 1u, &loop, &tail_live) ||
        !expect_marker(e, program, *cursor, IR_END_LOOP)) return false;
    ++*cursor;
    if (tail_live || (active && !loop.continue_live)) { EMIT(OP_BRANCH, loop.header); }
    *live = active && ((begin->operands.scalar.literal & 1u) != 0u);
    if (active) {
        EMIT(OP_LABEL, loop.merge);
        e->current_label = loop.merge;
        if (!*live && !spirv_instruction(e, OP_UNREACHABLE, NULL, 0u)) return false;
    }
    return true;
}
static bool emit_workgroup_barrier(Emitter *e, BarrierMemory memory) {
    /* All WGSL barriers synchronize workgroup invocations; the memory class
     * selects WorkgroupMemory, UniformMemory or ImageMemory with AcquireRelease. */
    const uint32_t semantics = memory == BARRIER_WORKGROUP ? e->barrier_semantics :
        spirv_unsigned_constant(e, memory == BARRIER_STORAGE ? 0x48u : 0x808u);
    EMIT(OP_CONTROL_BARRIER, ID_TWO_U32, ID_TWO_U32, semantics);
    return true;
}
static bool emit_block(Emitter *e, const IrProgram *program, size_t *cursor,
                       unsigned depth, EmitLoop *loop, bool *live) {
    while (*cursor < e->function_end) {
        const IrInstruction *ir = &program->instructions[*cursor];
        if (ir->opcode == IR_ELSE || ir->opcode == IR_END_IF || ir->opcode == IR_SHORT_END ||
            ir->opcode == IR_LOOP_TEST || ir->opcode == IR_LOOP_CONTINUING || ir->opcode == IR_END_LOOP) return true;
        if (ir->opcode == IR_IF) {
            if (!emit_selection(e, program, cursor, depth, loop, live)) return false;
            continue;
        }
        if (ir->opcode == IR_SHORT_BEGIN) {
            if (!emit_short_circuit(e, program, cursor, depth, loop, live)) return false;
            continue;
        }
        if (ir->opcode == IR_LOOP_BEGIN) {
            if (!emit_loop(e, program, cursor, depth, loop, live)) return false;
            continue;
        }
        ++*cursor;
        if (!*live) continue;
        if (ir->result != 0u && ir->opcode != IR_CONSTANT) e->values[ir->result] = spirv_reserve_ids(e, 1u);
        const uint32_t result = e->values[ir->result];
        switch (ir->opcode) {
            case IR_DEFERRED_OVERRIDE:
                return source_error(e->source, DOE_COMPILER_INPUT_ERROR, ir->source_offset,
                                    "deferred override reached executable target lowering");
            case IR_ADDRESS:
                /* A local subobject address is a typed path descriptor. Its
                 * bounds are tested only when dereferenced; baseline calls
                 * accept whole-root pointers, never these subobject values. */
                e->values[ir->result] = ir->operands.access.path != NO_ID ||
                    ir->operands.access.category == ROOT_RESOURCE || ir->operands.access.category == ROOT_WORKGROUP ? 0u :
                    ir->operands.access.category == ROOT_POINTER ?
                    e->values[program->limits.instructions + 1u + ir->operands.access.root] :
                    e->local_base + ir->operands.access.root - e->local_start;
                break;
            case IR_ARRAY_LENGTH:
                if (!spirv_runtime_length(e, ir->operands.scalar.binding, result)) return false;
                break;
            case IR_SELECT:
                EMIT(OP_SELECT, spirv_target_type(e, ir->type), result,
                    e->values[ir->operands.scalar.binding], e->values[ir->operands.scalar.right],
                    e->values[ir->operands.scalar.left]);
                break;
            case IR_STAGE_INPUT:
                EMIT(OP_COPY_OBJECT, spirv_target_type(e, ir->type), result,
                    e->values[program->limits.instructions + 1u + ir->operands.scalar.binding]);
                break;
            case IR_INPUT:
                if (e->entry==NULL) return spirv_emit_error(e,ir->source_offset,"helper cannot read an entry input");
                e->entry->state.resolved.target_input_mask|=1u<<program->entry_inputs[ir->operands.scalar.binding].kind; {
                const uint32_t input = program->entry_inputs[ir->operands.scalar.binding].target;
                if (input_scalar(program->entry_inputs[ir->operands.scalar.binding].kind)) {
                    EMIT(OP_LOAD, ID_U32, result, input);
                } else if (ir->operands.scalar.literal == NO_ID) {
                    EMIT(OP_LOAD, ID_VEC3, result, input);
                } else {
                    const uint32_t vector = spirv_reserve_ids(e, 1u);
                    EMIT(OP_LOAD, ID_VEC3, vector, input);
                    EMIT(OP_COMPOSITE_EXTRACT, ID_U32, result, vector, ir->operands.scalar.literal);
                }
                break;
            }
            case IR_HANDLE:
                EMIT(OP_LOAD, spirv_target_type(e, ir->type), result, program->resources[ir->operands.scalar.binding].target);
                break;
            case IR_IMAGE:
                if (!spirv_image_operation(e, ir)) return false;
                break;
            case IR_ATOMIC_LOAD: case IR_ATOMIC_STORE: case IR_ATOMIC_ADD: case IR_ATOMIC_SUBTRACT:
            case IR_ATOMIC_MAX: case IR_ATOMIC_MIN: case IR_ATOMIC_AND: case IR_ATOMIC_OR:
            case IR_ATOMIC_XOR: case IR_ATOMIC_EXCHANGE: case IR_ATOMIC_COMPARE_EXCHANGE:
            case IR_ACCESS_LOAD:
            case IR_ACCESS_STORE:
                if (!emit_pointer_access(e, ir)) return false;
                break;
            case IR_VALUE_ACCESS:
                if (!emit_value_access(e, ir)) return false;
                break;
            case IR_COMPOSITE:
                if (!emit_composite(e, ir)) return false;
                break;
            case IR_SWIZZLE:
                if (!emit_swizzle(e, ir)) return false;
                break;
            case IR_ADD:
            case IR_SUBTRACT:
            case IR_MULTIPLY:
                if (!emit_arithmetic(e, ir)) return false;
                break;
            case IR_DIVIDE:
            case IR_REMAINDER:
            case IR_SHIFT_LEFT:
            case IR_SHIFT_RIGHT:
            case IR_CONVERT:
                if (!emit_safe_operation(e, ir)) return false;
                break;
            case IR_BITCAST:
            case IR_NEGATE:
            case IR_COMPLEMENT:
                EMIT(ir->opcode == IR_BITCAST ? OP_BITCAST : ir->opcode == IR_COMPLEMENT ? OP_NOT :
                     (type_leaf(program, ir->type) == IR_F32 || type_leaf(program, ir->type) == IR_F16) ? OP_FNEGATE : OP_SNEGATE,
                     spirv_target_type(e, ir->type), result, e->values[ir->operands.scalar.left]);
                break;
            case IR_BIT_AND:
            case IR_BIT_OR:
            case IR_BIT_XOR: {
                const uint32_t opcode = type_leaf(program, ir->type) == IR_BOOL ?
                    (ir->opcode == IR_BIT_AND ? OP_LOGICAL_AND : OP_LOGICAL_OR) :
                    ir->opcode == IR_BIT_AND ? OP_BIT_AND : ir->opcode == IR_BIT_OR ? OP_BIT_OR : OP_BIT_XOR;
                EMIT(opcode, spirv_target_type(e, ir->type), result,
                     e->values[ir->operands.scalar.left], e->values[ir->operands.scalar.right]);
                break;
            }
            case IR_CALL:
                if (!emit_call(e, program, ir)) return false;
                break;
            case IR_BUILTIN:
                if (!emit_numeric_builtin(e, ir)) return false;
                break;
            case IR_DISCARD:
                if (!spirv_instruction(e, OP_DEMOTE_TO_HELPER_INVOCATION, NULL, 0u)) return false;
                break;
            case IR_WORKGROUP_BARRIER:
                if (!emit_workgroup_barrier(e, (BarrierMemory)ir->operands.scalar.literal)) return false;
                break;
            case IR_COMPARE:
                EMIT(comparison_opcode((Comparison)ir->operands.scalar.literal, type_leaf(program, ir->operands.scalar.binding)),
                     spirv_target_type(e, ir->type), result, e->values[ir->operands.scalar.left], e->values[ir->operands.scalar.right]);
                break;
            case IR_NOT:
                EMIT(OP_LOGICAL_NOT, spirv_target_type(e, ir->type), result, e->values[ir->operands.scalar.left]);
                break;
            case IR_LOCAL_LOAD:
                EMIT(OP_LOAD, spirv_target_type(e, ir->type), result, e->local_base + (ir->operands.scalar.binding - e->local_start));
                break;
            case IR_LOCAL_STORE:
                EMIT(OP_STORE, e->local_base + (ir->operands.scalar.binding - e->local_start), e->values[ir->operands.scalar.right]);
                break;
            case IR_BREAK:
            case IR_CONTINUE:
            case IR_BREAK_IF:
                if (loop == NULL) return spirv_emit_error(e, ir->source_offset, "internal loop target is missing");
                if (ir->opcode == IR_BREAK_IF) {
                    EMIT(OP_BRANCH_CONDITIONAL, e->values[ir->operands.scalar.left], loop->merge, loop->header);
                } else {
                    EMIT(OP_BRANCH, ir->opcode == IR_BREAK ? loop->merge : loop->continuing);
                    if (ir->opcode == IR_CONTINUE) loop->continue_live = true;
                }
                *live = false;
                break;
            case IR_RETURN:
                if (e->entry != NULL && stage_graphics(&program->functions[e->entry->function])) {
                    if (!spirv_stage_return(e, ir)) return false;
                } else if (ir->type == IR_VOID) {
                    if (!spirv_instruction(e, OP_RETURN, NULL, 0u)) return false;
                } else { EMIT(OP_RETURN_VALUE, e->values[ir->operands.scalar.left]); }
                *live = false;
                break;
            case IR_CONSTANT:
                break;
            case IR_UNIFORM_LOAD:
            case IR_ROBUST_LOAD:
            case IR_ROBUST_STORE:
            case IR_MAX:
                return spirv_emit_error(e, ir->source_offset, "obsolete scalar-only IR operation");
            case IR_SHORT_BEGIN:
            case IR_SHORT_END:
            case IR_LOOP_BEGIN:
            case IR_LOOP_TEST:
            case IR_LOOP_CONTINUING:
            case IR_END_LOOP:
            case IR_IF:
            case IR_ELSE:
            case IR_END_IF:
                return spirv_emit_error(e, ir->source_offset, "unexpected internal structured IR marker");
        }
    }
    return !e->failed;
}
static bool emit_workgroup_initialization(Emitter *e) {
    const uint32_t lane = spirv_reserve_ids(e, 1u), first_lane = spirv_reserve_ids(e, 1u);
    const uint32_t initialize = spirv_reserve_ids(e, 1u), merge = spirv_reserve_ids(e, 1u);
    EMIT(OP_LOAD, ID_U32, lane, e->entry->state.resolved.local_index_target);
    e->entry->state.resolved.target_input_mask|=1u<<INPUT_LOCAL_INDEX;
    EMIT(OP_IEQUAL, ID_BOOL, first_lane, lane, ID_ZERO_U32);
    EMIT(OP_SELECTION_MERGE, merge, 0u);
    EMIT(OP_BRANCH_CONDITIONAL, first_lane, initialize, merge);
    EMIT(OP_LABEL, initialize);
    for (uint32_t i=0u;i<e->entry->state.resolved.workgroup_count;++i) {
        const uint32_t index=e->program->entry_references[e->entry->state.resolved.workgroup_first+i];
        const WorkgroupRoot *root=&e->program->workgroups[index];
        if (root->state.resolved.address_space == MODULE_WORKGROUP)
            EMIT(OP_STORE, root->target, spirv_workgroup_zero(e, root->type));
    }
    EMIT(OP_BRANCH, merge);
    EMIT(OP_LABEL, merge);
    e->current_label = merge;
    return emit_workgroup_barrier(e, BARRIER_WORKGROUP);
}
static bool emit_private_initialization(Emitter *e) {
    const CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < e->entry->state.resolved.workgroup_count; ++i) {
        const WorkgroupRoot *root = &c->workgroups[c->entry_references[e->entry->state.resolved.workgroup_first + i]];
        if (root->state.resolved.address_space != MODULE_PRIVATE) continue;
        uint32_t value = spirv_workgroup_zero(e, root->type);
        if (root->state.resolved.initializer != NO_ID) {
            value = spirv_constant_value(e, c->constant_edges[root->state.resolved.initializer]);
            if (spirv_workgroup_type(e, root->type) != spirv_target_type(e, root->type)) {
                const uint32_t converted = spirv_reserve_ids(e, 1u);
                if (!spirv_workgroup_convert(e, root->type, value, converted, true)) return false;
                value = converted;
            }
        }
        if (value == 0u) return false;
        EMIT(OP_STORE, root->target, value);
    }
    return !e->failed;
}
static bool function_body(Emitter *e, const IrProgram *program, const FunctionRecord *function) {
    EMIT(OP_FUNCTION, stage_graphics(function) ? ID_VOID : spirv_target_type(e, function->result_type), function->state.target.function, 0u, function->state.target.signature);
    for (uint32_t p = 0u; p < function->formal_count; ++p) {
        const uint32_t index = function->formal_start + p;
        const uint32_t identity = e->program->limits.instructions + 1u + index;
        e->values[identity] = spirv_reserve_ids(e, 1u);
        EMIT(OP_FUNCTION_PARAMETER, spirv_target_type(e, program->formals[index].type.resolved.type), e->values[identity]);
    }
    e->current_label = function->state.target.label;
    e->function_end = function->instruction_end;
    EMIT(OP_LABEL, e->current_label);
    e->local_base = spirv_reserve_ids(e, function->local_count);
    e->local_start = function->local_start;
    for (uint32_t i = 0u; i < function->local_count; ++i)
        EMIT(OP_VARIABLE, spirv_pointer_type(e, program->locals[function->local_start + i].type, 7u), e->local_base + i, 7u);
    e->entry=(function->flags&FUNCTION_ENTRY)!=0u?&e->program->entries[function->entry_index]:NULL;
    if (stage_graphics(function) && !spirv_stage_inputs(e, function->entry_index)) return false;
    if (e->entry != NULL) {
        if (!emit_private_initialization(e)) return false;
        if (e->entry->state.resolved.workgroup_storage_bytes != 0u && !emit_workgroup_initialization(e)) return false;
    }
    size_t cursor = function->instruction_start;
    bool live = true;
    if (!emit_block(e, program, &cursor, 0u, NULL, &live)) return false;
    if (cursor != function->instruction_end) return spirv_emit_error(e, (uint32_t)e->source->length, "unexpected top-level structured IR boundary");
    if (live) {
        if (function->result_type != IR_VOID) return spirv_emit_error(e, (uint32_t)e->source->length, "missing typed function return");
        if (!spirv_instruction(e, OP_RETURN, NULL, 0u)) return false;
    }
    return spirv_instruction(e, OP_FUNCTION_END, NULL, 0u);
}
/* Interfaces depend on the instructions actually emitted, not dead source.
 * EntryPoint headers initially contain only names. Insert their bounded input
 * IDs into the private word span after bodies have established those masks. */
static bool finish_entry_interfaces(Emitter *e) {
    CompilerWorkspace *c=e->program;
    size_t cursor=5u;
    for (uint32_t j=0u;j<c->entry_count;++j) {
        while (cursor<c->word_count && (c->words[cursor]&UINT16_MAX)!=OP_ENTRY_POINT)
            cursor+=c->words[cursor]>>16u;
        if (cursor>=c->word_count) return spirv_emit_error(e,0u,"missing staged entry declaration");
        const EntryRecord *entry=&c->entries[j];
        const StageEntry *stage = e->source->stages != NULL ? &e->source->stages->entries[j] : NULL;
        size_t count=spirv_hidden_initialization_input(c,entry)?1u:0u;
        if (stage != NULL) count += stage->interface_count;
        for (uint32_t i=entry->input_first;i<entry->input_first+entry->input_count;++i)
            if ((entry->state.resolved.target_input_mask&(1u<<c->entry_inputs[i].kind))!=0u) ++count;
        const size_t old_width=c->words[cursor]>>16u;
        if (count>UINT16_MAX-old_width)
            return source_error(e->source,DOE_COMPILER_UNSUPPORTED,c->functions[entry->function].name.offset,
                "SPIR-V entry spirv_instruction word-count format capacity exceeded");
        if (count>c->limits.words-c->word_count)
            return source_pool_error(e->source,DOE_COMPILER_POOL_WORDS,c->word_count+count,
                c->functions[entry->function].name.offset,"SPIR-V staging word capacity exceeded");
        size_t insert=cursor+old_width;
        memmove(c->words+insert+count,c->words+insert,(c->word_count-insert)*sizeof(*c->words));
        c->word_count+=count;
        c->words[cursor]=(uint32_t)((old_width+count)<<16u)|OP_ENTRY_POINT;
        for (uint32_t i=entry->input_first;i<entry->input_first+entry->input_count;++i)
            if ((entry->state.resolved.target_input_mask&(1u<<c->entry_inputs[i].kind))!=0u)
                c->words[insert++]=c->entry_inputs[i].target;
        if (spirv_hidden_initialization_input(c,entry)) c->words[insert++]=entry->state.resolved.local_index_target;
        if (stage != NULL)
            for (uint32_t i = stage->interface_first; i < stage->interface_first + stage->interface_count; ++i)
                c->words[insert++] = e->source->stages->interfaces[i].target;
        cursor=insert;
    }
    return true;
}
#undef EMIT
bool emit_spirv(Source *source, CompilerWorkspace *program) {
    program->word_count = 0u; program->target_count = 0u; program->walk_count = 0u;
    if (program->limits.words < 5u)
        return source_pool_error(source, DOE_COMPILER_POOL_WORDS, 5u, source->length,
                                 "SPIR-V staging capacity is smaller than the header");
    /* Physical uniform reconstruction is output-sensitive: each additional ID
     * requires a serialized defining instruction, so W charges its complete
     * contribution without a hidden aggregate-size pool or guessed IR ratio. */
    const uint64_t ceiling = 256u + (uint64_t)program->limits.targets + 64u * (uint64_t)program->limits.instructions +
        3u * (uint64_t)(program->limits.tokens / 6u) + program->limits.tokens / 4u +
        (uint64_t)program->limits.statements + 1u + 11u * (uint64_t)program->limits.accesses + program->limits.resources +
        program->limits.tokens / 8u + (uint64_t)program->limits.entry_inputs + 5u*(uint64_t)program->limits.entries + 8u + spirv_stage_id_budget(source) + program->limits.words;
    const uint32_t bound = ceiling < UINT32_MAX ? (uint32_t)ceiling : UINT32_MAX - 1u;
    const size_t values = (size_t)program->limits.instructions + program->limits.tokens / 4u + 1u;
    memset(program->values, 0, values * sizeof(*program->values));
    program->words[0] = UINT32_C(0x07230203); program->words[1] = UINT32_C(0x00010300);
    program->words[2] = 0u; program->words[3] = 0u; program->words[4] = 0u;
    program->word_count = 5u;
    Emitter emitter = { .source = source, .program = program, .next_id = FIRST_DYNAMIC_ID,
                        .id_bound = bound, .values = program->values };
    if (!spirv_plan_module(&emitter) || !spirv_module_declarations(&emitter)) return false;
    const uint32_t planned = program->target_count, constants_planned = emitter.constants_used;
    for (uint32_t f = 0u; f < program->function_count; ++f)
        if ((program->functions[f].flags & FUNCTION_REACHABLE) != 0u &&
            !function_body(&emitter, program, &program->functions[f])) return false;
    if (emitter.failed || program->target_count != planned || emitter.constants_used != constants_planned)
        return spirv_emit_error(&emitter, (uint32_t)source->length, "target declaration plan changed during body emission");
    if (!finish_entry_interfaces(&emitter)) return false;
    program->words[3] = emitter.next_id;
    return true;
}
