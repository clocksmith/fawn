/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_INTERNAL_H
#define DOE_COMPILER_INTERNAL_H
#include "../include/doe_compiler.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TOKEN_END = 256, TOKEN_WORD, TOKEN_NUMBER,
    TOKEN_EQUAL, TOKEN_NOT_EQUAL, TOKEN_LESS_EQUAL, TOKEN_GREATER_EQUAL
} TokenKind;
enum { MAX_STATEMENTS = 16, MAX_IF_DEPTH = 4,
       /* One invocation value; each assignment needs at most three indices
        * (three operations each), two loads, an add and a store. An if needs
        * at most eight operations including its three structural markers. */
       MAX_IR_INSTRUCTIONS = 1 + MAX_STATEMENTS * 13,
       MAX_IR_VALUES = MAX_IR_INSTRUCTIONS + 1 };
typedef struct { unsigned kind; size_t offset, length; } Token;
typedef struct {
    const char *source;
    size_t length;
    DoeCompilerDiagnostic *diagnostic;
} Source;
typedef struct { Token tokens[DOE_COMPILER_MAX_TOKENS + 1u]; size_t count; } TokenList;
typedef struct { char text[64]; size_t offset; } Name;
typedef struct { uint32_t value; size_t offset; } Integer;
typedef enum { ATOM_NAME, ATOM_MEMBER_X, ATOM_LITERAL } AtomKind;
typedef struct {
    AtomKind kind;
    union { Name name; Integer literal; } value;
} Atom;
typedef enum { INDEX_BASE, INDEX_ADD, INDEX_SUBTRACT } IndexOperation;
typedef struct { Atom base, offset; IndexOperation operation; } Index;
typedef enum { COMPARE_EQUAL, COMPARE_NOT_EQUAL, COMPARE_LESS,
               COMPARE_LESS_EQUAL, COMPARE_GREATER, COMPARE_GREATER_EQUAL } Comparison;
typedef struct { Name storage; Index index; } Reference;
typedef enum { OPERAND_LOAD, OPERAND_LITERAL, OPERAND_NAME } OperandKind;
typedef struct {
    OperandKind kind;
    union { Reference reference; Integer literal; Name name; } value;
} Operand;
typedef struct { Reference target; Operand operands[2]; bool addition; } Assignment;
typedef struct { Index left; Atom right; Comparison comparison; } Condition;
typedef enum { STATEMENT_ASSIGN, STATEMENT_RETURN, STATEMENT_IF } StatementKind;
typedef struct {
    StatementKind kind;
    size_t source_offset;
    union { Assignment assignment; Condition condition; } value;
    /* Preorder ranges: then is [self+1, else_start), else is [else_start,end). */
    size_t else_start, end;
} Statement;
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
    Statement statements[MAX_STATEMENTS];
    size_t statement_count;
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
    IR_INVOCATION_X, IR_ROBUST_LOAD, IR_CONSTANT, IR_ADD, IR_SUBTRACT,
    IR_COMPARE, IR_ROBUST_STORE, IR_IF, IR_ELSE, IR_END_IF, IR_RETURN
} IrOpcode;
typedef enum { IR_U32, IR_F32, IR_BOOL } IrType;
typedef struct { uint32_t id; IrType type; size_t source_offset; } IrValue;
typedef struct {
    IrOpcode opcode;
    IrType type;
    Comparison comparison;
    uint32_t result, left, right, binding, literal;
    size_t source_offset;
} IrInstruction;
typedef struct {
    Name entry, invocation;
    IrBinding bindings[DOE_COMPILER_MAX_BINDINGS];
    size_t binding_count;
    uint32_t local_size[3];
    IrInstruction instructions[MAX_IR_INSTRUCTIONS];
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
