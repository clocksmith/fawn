/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

/* SPIR-V 1.3 numeric enums, from Khronos specification definitions. */
enum {
    OP_NAME = 5, OP_MEMORY_MODEL = 14, OP_ENTRY_POINT = 15, OP_EXECUTION_MODE = 16,
    OP_CAPABILITY = 17, OP_TYPE_VOID = 19, OP_TYPE_BOOL = 20, OP_TYPE_INT = 21,
    OP_TYPE_FLOAT = 22, OP_TYPE_VECTOR = 23, OP_TYPE_RUNTIME_ARRAY = 29, OP_TYPE_STRUCT = 30,
    OP_TYPE_POINTER = 32, OP_TYPE_FUNCTION = 33, OP_CONSTANT_TRUE = 41, OP_CONSTANT_FALSE = 42,
    OP_CONSTANT = 43, OP_FUNCTION = 54,
    OP_FUNCTION_END = 56, OP_VARIABLE = 59, OP_LOAD = 61, OP_STORE = 62,
    OP_ACCESS_CHAIN = 65, OP_ARRAY_LENGTH = 68, OP_DECORATE = 71,
    OP_MEMBER_DECORATE = 72, OP_COMPOSITE_EXTRACT = 81,
    OP_CONVERT_F_TO_U = 109, OP_CONVERT_F_TO_S = 110, OP_CONVERT_S_TO_F = 111, OP_CONVERT_U_TO_F = 112,
    OP_BITCAST = 124, OP_SNEGATE = 126, OP_FNEGATE = 127, OP_IADD = 128, OP_FADD = 129,
    OP_ISUB = 130, OP_FSUB = 131, OP_IMUL = 132, OP_FMUL = 133,
    OP_UDIV = 134, OP_SDIV = 135, OP_FDIV = 136, OP_UMOD = 137, OP_SREM = 138, OP_FREM = 140,
    OP_IS_NAN = 156, OP_LOGICAL_OR = 166, OP_LOGICAL_AND = 167,
    OP_LOGICAL_EQUAL = 164, OP_LOGICAL_NOT_EQUAL = 165, OP_LOGICAL_NOT = 168, OP_SELECT = 169, OP_IEQUAL = 170, OP_INOT_EQUAL = 171,
    OP_UGREATER_THAN = 172, OP_SGREATER_THAN = 173, OP_UGREATER_EQUAL = 174, OP_SGREATER_EQUAL = 175,
    OP_ULESS_THAN = 176, OP_SLESS_THAN = 177, OP_ULESS_EQUAL = 178, OP_SLESS_EQUAL = 179, OP_FORD_EQUAL = 180, OP_FUNORD_NOT_EQUAL = 183,
    OP_FORD_LESS_THAN = 184, OP_FORD_GREATER_THAN = 186, OP_FORD_LESS_EQUAL = 188,
    OP_FORD_GREATER_EQUAL = 190,
    OP_SHIFT_RIGHT_LOGICAL = 194, OP_SHIFT_RIGHT_ARITHMETIC = 195, OP_SHIFT_LEFT = 196,
    OP_BIT_OR = 197, OP_BIT_XOR = 198, OP_BIT_AND = 199, OP_NOT = 200, OP_PHI = 245, OP_LOOP_MERGE = 246, OP_SELECTION_MERGE = 247, OP_LABEL = 248,
    OP_BRANCH = 249, OP_BRANCH_CONDITIONAL = 250, OP_RETURN = 253, OP_UNREACHABLE = 255
};
enum {
    ID_VOID = 1, ID_BOOL, ID_U32, ID_F32, ID_VEC3,
    ID_ARRAY_U32, ID_ARRAY_F32, ID_BLOCK_U32, ID_BLOCK_F32,
    ID_PTR_BLOCK_U32, ID_PTR_BLOCK_F32, ID_PTR_ELEMENT_U32, ID_PTR_ELEMENT_F32,
    ID_PTR_INPUT, ID_PTR_INPUT_U32, ID_FN_TYPE, ID_ZERO_U32, ID_ZERO_F32,
    ID_ENTRY, ID_I32, ID_ZERO_I32, ID_ONE_U32, ID_ONE_I32, ID_ONE_F32,
    ID_MIN_I32, ID_NEG_ONE_I32, ID_SHIFT_MASK, ID_F32_U32_MAX, ID_F32_I32_MAX, ID_F32_I32_MIN,
    FIRST_DYNAMIC_ID
};
typedef struct {
    Source *source;
    Spirv *output;
    uint32_t next_id, current_label;
    uint32_t buffers[DOE_COMPILER_MAX_BINDINGS];
    uint32_t inputs[MAX_ENTRY_PARAMETERS];
    uint32_t *values;
    uint32_t local_base, local_pointers[4];
    bool failed;
    uint32_t uniform_types[DOE_COMPILER_MAX_STRUCTS];
    uint32_t uniform_pointers[DOE_COMPILER_MAX_STRUCTS];
    uint32_t member_indices[DOE_COMPILER_MAX_STRUCT_MEMBERS];
    uint32_t uniform_u32_pointer, uniform_f32_pointer;
} Emitter;
static uint32_t reserve_ids(Emitter *e, uint32_t count) {
    if (count > e->source->limits->target_bound - e->next_id) {
        e->failed = true;
        (void)source_error(e->source, DOE_COMPILER_INPUT_ERROR, e->source->length,
                           "target ID capacity exceeded");
        return 0u;
    }
    const uint32_t first = e->next_id;
    e->next_id += count;
    return first;
}
static uint32_t scalar_type(IrType type) {
    return type == IR_BOOL ? ID_BOOL : type == IR_F32 ? ID_F32 : type == IR_I32 ? ID_I32 : ID_U32;
}
static uint32_t element_pointer(IrType type) {
    return type == IR_F32 ? ID_PTR_ELEMENT_F32 : ID_PTR_ELEMENT_U32;
}
static bool instruction(Emitter *e, uint32_t opcode, const uint32_t *arguments, size_t count) {
    Spirv *s = e->output;
    if (e->failed) return false;
    if (count + 1u > s->capacity - s->count)
        return source_error(e->source, DOE_COMPILER_OUTPUT_TOO_SMALL, e->source->length,
                            "internal SPIR-V word budget exceeded");
    s->words[s->count++] = ((uint32_t)(count + 1u) << 16u) | opcode;
    if (count != 0u) memcpy(s->words + s->count, arguments, count * sizeof(*arguments));
    s->count += count;
    return true;
}
static size_t string_words(const char *name, uint32_t *words) {
    const size_t bytes = strlen(name) + 1u;
    const size_t count = (bytes + 3u) / 4u;
    memset(words, 0, count * sizeof(*words));
    for (size_t i = 0; i < bytes; ++i)
        words[i / 4u] |= (uint32_t)(unsigned char)name[i] << ((uint32_t)(i % 4u) * 8u);
    return count;
}
static bool debug_name(Emitter *e, uint32_t id, const char *name) {
    uint32_t arguments[17] = { id };
    const size_t count = string_words(name, arguments + 1u);
    return instruction(e, OP_NAME, arguments, count + 1u);
}
/* Local checked serialization shorthand only; IR and ownership stay typed. */
#define EMIT(opcode, ...) do { \
    const uint32_t arguments[] = { __VA_ARGS__ }; \
    if (!instruction(e, opcode, arguments, sizeof(arguments) / sizeof(arguments[0]))) return false; \
} while (0)

static uint32_t builtin_decoration(InputKind kind) {
    switch (kind) {
        case INPUT_NUM_WORKGROUPS: return 24u;
        case INPUT_WORKGROUP: return 26u;
        case INPUT_LOCAL_INVOCATION: return 27u;
        case INPUT_GLOBAL_INVOCATION: return 28u;
        case INPUT_LOCAL_INDEX: return 29u;
    }
    return 0u;
}
static bool module_declarations(Emitter *e, const IrProgram *program) {
    for (size_t i = 0; i < program->binding_count; ++i) e->buffers[i] = reserve_ids(e, 1u);
    for (size_t i = 0; i < program->parameter_count; ++i) e->inputs[i] = reserve_ids(e, 1u);
    if (program->struct_count != 0u) {
        e->uniform_u32_pointer = reserve_ids(e, 1u);
        e->uniform_f32_pointer = reserve_ids(e, 1u);
        for (size_t i = 0; i < program->struct_count; ++i) {
            e->uniform_types[i] = reserve_ids(e, 1u);
            e->uniform_pointers[i] = reserve_ids(e, 1u);
        }
        for (size_t i = 0; i < DOE_COMPILER_MAX_STRUCT_MEMBERS; ++i)
            e->member_indices[i] = reserve_ids(e, 1u);
    }
    EMIT(OP_CAPABILITY, 1u); /* Shader */
    EMIT(OP_MEMORY_MODEL, 0u, 1u); /* Logical, GLSL450 */
    /* Model, function, NUL-terminated maximum name, and all declared inputs. */
    uint32_t entry_arguments[2u + (DOE_COMPILER_MAX_NAME_BYTES + 4u) / 4u +
                             MAX_ENTRY_PARAMETERS] = { 5u, ID_ENTRY }; /* GLCompute */
    const size_t entry_string_count = string_words(program->entry.text, entry_arguments + 2u);
    for (size_t i = 0; i < program->parameter_count; ++i)
        entry_arguments[entry_string_count + 2u + i] = e->inputs[i];
    if (!instruction(e, OP_ENTRY_POINT, entry_arguments,
                     entry_string_count + 2u + program->parameter_count)) return false;
    EMIT(OP_EXECUTION_MODE, ID_ENTRY, 17u, program->local_size[0], program->local_size[1], program->local_size[2]);
    if (!debug_name(e, ID_ENTRY, program->entry.text)) return false;
    for (size_t i = 0; i < program->parameter_count; ++i)
        if (!debug_name(e, e->inputs[i], program->parameters[i].name.text)) return false;
    for (size_t i = 0; i < program->binding_count; ++i)
        if (!debug_name(e, e->buffers[i], program->bindings[i].name.text)) return false;
    EMIT(OP_DECORATE, ID_ARRAY_U32, 6u, 4u); /* ArrayStride */
    EMIT(OP_DECORATE, ID_ARRAY_F32, 6u, 4u);
    EMIT(OP_MEMBER_DECORATE, ID_BLOCK_U32, 0u, 35u, 0u); /* member Offset */
    EMIT(OP_MEMBER_DECORATE, ID_BLOCK_F32, 0u, 35u, 0u);
    EMIT(OP_DECORATE, ID_BLOCK_U32, 2u); /* Block */
    EMIT(OP_DECORATE, ID_BLOCK_F32, 2u);
    for (size_t i = 0; i < program->binding_count; ++i) {
        EMIT(OP_DECORATE, e->buffers[i], 34u, program->bindings[i].group); /* DescriptorSet */
        EMIT(OP_DECORATE, e->buffers[i], 33u, program->bindings[i].binding);
        if (program->bindings[i].access == DOE_COMPILER_READ)
            EMIT(OP_DECORATE, e->buffers[i], 24u); /* NonWritable */
    }
    for (size_t i = 0; i < program->struct_count; ++i) {
        EMIT(OP_DECORATE, e->uniform_types[i], 2u);
        for (uint32_t j = 0; j < program->structs[i].member_count; ++j)
            EMIT(OP_MEMBER_DECORATE, e->uniform_types[i], j, 35u,
                 program->structs[i].members[j].offset_bytes);
    }
    for (size_t i = 0; i < program->parameter_count; ++i)
        EMIT(OP_DECORATE, e->inputs[i], 11u, builtin_decoration(program->parameters[i].kind));
    EMIT(OP_TYPE_VOID, ID_VOID);
    EMIT(OP_TYPE_BOOL, ID_BOOL);
    bool signed_type = false, guards = false;
    for (size_t i = 0; i < program->instruction_count; ++i) {
        const IrInstruction *ir = &program->instructions[i];
        signed_type = signed_type || ir->type == IR_I32 || ir->comparison_type == IR_I32;
        guards = guards || ir->opcode == IR_CONVERT || ir->opcode == IR_DIVIDE || ir->opcode == IR_REMAINDER ||
            ir->opcode == IR_SHIFT_LEFT || ir->opcode == IR_SHIFT_RIGHT || ir->comparison_type == IR_I32;
    }
    EMIT(OP_TYPE_INT, ID_U32, 32u, 0u);
    if (signed_type || guards) { EMIT(OP_TYPE_INT, ID_I32, 32u, 1u); }
    EMIT(OP_TYPE_FLOAT, ID_F32, 32u);
    EMIT(OP_TYPE_VECTOR, ID_VEC3, ID_U32, 3u);
    EMIT(OP_TYPE_RUNTIME_ARRAY, ID_ARRAY_U32, ID_U32);
    EMIT(OP_TYPE_RUNTIME_ARRAY, ID_ARRAY_F32, ID_F32);
    EMIT(OP_TYPE_STRUCT, ID_BLOCK_U32, ID_ARRAY_U32);
    EMIT(OP_TYPE_STRUCT, ID_BLOCK_F32, ID_ARRAY_F32);
    EMIT(OP_TYPE_POINTER, ID_PTR_BLOCK_U32, 12u, ID_BLOCK_U32); /* StorageBuffer */
    EMIT(OP_TYPE_POINTER, ID_PTR_BLOCK_F32, 12u, ID_BLOCK_F32);
    EMIT(OP_TYPE_POINTER, ID_PTR_ELEMENT_U32, 12u, ID_U32);
    EMIT(OP_TYPE_POINTER, ID_PTR_ELEMENT_F32, 12u, ID_F32);
    if (program->struct_count != 0u) {
        EMIT(OP_TYPE_POINTER, e->uniform_u32_pointer, 2u, ID_U32); /* Uniform */
        EMIT(OP_TYPE_POINTER, e->uniform_f32_pointer, 2u, ID_F32);
        for (size_t i = 0; i < program->struct_count; ++i) {
            uint32_t fields[1u + DOE_COMPILER_MAX_STRUCT_MEMBERS] = { e->uniform_types[i] };
            const IrStruct *type = &program->structs[i];
            for (size_t j = 0; j < type->member_count; ++j)
                fields[j + 1u] = type->members[j].scalar == DOE_COMPILER_F32 ? ID_F32 : ID_U32;
            if (!instruction(e, OP_TYPE_STRUCT, fields, 1u + type->member_count)) return false;
            EMIT(OP_TYPE_POINTER, e->uniform_pointers[i], 2u, e->uniform_types[i]);
        }
    }
    EMIT(OP_TYPE_POINTER, ID_PTR_INPUT, 1u, ID_VEC3); /* Input */
    EMIT(OP_TYPE_POINTER, ID_PTR_INPUT_U32, 1u, ID_U32);
    if (program->local_count != 0u) {
        for (size_t i = 0u; i < (signed_type || guards ? 4u : 3u); ++i) {
            e->local_pointers[i] = reserve_ids(e, 1u);
            EMIT(OP_TYPE_POINTER, e->local_pointers[i], 7u, scalar_type((IrType)i));
        }
    }
    EMIT(OP_TYPE_FUNCTION, ID_FN_TYPE, ID_VOID);
    EMIT(OP_CONSTANT, ID_U32, ID_ZERO_U32, 0u);
    EMIT(OP_CONSTANT, ID_F32, ID_ZERO_F32, 0u);
    if (signed_type || guards) { EMIT(OP_CONSTANT, ID_I32, ID_ZERO_I32, 0u); }
    if (guards) {
        EMIT(OP_CONSTANT, ID_U32, ID_ONE_U32, 1u);
        EMIT(OP_CONSTANT, ID_I32, ID_ONE_I32, 1u);
        EMIT(OP_CONSTANT, ID_F32, ID_ONE_F32, UINT32_C(0x3f800000));
        EMIT(OP_CONSTANT, ID_I32, ID_MIN_I32, UINT32_C(0x80000000));
        EMIT(OP_CONSTANT, ID_I32, ID_NEG_ONE_I32, UINT32_MAX);
        EMIT(OP_CONSTANT, ID_U32, ID_SHIFT_MASK, 31u);
        EMIT(OP_CONSTANT, ID_F32, ID_F32_U32_MAX, UINT32_C(0x4f7fffff));
        EMIT(OP_CONSTANT, ID_F32, ID_F32_I32_MAX, UINT32_C(0x4effffff));
        EMIT(OP_CONSTANT, ID_F32, ID_F32_I32_MIN, UINT32_C(0xcf000000));
    }
    if (program->struct_count != 0u)
        for (uint32_t i = 0; i < DOE_COMPILER_MAX_STRUCT_MEMBERS; ++i)
            EMIT(OP_CONSTANT, ID_U32, e->member_indices[i], i);
    for (size_t i = 0; i < program->instruction_count; ++i) {
        const IrInstruction *ir = &program->instructions[i];
        if (ir->opcode == IR_CONSTANT) {
            e->values[ir->result] = reserve_ids(e, 1u);
            if (ir->type == IR_BOOL) {
                EMIT(ir->literal != 0u ? OP_CONSTANT_TRUE : OP_CONSTANT_FALSE,
                     ID_BOOL, e->values[ir->result]);
            } else {
                EMIT(OP_CONSTANT, scalar_type(ir->type), e->values[ir->result], ir->literal);
            }
        }
    }
    for (size_t i = 0; i < program->binding_count; ++i) {
        const IrBinding *binding = &program->bindings[i];
        const bool uniform = binding->kind == DOE_COMPILER_UNIFORM_BUFFER;
        const uint32_t pointer_type = uniform ? e->uniform_pointers[binding->struct_index] :
            binding->scalar == DOE_COMPILER_F32 ? ID_PTR_BLOCK_F32 : ID_PTR_BLOCK_U32;
        EMIT(OP_VARIABLE, pointer_type, e->buffers[i], uniform ? 2u : 12u);
    }
    for (size_t i = 0; i < program->parameter_count; ++i) {
        const uint32_t type = program->parameters[i].kind == INPUT_LOCAL_INDEX ?
            ID_PTR_INPUT_U32 : ID_PTR_INPUT;
        EMIT(OP_VARIABLE, type, e->inputs[i], 1u);
    }
    return true;
}
static bool robust_access(Emitter *e, const IrInstruction *ir) {
    const uint32_t length = reserve_ids(e, 1u);
    const uint32_t condition = reserve_ids(e, 1u);
    const uint32_t body = reserve_ids(e, 1u);
    const uint32_t merge = reserve_ids(e, 1u);
    const uint32_t pointer = reserve_ids(e, 1u);
    const uint32_t predecessor = e->current_label;
    EMIT(OP_ARRAY_LENGTH, ID_U32, length, e->buffers[ir->binding], 0u);
    uint32_t index = e->values[ir->left];
    if (ir->comparison_type == IR_I32) {
        const uint32_t unsigned_index = reserve_ids(e, 1u), nonnegative = reserve_ids(e, 1u), in_range = reserve_ids(e, 1u);
        EMIT(OP_BITCAST, ID_U32, unsigned_index, index);
        EMIT(OP_SGREATER_EQUAL, ID_BOOL, nonnegative, index, ID_ZERO_I32);
        EMIT(OP_ULESS_THAN, ID_BOOL, in_range, unsigned_index, length);
        EMIT(OP_LOGICAL_AND, ID_BOOL, condition, nonnegative, in_range);
        index = unsigned_index;
    } else { EMIT(OP_ULESS_THAN, ID_BOOL, condition, index, length); }
    EMIT(OP_SELECTION_MERGE, merge, 0u);
    EMIT(OP_BRANCH_CONDITIONAL, condition, body, merge);
    EMIT(OP_LABEL, body);
    /* Even forming the element pointer is dominated by this resource's guard. */
    EMIT(OP_ACCESS_CHAIN, element_pointer(ir->type), pointer,
         e->buffers[ir->binding], ID_ZERO_U32, index);
    uint32_t loaded = 0u;
    if (ir->opcode == IR_ROBUST_LOAD) {
        loaded = reserve_ids(e, 1u);
        EMIT(OP_LOAD, scalar_type(ir->type), loaded, pointer);
    } else {
        EMIT(OP_STORE, pointer, e->values[ir->right]);
    }
    EMIT(OP_BRANCH, merge);
    EMIT(OP_LABEL, merge);
    if (ir->opcode == IR_ROBUST_LOAD) {
        const uint32_t zero = ir->type == IR_F32 ? ID_ZERO_F32 : ID_ZERO_U32;
        EMIT(OP_PHI, scalar_type(ir->type), e->values[ir->result], loaded, body, zero, predecessor);
    }
    e->current_label = merge;
    return true;
}
static uint32_t comparison_opcode(Comparison comparison, IrType type) {
    if (type == IR_BOOL)
        return comparison == COMPARE_EQUAL ? OP_LOGICAL_EQUAL : OP_LOGICAL_NOT_EQUAL;
    if (type == IR_F32) {
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
static bool emit_division(Emitter *e, const IrInstruction *ir) {
    const uint32_t result = e->values[ir->result], left = e->values[ir->left], right = e->values[ir->right];
    const bool remainder = ir->opcode == IR_REMAINDER;
    if (ir->type == IR_F32) {
        EMIT(remainder ? OP_FREM : OP_FDIV, ID_F32, result, left, right);
        return true;
    }
    const bool is_signed = ir->type == IR_I32;
    const uint32_t type = scalar_type(ir->type), zero = is_signed ? ID_ZERO_I32 : ID_ZERO_U32;
    uint32_t invalid = reserve_ids(e, 1u);
    EMIT(OP_IEQUAL, ID_BOOL, invalid, right, zero);
    if (is_signed) {
        const uint32_t minimum = reserve_ids(e, 1u), negative_one = reserve_ids(e, 1u);
        const uint32_t overflow = reserve_ids(e, 1u), combined = reserve_ids(e, 1u);
        EMIT(OP_IEQUAL, ID_BOOL, minimum, left, ID_MIN_I32);
        EMIT(OP_IEQUAL, ID_BOOL, negative_one, right, ID_NEG_ONE_I32);
        EMIT(OP_LOGICAL_AND, ID_BOOL, overflow, minimum, negative_one);
        EMIT(OP_LOGICAL_OR, ID_BOOL, combined, invalid, overflow);
        invalid = combined;
    }
    const uint32_t safe = reserve_ids(e, 1u), quotient = reserve_ids(e, 1u);
    EMIT(OP_SELECT, type, safe, invalid, is_signed ? ID_ONE_I32 : ID_ONE_U32, right);
    EMIT(remainder ? (is_signed ? OP_SREM : OP_UMOD) : (is_signed ? OP_SDIV : OP_UDIV), type, quotient, left, safe);
    EMIT(OP_SELECT, type, result, invalid, remainder ? zero : left, quotient);
    return true;
}
/* Seven target identities including result. Ordered clamps and explicit NaN
 * normalization ensure OpConvertFTo[US] receives only in-range finite input. */
static bool emit_conversion(Emitter *e, const IrInstruction *ir) {
    const uint32_t result = e->values[ir->result], input = e->values[ir->left];
    const IrType from = ir->comparison_type, to = ir->type;
    if (to == IR_BOOL) {
        EMIT(from == IR_F32 ? OP_FUNORD_NOT_EQUAL : OP_INOT_EQUAL, ID_BOOL, result, input,
             from == IR_F32 ? ID_ZERO_F32 : from == IR_I32 ? ID_ZERO_I32 : ID_ZERO_U32);
    } else if (from == IR_BOOL) {
        EMIT(OP_SELECT, scalar_type(to), result, input,
             to == IR_F32 ? ID_ONE_F32 : to == IR_I32 ? ID_ONE_I32 : ID_ONE_U32,
             to == IR_F32 ? ID_ZERO_F32 : to == IR_I32 ? ID_ZERO_I32 : ID_ZERO_U32);
    } else if (from == IR_F32) {
        const uint32_t nan = reserve_ids(e, 1u), finite = reserve_ids(e, 1u);
        const uint32_t low = reserve_ids(e, 1u), lower = reserve_ids(e, 1u);
        const uint32_t high = reserve_ids(e, 1u), clamped = reserve_ids(e, 1u);
        const uint32_t minimum = to == IR_I32 ? ID_F32_I32_MIN : ID_ZERO_F32;
        const uint32_t maximum = to == IR_I32 ? ID_F32_I32_MAX : ID_F32_U32_MAX;
        EMIT(OP_IS_NAN, ID_BOOL, nan, input);
        EMIT(OP_SELECT, ID_F32, finite, nan, ID_ZERO_F32, input);
        EMIT(OP_FORD_LESS_THAN, ID_BOOL, low, finite, minimum);
        EMIT(OP_SELECT, ID_F32, lower, low, minimum, finite);
        EMIT(OP_FORD_GREATER_THAN, ID_BOOL, high, lower, maximum);
        EMIT(OP_SELECT, ID_F32, clamped, high, maximum, lower);
        EMIT(to == IR_I32 ? OP_CONVERT_F_TO_S : OP_CONVERT_F_TO_U, scalar_type(to), result, clamped);
    } else if (to == IR_F32) {
        EMIT(from == IR_I32 ? OP_CONVERT_S_TO_F : OP_CONVERT_U_TO_F, ID_F32, result, input);
    } else { EMIT(OP_BITCAST, scalar_type(to), result, input); }
    return true;
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
        yes = reserve_ids(e, 1u);
        no = reserve_ids(e, 1u);
        merge = reserve_ids(e, 1u);
        EMIT(OP_SELECTION_MERGE, merge, 0u);
        EMIT(OP_BRANCH_CONDITIONAL, e->values[test->left], yes, no);
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
        if (!*live && !instruction(e, OP_UNREACHABLE, NULL, 0u)) return false;
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
        rhs = reserve_ids(e, 1u); merge = reserve_ids(e, 1u);
        EMIT(OP_SELECTION_MERGE, merge, 0u);
        EMIT(OP_BRANCH_CONDITIONAL, e->values[begin->left],
             begin->literal != 0u ? merge : rhs, begin->literal != 0u ? rhs : merge);
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
        e->values[end->result] = reserve_ids(e, 1u);
        EMIT(OP_PHI, ID_BOOL, e->values[end->result], e->values[end->left], predecessor,
             e->values[end->right], rhs_predecessor);
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
        loop.header = reserve_ids(e, 1u); test_label = reserve_ids(e, 1u);
        body_label = reserve_ids(e, 1u); loop.continuing = reserve_ids(e, 1u);
        loop.merge = reserve_ids(e, 1u);
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
        if (test->left != 0u) { EMIT(OP_BRANCH_CONDITIONAL, e->values[test->left], body_label, loop.merge); }
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
    *live = active && ((begin->literal & 1u) != 0u);
    if (active) {
        EMIT(OP_LABEL, loop.merge);
        e->current_label = loop.merge;
        if (!*live && !instruction(e, OP_UNREACHABLE, NULL, 0u)) return false;
    }
    return true;
}
static bool emit_block(Emitter *e, const IrProgram *program, size_t *cursor,
                       unsigned depth, EmitLoop *loop, bool *live) {
    while (*cursor < program->instruction_count) {
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
        if (!*live) continue; /* Validation already covered unreachable source. */
        if (ir->result != 0u && ir->opcode != IR_CONSTANT) e->values[ir->result] = reserve_ids(e, 1u);
        switch (ir->opcode) {
            case IR_INPUT: {
                const uint32_t input = e->inputs[ir->binding];
                if (program->parameters[ir->binding].kind == INPUT_LOCAL_INDEX) {
                    EMIT(OP_LOAD, ID_U32, e->values[ir->result], input);
                } else {
                    const uint32_t vector = reserve_ids(e, 1u);
                    EMIT(OP_LOAD, ID_VEC3, vector, input);
                    EMIT(OP_COMPOSITE_EXTRACT, ID_U32, e->values[ir->result], vector, ir->literal);
                }
                break;
            }
            case IR_UNIFORM_LOAD: {
                const uint32_t pointer = reserve_ids(e, 1u);
                const uint32_t pointer_type = ir->type == IR_F32 ?
                    e->uniform_f32_pointer : e->uniform_u32_pointer;
                EMIT(OP_ACCESS_CHAIN, pointer_type, pointer, e->buffers[ir->binding],
                     e->member_indices[ir->literal]);
                EMIT(OP_LOAD, scalar_type(ir->type), e->values[ir->result], pointer);
                break;
            }
            case IR_ROBUST_LOAD:
            case IR_ROBUST_STORE:
                if (!robust_access(e, ir)) return false;
                break;
            case IR_ADD:
            case IR_SUBTRACT:
            case IR_MULTIPLY: {
                const bool floating = ir->type == IR_F32;
                const uint32_t opcode = ir->opcode == IR_SUBTRACT ? (floating ? OP_FSUB : OP_ISUB) :
                                        ir->opcode == IR_MULTIPLY ? (floating ? OP_FMUL : OP_IMUL) :
                                        floating ? OP_FADD : OP_IADD;
                EMIT(opcode, scalar_type(ir->type), e->values[ir->result],
                     e->values[ir->left], e->values[ir->right]);
                break;
            }
            case IR_DIVIDE:
            case IR_REMAINDER:
                if (!emit_division(e, ir)) return false;
                break;
            case IR_CONVERT:
                if (!emit_conversion(e, ir)) return false;
                break;
            case IR_BITCAST:
            case IR_NEGATE:
            case IR_COMPLEMENT:
                EMIT(ir->opcode == IR_BITCAST ? OP_BITCAST : ir->opcode == IR_COMPLEMENT ? OP_NOT :
                     ir->type == IR_F32 ? OP_FNEGATE : OP_SNEGATE,
                     scalar_type(ir->type), e->values[ir->result], e->values[ir->left]);
                break;
            case IR_BIT_AND:
            case IR_BIT_OR:
            case IR_BIT_XOR: {
                const uint32_t opcode = ir->type == IR_BOOL ?
                    (ir->opcode == IR_BIT_AND ? OP_LOGICAL_AND : OP_LOGICAL_OR) :
                    ir->opcode == IR_BIT_AND ? OP_BIT_AND : ir->opcode == IR_BIT_OR ? OP_BIT_OR : OP_BIT_XOR;
                EMIT(opcode, scalar_type(ir->type), e->values[ir->result], e->values[ir->left], e->values[ir->right]);
                break;
            }
            case IR_SHIFT_LEFT:
            case IR_SHIFT_RIGHT: {
                const uint32_t masked = reserve_ids(e, 1u);
                EMIT(OP_BIT_AND, ID_U32, masked, e->values[ir->right], ID_SHIFT_MASK);
                EMIT(ir->opcode == IR_SHIFT_LEFT ? OP_SHIFT_LEFT : ir->type == IR_I32 ?
                     OP_SHIFT_RIGHT_ARITHMETIC : OP_SHIFT_RIGHT_LOGICAL, scalar_type(ir->type),
                     e->values[ir->result], e->values[ir->left], masked);
                break;
            }
            case IR_MAX: {
                const uint32_t condition = reserve_ids(e, 1u);
                EMIT(ir->type == IR_F32 ? OP_FORD_GREATER_THAN : ir->type == IR_I32 ? OP_SGREATER_THAN : OP_UGREATER_THAN,
                     ID_BOOL, condition, e->values[ir->left], e->values[ir->right]);
                EMIT(OP_SELECT, scalar_type(ir->type), e->values[ir->result], condition,
                     e->values[ir->left], e->values[ir->right]);
                break;
            }
            case IR_COMPARE:
                EMIT(comparison_opcode(ir->comparison, ir->comparison_type), ID_BOOL, e->values[ir->result],
                     e->values[ir->left], e->values[ir->right]);
                break;
            case IR_NOT:
                EMIT(OP_LOGICAL_NOT, ID_BOOL, e->values[ir->result], e->values[ir->left]);
                break;
            case IR_LOCAL_LOAD:
                EMIT(OP_LOAD, scalar_type(ir->type), e->values[ir->result], e->local_base + ir->binding);
                break;
            case IR_LOCAL_STORE:
                EMIT(OP_STORE, e->local_base + ir->binding, e->values[ir->right]);
                break;
            case IR_BREAK:
            case IR_CONTINUE:
            case IR_BREAK_IF:
                if (loop == NULL)
                    return source_error(e->source, DOE_COMPILER_INPUT_ERROR, ir->source_offset,
                                        "internal loop target is missing");
                if (ir->opcode == IR_BREAK_IF) {
                    EMIT(OP_BRANCH_CONDITIONAL, e->values[ir->left], loop->merge, loop->header);
                } else {
                    EMIT(OP_BRANCH, ir->opcode == IR_BREAK ? loop->merge : loop->continuing);
                    if (ir->opcode == IR_CONTINUE) loop->continue_live = true;
                }
                *live = false;
                break;
            case IR_RETURN:
                if (!instruction(e, OP_RETURN, NULL, 0u)) return false;
                *live = false;
                break;
            case IR_CONSTANT:
                break; /* Module-scope constant already emitted and assigned. */
            case IR_SHORT_BEGIN:
            case IR_SHORT_END:
            case IR_LOOP_BEGIN:
            case IR_LOOP_TEST:
            case IR_LOOP_CONTINUING:
            case IR_END_LOOP:
            case IR_IF:
            case IR_ELSE:
            case IR_END_IF:
                return source_error(e->source, DOE_COMPILER_INPUT_ERROR, ir->source_offset,
                                    "unexpected internal structured IR marker");
        }
    }
    return true;
}
static bool function_body(Emitter *e, const IrProgram *program) {
    EMIT(OP_FUNCTION, ID_VOID, ID_ENTRY, 0u, ID_FN_TYPE);
    e->current_label = reserve_ids(e, 1u);
    EMIT(OP_LABEL, e->current_label);
    e->local_base = reserve_ids(e, (uint32_t)program->local_count);
    for (size_t i = 0u; i < program->local_count; ++i) {
        EMIT(OP_VARIABLE, e->local_pointers[program->locals[i].type], e->local_base + (uint32_t)i, 7u);
    }
    size_t cursor = 0u;
    bool live = true;
    if (!emit_block(e, program, &cursor, 0u, NULL, &live)) return false;
    if (cursor != program->instruction_count)
        return source_error(e->source, DOE_COMPILER_INPUT_ERROR, e->source->length,
                            "unexpected top-level structured IR boundary");
    if (live && !instruction(e, OP_RETURN, NULL, 0u)) return false;
    return instruction(e, OP_FUNCTION_END, NULL, 0u);
}
#undef EMIT

bool emit_spirv(Source *source, const IrProgram *program, Spirv *spirv, uint32_t *values) {
    spirv->count = 0u;
    if (spirv->capacity < 5u)
        return source_error(source, DOE_COMPILER_OUTPUT_TOO_SMALL, source->length,
                            "SPIR-V staging capacity is smaller than the header");
    memset(values, 0, source->limits->values * sizeof(*values));
    spirv->words[0] = 0x07230203u;
    spirv->words[1] = 0x00010300u;
    spirv->words[2] = 0u; /* Unregistered generator, no borrowed tool identity. */
    spirv->words[4] = 0u;
    spirv->count = 5u;
    Emitter emitter = { .source = source, .output = spirv, .next_id = FIRST_DYNAMIC_ID, .values = values };
    if (!module_declarations(&emitter, program) || !function_body(&emitter, program)) return false;
    spirv->words[3] = emitter.next_id;
    return true;
}
