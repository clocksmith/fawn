/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

/* SPIR-V 1.3 numeric enums, from Khronos specification definitions. */
enum {
    OP_NAME = 5, OP_MEMORY_MODEL = 14, OP_ENTRY_POINT = 15, OP_EXECUTION_MODE = 16,
    OP_CAPABILITY = 17, OP_TYPE_VOID = 19, OP_TYPE_BOOL = 20, OP_TYPE_INT = 21,
    OP_TYPE_FLOAT = 22, OP_TYPE_VECTOR = 23, OP_TYPE_RUNTIME_ARRAY = 29, OP_TYPE_STRUCT = 30,
    OP_TYPE_POINTER = 32, OP_TYPE_FUNCTION = 33, OP_CONSTANT = 43, OP_FUNCTION = 54,
    OP_FUNCTION_END = 56, OP_VARIABLE = 59, OP_LOAD = 61, OP_STORE = 62,
    OP_ACCESS_CHAIN = 65, OP_ARRAY_LENGTH = 68, OP_DECORATE = 71,
    OP_MEMBER_DECORATE = 72, OP_COMPOSITE_EXTRACT = 81, OP_IADD = 128, OP_FADD = 129,
    OP_ULESS_THAN = 176, OP_PHI = 245, OP_SELECTION_MERGE = 247, OP_LABEL = 248,
    OP_BRANCH = 249, OP_BRANCH_CONDITIONAL = 250, OP_RETURN = 253
};
enum {
    ID_VOID = 1, ID_BOOL, ID_U32, ID_F32, ID_VEC3,
    ID_ARRAY_U32, ID_ARRAY_F32, ID_BLOCK_U32, ID_BLOCK_F32,
    ID_PTR_BLOCK_U32, ID_PTR_BLOCK_F32, ID_PTR_ELEMENT_U32, ID_PTR_ELEMENT_F32,
    ID_PTR_INPUT, ID_FN_TYPE, ID_ZERO_U32, ID_ZERO_F32,
    ID_INVOCATION, ID_ENTRY, FIRST_DYNAMIC_ID
};
typedef struct {
    Source *source;
    Spirv *output;
    uint32_t next_id, current_label;
    uint32_t buffers[DOE_COMPILER_MAX_BINDINGS];
    uint32_t values[5];
} Emitter;
static uint32_t scalar_type(DoeCompilerScalar scalar) {
    return scalar == DOE_COMPILER_F32 ? ID_F32 : ID_U32;
}
static uint32_t element_pointer(DoeCompilerScalar scalar) {
    return scalar == DOE_COMPILER_F32 ? ID_PTR_ELEMENT_F32 : ID_PTR_ELEMENT_U32;
}
static bool instruction(Emitter *e, uint32_t opcode, const uint32_t *arguments, size_t count) {
    Spirv *s = e->output;
    if (count + 1u > DOE_COMPILER_MAX_WORDS - s->count)
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

static bool module_declarations(Emitter *e, const IrProgram *program) {
    for (size_t i = 0; i < program->binding_count; ++i) e->buffers[i] = e->next_id++;
    EMIT(OP_CAPABILITY, 1u); /* Shader */
    EMIT(OP_MEMORY_MODEL, 0u, 1u); /* Logical, GLSL450 */
    uint32_t entry_arguments[19] = { 5u, ID_ENTRY }; /* GLCompute */
    const size_t entry_string_count = string_words(program->entry.text, entry_arguments + 2u);
    entry_arguments[entry_string_count + 2u] = ID_INVOCATION;
    if (!instruction(e, OP_ENTRY_POINT, entry_arguments, entry_string_count + 3u)) return false;
    EMIT(OP_EXECUTION_MODE, ID_ENTRY, 17u, program->local_size[0], program->local_size[1], program->local_size[2]);
    if (!debug_name(e, ID_ENTRY, program->entry.text) ||
        !debug_name(e, ID_INVOCATION, program->invocation.text)) return false;
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
    EMIT(OP_DECORATE, ID_INVOCATION, 11u, 28u); /* BuiltIn GlobalInvocationId */
    EMIT(OP_TYPE_VOID, ID_VOID);
    EMIT(OP_TYPE_BOOL, ID_BOOL);
    EMIT(OP_TYPE_INT, ID_U32, 32u, 0u);
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
    EMIT(OP_TYPE_POINTER, ID_PTR_INPUT, 1u, ID_VEC3); /* Input */
    EMIT(OP_TYPE_FUNCTION, ID_FN_TYPE, ID_VOID);
    EMIT(OP_CONSTANT, ID_U32, ID_ZERO_U32, 0u);
    EMIT(OP_CONSTANT, ID_F32, ID_ZERO_F32, 0u);
    for (size_t i = 0; i < program->instruction_count; ++i) {
        const IrInstruction *ir = &program->instructions[i];
        if (ir->opcode == IR_CONSTANT) {
            e->values[ir->result] = e->next_id++;
            EMIT(OP_CONSTANT, scalar_type(ir->scalar), e->values[ir->result], ir->literal);
        }
    }
    for (size_t i = 0; i < program->binding_count; ++i) {
        const uint32_t pointer_type = program->bindings[i].scalar == DOE_COMPILER_F32 ?
                                      ID_PTR_BLOCK_F32 : ID_PTR_BLOCK_U32;
        EMIT(OP_VARIABLE, pointer_type, e->buffers[i], 12u);
    }
    EMIT(OP_VARIABLE, ID_PTR_INPUT, ID_INVOCATION, 1u);
    return true;
}
static bool robust_access(Emitter *e, const IrInstruction *ir) {
    const uint32_t length = e->next_id++;
    const uint32_t condition = e->next_id++;
    const uint32_t body = e->next_id++;
    const uint32_t merge = e->next_id++;
    const uint32_t pointer = e->next_id++;
    const uint32_t predecessor = e->current_label;
    EMIT(OP_ARRAY_LENGTH, ID_U32, length, e->buffers[ir->binding], 0u);
    EMIT(OP_ULESS_THAN, ID_BOOL, condition, e->values[ir->left], length);
    EMIT(OP_SELECTION_MERGE, merge, 0u);
    EMIT(OP_BRANCH_CONDITIONAL, condition, body, merge);
    EMIT(OP_LABEL, body);
    /* Even forming the element pointer is dominated by this resource's guard. */
    EMIT(OP_ACCESS_CHAIN, element_pointer(ir->scalar), pointer,
         e->buffers[ir->binding], ID_ZERO_U32, e->values[ir->left]);
    uint32_t loaded = 0u;
    if (ir->opcode == IR_ROBUST_LOAD) {
        loaded = e->next_id++;
        EMIT(OP_LOAD, scalar_type(ir->scalar), loaded, pointer);
    } else {
        EMIT(OP_STORE, pointer, e->values[ir->right]);
    }
    EMIT(OP_BRANCH, merge);
    EMIT(OP_LABEL, merge);
    if (ir->opcode == IR_ROBUST_LOAD) {
        const uint32_t zero = ir->scalar == DOE_COMPILER_F32 ? ID_ZERO_F32 : ID_ZERO_U32;
        EMIT(OP_PHI, scalar_type(ir->scalar), e->values[ir->result], loaded, body, zero, predecessor);
    }
    e->current_label = merge;
    return true;
}
static bool function_body(Emitter *e, const IrProgram *program) {
    EMIT(OP_FUNCTION, ID_VOID, ID_ENTRY, 0u, ID_FN_TYPE);
    e->current_label = e->next_id++;
    EMIT(OP_LABEL, e->current_label);
    for (size_t i = 0; i < program->instruction_count; ++i) {
        const IrInstruction *ir = &program->instructions[i];
        if (ir->result != 0u && ir->opcode != IR_CONSTANT) e->values[ir->result] = e->next_id++;
        switch (ir->opcode) {
            case IR_INVOCATION_X: {
                const uint32_t vector = e->next_id++;
                EMIT(OP_LOAD, ID_VEC3, vector, ID_INVOCATION);
                EMIT(OP_COMPOSITE_EXTRACT, ID_U32, e->values[ir->result], vector, 0u);
                break;
            }
            case IR_ROBUST_LOAD:
            case IR_ROBUST_STORE:
                if (!robust_access(e, ir)) return false;
                break;
            case IR_ADD:
                EMIT(ir->scalar == DOE_COMPILER_F32 ? OP_FADD : OP_IADD,
                     scalar_type(ir->scalar), e->values[ir->result], e->values[ir->left], e->values[ir->right]);
                break;
            case IR_CONSTANT:
                break; /* Module-scope constant already emitted and assigned. */
        }
    }
    return instruction(e, OP_RETURN, NULL, 0u) && instruction(e, OP_FUNCTION_END, NULL, 0u);
}
#undef EMIT

bool emit_spirv(Source *source, const IrProgram *program, Spirv *spirv) {
    memset(spirv, 0, sizeof(*spirv));
    spirv->words[0] = 0x07230203u;
    spirv->words[1] = 0x00010300u;
    spirv->words[2] = 0u; /* Unregistered generator, no borrowed tool identity. */
    spirv->count = 5u;
    Emitter emitter = { .source = source, .output = spirv, .next_id = FIRST_DYNAMIC_ID };
    if (!module_declarations(&emitter, program) || !function_body(&emitter, program)) return false;
    spirv->words[3] = emitter.next_id;
    return true;
}
