/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_SPIRV_INTERNAL_H
#define DOE_COMPILER_SPIRV_INTERNAL_H
#include "builtin.h"
#include "spirv_decl.h"
/* SPIR-V 1.3 numeric enums, from Khronos specification definitions. */
enum {
    OP_NAME = 5, OP_EXT_INST_IMPORT = 11, OP_EXT_INST = 12, OP_MEMORY_MODEL = 14, OP_ENTRY_POINT = 15, OP_EXECUTION_MODE = 16,
    OP_CAPABILITY = 17, OP_TYPE_VOID = 19, OP_TYPE_BOOL = 20, OP_TYPE_INT = 21,
    OP_TYPE_FLOAT = 22, OP_TYPE_VECTOR = 23, OP_TYPE_ARRAY = 28, OP_TYPE_RUNTIME_ARRAY = 29, OP_TYPE_STRUCT = 30,
    OP_TYPE_POINTER = 32, OP_TYPE_FUNCTION = 33, OP_CONSTANT_TRUE = 41, OP_CONSTANT_FALSE = 42,
    OP_CONSTANT = 43, OP_CONSTANT_COMPOSITE = 44, OP_CONSTANT_NULL = 46, OP_FUNCTION = 54, OP_FUNCTION_PARAMETER = 55, OP_FUNCTION_CALL = 57,
    OP_FUNCTION_END = 56, OP_VARIABLE = 59, OP_LOAD = 61, OP_STORE = 62,
    OP_ACCESS_CHAIN = 65, OP_ARRAY_LENGTH = 68, OP_DECORATE = 71,
    OP_MEMBER_DECORATE = 72, OP_VECTOR_EXTRACT_DYNAMIC = 77, OP_VECTOR_SHUFFLE = 79,
    OP_COMPOSITE_CONSTRUCT = 80, OP_COMPOSITE_EXTRACT = 81, OP_COPY_OBJECT = 83,
    OP_CONVERT_F_TO_U = 109, OP_CONVERT_F_TO_S = 110, OP_CONVERT_S_TO_F = 111, OP_CONVERT_U_TO_F = 112,
    OP_BITCAST = 124, OP_SNEGATE = 126, OP_FNEGATE = 127, OP_IADD = 128, OP_FADD = 129,
    OP_ISUB = 130, OP_FSUB = 131, OP_IMUL = 132, OP_FMUL = 133,
    OP_UDIV = 134, OP_SDIV = 135, OP_FDIV = 136, OP_UMOD = 137, OP_SREM = 138, OP_FREM = 140,
    OP_DOT = 148, OP_IS_NAN = 156, OP_LOGICAL_OR = 166, OP_LOGICAL_AND = 167,
    OP_LOGICAL_EQUAL = 164, OP_LOGICAL_NOT_EQUAL = 165, OP_LOGICAL_NOT = 168, OP_SELECT = 169, OP_IEQUAL = 170, OP_INOT_EQUAL = 171,
    OP_UGREATER_THAN = 172, OP_SGREATER_THAN = 173, OP_UGREATER_EQUAL = 174, OP_SGREATER_EQUAL = 175,
    OP_ULESS_THAN = 176, OP_SLESS_THAN = 177, OP_ULESS_EQUAL = 178, OP_SLESS_EQUAL = 179, OP_FORD_EQUAL = 180, OP_FUNORD_NOT_EQUAL = 183,
    OP_FORD_LESS_THAN = 184, OP_FORD_GREATER_THAN = 186, OP_FORD_LESS_EQUAL = 188,
    OP_FORD_GREATER_EQUAL = 190,
    OP_SHIFT_RIGHT_LOGICAL = 194, OP_SHIFT_RIGHT_ARITHMETIC = 195, OP_SHIFT_LEFT = 196,
    OP_BIT_OR = 197, OP_BIT_XOR = 198, OP_BIT_AND = 199, OP_NOT = 200, OP_CONTROL_BARRIER = 224,
    OP_PHI = 245, OP_LOOP_MERGE = 246, OP_SELECTION_MERGE = 247, OP_LABEL = 248,
    OP_BRANCH = 249, OP_BRANCH_CONDITIONAL = 250, OP_RETURN = 253, OP_RETURN_VALUE = 254, OP_UNREACHABLE = 255
};
/* Primitive/guard declarations use the frozen bounded allowance, never D or a
 * semantic-value slot. Aggregate declarations are caller-owned descriptors. */
enum {
    ID_VOID = 1, ID_BOOL, ID_U32, ID_F32, ID_I32, ID_VEC3,
    ID_PTR_INPUT, ID_PTR_INPUT_U32, ID_FN_TYPE, ID_ZERO_U32, ID_ZERO_F32,
    ID_ZERO_I32, ID_FALSE, ID_TRUE, ID_ONE_U32, ID_ONE_I32, ID_ONE_F32,
    ID_MIN_I32, ID_NEG_ONE_I32, ID_SHIFT_MASK, ID_F32_U32_MAX,
    ID_F32_I32_MAX, ID_F32_I32_MIN, ID_TWO_U32, ID_THREE_U32, ID_FOUR_U32,
    FIRST_DYNAMIC_ID
};
enum { WALK_TYPE, WALK_CONSTANT, WALK_ACCESS };
typedef struct {
    Source *source;
    CompilerWorkspace *program;
    uint32_t next_id, id_bound, current_label;
    EntryRecord *entry;
    uint32_t *values;
    uint32_t local_base, local_start, local_pointers[4][5], math_import;
    uint32_t barrier_semantics, subgroup_capabilities;
    size_t function_end;
    uint32_t constants_used;
    bool failed;
} Emitter;
bool spirv_emit_error(Emitter *e, uint32_t offset, const char *message);
uint32_t spirv_reserve_ids(Emitter *e, uint32_t count);
uint32_t spirv_scalar_type(TypeId type);
uint32_t spirv_target_type(const Emitter *e, TypeId type);
bool spirv_instruction_space(Emitter *e, uint32_t opcode, size_t count, uint32_t **arguments);
bool spirv_instruction(Emitter *e, uint32_t opcode, const uint32_t *arguments, size_t count);
bool spirv_push_walk(Emitter *e, uint32_t kind, uint32_t object);
uint32_t spirv_unsigned_constant(Emitter *e, uint32_t value);
uint32_t spirv_pointer_type(Emitter *e, TypeId type, uint32_t storage);
uint32_t spirv_zero_constant(Emitter *e, TypeId type);
uint32_t spirv_workgroup_zero(Emitter *e, TypeId type);
bool spirv_wrapped_resource(const CompilerWorkspace *c, const ResourceRecord *r);
uint32_t spirv_resource_storage(const ResourceRecord *r);
bool spirv_plan_module(Emitter *e);
bool spirv_hidden_initialization_input(const CompilerWorkspace *c, const EntryRecord *entry);
bool spirv_module_declarations(Emitter *e);
bool spirv_subgroup_builtin(Emitter *, const IrInstruction *, const NumericBuiltin *, TypeId);
#define EMIT(opcode, ...) do { \
    const uint32_t arguments[] = { __VA_ARGS__ }; \
    if (!spirv_instruction(e, opcode, arguments, sizeof(arguments) / sizeof(arguments[0]))) return false; \
} while (0)
#endif
