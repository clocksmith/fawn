/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_INTERNAL_H
#define DOE_COMPILER_INTERNAL_H
#include "../include/doe_compiler_v2.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TOKEN_END = 256, TOKEN_WORD, TOKEN_NUMBER, TOKEN_DECIMAL,
    TOKEN_EQUAL, TOKEN_NOT_EQUAL, TOKEN_LESS_EQUAL, TOKEN_GREATER_EQUAL
} TokenKind;
enum { MAX_STATEMENTS = 16, MAX_IF_DEPTH = 4,
       MAX_EXPRESSIONS = 64, MAX_EXPRESSION_DEPTH = 16,
       /* Each expression contributes at most a three-operation index plus a
        * load. Each statement contributes at most an eight-operation if,
        * excluding its separately counted child statements/expressions. */
       MAX_IR_INSTRUCTIONS = 1 + MAX_EXPRESSIONS * 4 + MAX_STATEMENTS * 8,
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
typedef struct { Name object, member; } MemberSyntax;
typedef enum { ATOM_NAME, ATOM_MEMBER, ATOM_LITERAL } AtomKind;
typedef struct {
    AtomKind kind;
    union { Name name; MemberSyntax member; Integer literal; } value;
} Atom;
typedef enum { INDEX_BASE, INDEX_ADD, INDEX_SUBTRACT } IndexOperation;
typedef struct { Atom base, offset; IndexOperation operation; } Index;
typedef enum { COMPARE_EQUAL, COMPARE_NOT_EQUAL, COMPARE_LESS,
               COMPARE_LESS_EQUAL, COMPARE_GREATER, COMPARE_GREATER_EQUAL } Comparison;
typedef struct { Name storage; Index index; } Reference;
typedef struct { uint32_t numerator, denominator; size_t offset; } Decimal;
typedef enum {
    EXPRESSION_LOAD, EXPRESSION_INTEGER, EXPRESSION_DECIMAL, EXPRESSION_NAME,
    EXPRESSION_MEMBER, EXPRESSION_ADD, EXPRESSION_SUBTRACT,
    EXPRESSION_MULTIPLY, EXPRESSION_MAX
} ExpressionKind;
typedef struct {
    ExpressionKind kind;
    size_t source_offset;
    unsigned depth;
    uint32_t left, right;
    union { Reference reference; Integer integer; Decimal decimal; Name name; MemberSyntax member; } value;
} Expression;
typedef struct { Reference target; uint32_t expression; } Assignment;
typedef struct {
    Name name, annotation;
    bool annotated;
    DoeCompilerScalar scalar;
    uint32_t expression;
} LocalSyntax;
typedef struct { Index left; Atom right; Comparison comparison; } Condition;
typedef enum { STATEMENT_ASSIGN, STATEMENT_RETURN, STATEMENT_IF, STATEMENT_LET } StatementKind;
typedef struct {
    StatementKind kind;
    size_t source_offset;
    union { Assignment assignment; Condition condition; LocalSyntax local; } value;
    /* Preorder ranges: then is [self+1, else_start), else is [else_start,end). */
    size_t else_start, end;
} Statement;
typedef struct {
    Name name;
    Integer group, binding;
    DoeCompilerScalar scalar;
    DoeCompilerAccess access;
    uint32_t kind;
    Name type_name;
} ResourceSyntax;
typedef struct { Name name, annotation; DoeCompilerScalar scalar; } MemberDeclaration;
typedef struct {
    Name name;
    MemberDeclaration members[DOE_COMPILER_MAX_STRUCT_MEMBERS];
    size_t member_count;
} StructSyntax;
typedef struct {
    ResourceSyntax resources[DOE_COMPILER_MAX_BINDINGS];
    size_t resource_count;
    StructSyntax structs[DOE_COMPILER_MAX_STRUCTS];
    size_t struct_count;
    Name entry, invocation, alias, alias_source;
    Integer local_size;
    bool vector_alias, has_alias, has_constant;
    Name constant_name;
    Integer constant_value;
    Statement statements[MAX_STATEMENTS];
    size_t statement_count;
    Expression expressions[MAX_EXPRESSIONS];
    size_t expression_count;
} Syntax;

typedef struct {
    Name name;
    uint32_t group, binding;
    DoeCompilerScalar scalar;
    DoeCompilerAccess access;
    uint32_t kind, struct_index;
    uint32_t element_stride, runtime_sized, minimum_binding_size_bytes;
    uint32_t type_alignment_bytes, address_space_alignment_bytes;
} IrBinding;
typedef struct {
    Name name;
    DoeCompilerScalar scalar;
    uint32_t offset_bytes, size_bytes, alignment_bytes;
} IrMember;
typedef struct {
    Name name;
    uint32_t member_count, size_bytes, alignment_bytes;
    IrMember members[DOE_COMPILER_MAX_STRUCT_MEMBERS];
} IrStruct;
/* Robust accesses are semantic IR operations, each with its own binding.
 * The backend expands each load/store to an independent structured guard. */
typedef enum {
    IR_INVOCATION_X, IR_ROBUST_LOAD, IR_UNIFORM_LOAD, IR_CONSTANT, IR_ADD, IR_SUBTRACT,
    IR_MULTIPLY, IR_MAX,
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
    IrStruct structs[DOE_COMPILER_MAX_STRUCTS];
    size_t struct_count;
    uint32_t local_size[3];
    IrInstruction instructions[MAX_IR_INSTRUCTIONS];
    size_t instruction_count;
} IrProgram;
typedef struct { uint32_t words[DOE_COMPILER_MAX_WORDS]; size_t count; } Spirv;
/* Parsing owns tokens only until Syntax is complete. The next phase starts
 * the typed program/output member, with no token pointers surviving the call. */
typedef struct {
    Syntax syntax;
    union {
        TokenList tokens;
        struct { IrProgram program; Spirv output; } lowered;
    } phase;
} CompilerWorkspace;

bool source_error(Source *source, DoeCompilerStatus status, size_t offset, const char *message);
bool lex_source(Source *source, TokenList *tokens);
bool parse_decimal(Source *source, const Token *token, Decimal *decimal);
bool decimal_binary32(Source *source, const Decimal *decimal, uint32_t *bits);
bool parse_tokens(Source *source, const TokenList *tokens, Syntax *syntax, bool allow_uniforms);
bool validate_syntax(Source *source, const Syntax *syntax, IrProgram *program);
bool emit_spirv(Source *source, const IrProgram *program, Spirv *spirv);
void reflect_program(const IrProgram *program, DoeCompilerReflection *reflection);
void reflect_program_v2(const IrProgram *program, DoeCompilerReflectionV2 *reflection);
#endif
