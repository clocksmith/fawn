/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_INTERNAL_H
#define DOE_COMPILER_INTERNAL_H
#include "../include/doe_compiler.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { TOKEN_END = 256, TOKEN_WORD, TOKEN_NUMBER } TokenKind;
typedef struct { unsigned kind; size_t offset, length; } Token;
typedef struct {
    const char *source;
    size_t length;
    DoeCompilerDiagnostic *diagnostic;
} Source;
typedef struct { Token tokens[DOE_COMPILER_MAX_TOKENS + 1u]; size_t count; } TokenList;
typedef struct { char text[64]; size_t offset; } Name;
typedef struct { uint32_t value; size_t offset; } Integer;
typedef struct { Name name; bool member_x; } Index;
typedef struct { Name storage; Index index; } Reference;
typedef enum { OPERAND_LOAD, OPERAND_LITERAL, OPERAND_NAME } OperandKind;
typedef struct {
    OperandKind kind;
    Reference reference;
    Integer literal;
    Name name;
} Operand;
typedef struct {
    Name name;
    Integer group, binding;
    DoeCompilerScalar scalar;
    DoeCompilerAccess access;
} StorageSyntax;
typedef struct {
    StorageSyntax storage[DOE_COMPILER_MAX_BINDINGS];
    size_t storage_count;
    Name entry, invocation, alias, alias_source;
    Integer local_size;
    bool vector_alias, has_alias, has_constant;
    Name constant_name;
    Integer constant_value;
    Reference target;
    Operand operands[2];
} Syntax;

typedef struct {
    Name name;
    uint32_t group, binding;
    DoeCompilerScalar scalar;
    DoeCompilerAccess access;
} IrBinding;
/* Robust accesses are semantic IR operations, each with its own binding.
 * The backend expands each load/store to an independent structured guard. */
typedef enum {
    IR_INVOCATION_X, IR_ROBUST_LOAD, IR_CONSTANT, IR_ADD, IR_ROBUST_STORE
} IrOpcode;
typedef struct {
    IrOpcode opcode;
    DoeCompilerScalar scalar;
    uint32_t result, left, right, binding, literal;
    size_t source_offset;
} IrInstruction;
typedef struct {
    Name entry, invocation;
    IrBinding bindings[DOE_COMPILER_MAX_BINDINGS];
    size_t binding_count;
    uint32_t local_size[3];
    IrInstruction instructions[5];
    size_t instruction_count;
} IrProgram;
typedef struct { uint32_t words[DOE_COMPILER_MAX_WORDS]; size_t count; } Spirv;

bool source_error(Source *source, DoeCompilerStatus status, size_t offset, const char *message);
bool lex_source(Source *source, TokenList *tokens);
bool parse_tokens(Source *source, const TokenList *tokens, Syntax *syntax);
bool validate_syntax(Source *source, const Syntax *syntax, IrProgram *program);
bool emit_spirv(Source *source, const IrProgram *program, Spirv *spirv);
void reflect_program(const IrProgram *program, DoeCompilerReflection *reflection);
#endif
