/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_INTERNAL_H
#define DOE_COMPILER_INTERNAL_H
#include "../include/doe_compiler_job.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TOKEN_END = 256, TOKEN_WORD, TOKEN_NUMBER, TOKEN_DECIMAL,
    TOKEN_EQUAL, TOKEN_NOT_EQUAL, TOKEN_LESS_EQUAL, TOKEN_GREATER_EQUAL,
    TOKEN_AND, TOKEN_OR, TOKEN_ADD_ASSIGN, TOKEN_SUB_ASSIGN, TOKEN_MUL_ASSIGN,
    TOKEN_INCREMENT, TOKEN_DECREMENT, TOKEN_DIV_ASSIGN, TOKEN_REM_ASSIGN,
    TOKEN_BIT_AND_ASSIGN, TOKEN_BIT_OR_ASSIGN, TOKEN_BIT_XOR_ASSIGN,
    TOKEN_SHIFT_LEFT, TOKEN_SHIFT_RIGHT, TOKEN_SHIFT_LEFT_ASSIGN, TOKEN_SHIFT_RIGHT_ASSIGN
} TokenKind;
enum { MAX_STATEMENTS = 16, MAX_IF_DEPTH = 4,
       MAX_EXPRESSIONS = 320, MAX_EXPRESSION_DEPTH = 18, MAX_ENTRY_PARAMETERS = 5,
       /* One instruction per scalar node, four control/statement operations,
        * and at most one exempt leading global-x alias. */
       MAX_IR_INSTRUCTIONS = 1 + MAX_EXPRESSIONS + MAX_STATEMENTS * 4,
       MAX_IR_VALUES = MAX_IR_INSTRUCTIONS + 1 };
typedef struct { unsigned kind; uint32_t offset, length; } Token;
typedef struct {
    size_t source_bytes, tokens, statements, expressions, physical_expressions;
    size_t instructions, values, locals, constants, words;
    uint32_t expression_depth, control_depth, target_bound;
    bool legacy_counts, uniforms;
} CompilerLimits;
typedef struct {
    const char *source;
    size_t length;
    DoeCompilerDiagnostic *diagnostic;
    const CompilerLimits *limits;
} Source;
typedef struct { Token *tokens; size_t count, capacity; } TokenList;
typedef struct { char text[64]; size_t offset; } Name;
typedef struct { uint32_t value, expression; size_t offset; } Integer;
/* Offsets refer to the caller source, never the phase-unioned token list. */
typedef struct { uint32_t offset, length; } NameSpan;
typedef struct { NameSpan object, member; } MemberSyntax;
typedef enum { COMPARE_EQUAL, COMPARE_NOT_EQUAL, COMPARE_LESS,
               COMPARE_LESS_EQUAL, COMPARE_GREATER, COMPARE_GREATER_EQUAL } Comparison;
typedef struct { NameSpan storage; uint32_t index; } Reference;
typedef struct { uint32_t numerator, denominator; size_t offset; } Decimal;
typedef enum {
    EXPRESSION_LOAD, EXPRESSION_INTEGER, EXPRESSION_DECIMAL, EXPRESSION_NAME,
    EXPRESSION_MEMBER, EXPRESSION_ADD, EXPRESSION_SUBTRACT,
    EXPRESSION_MULTIPLY, EXPRESSION_MAX, EXPRESSION_ABSTRACT_INTEGER,
    EXPRESSION_BOOLEAN, EXPRESSION_COMPARE, EXPRESSION_NOT, EXPRESSION_AND, EXPRESSION_OR,
    EXPRESSION_LITERAL, EXPRESSION_NEGATE, EXPRESSION_COMPLEMENT,
    EXPRESSION_DIVIDE, EXPRESSION_REMAINDER, EXPRESSION_BIT_AND, EXPRESSION_BIT_OR,
    EXPRESSION_BIT_XOR, EXPRESSION_SHIFT_LEFT, EXPRESSION_SHIFT_RIGHT,
    EXPRESSION_CONSTRUCT, EXPRESSION_BITCAST
} ExpressionKind;
typedef struct {
    uint32_t source_offset;
    ExpressionKind kind;
    unsigned depth;
    uint32_t left, right;
    union { Reference reference; uint32_t integer;
            struct { uint32_t numerator, denominator; } decimal;
            NameSpan name; MemberSyntax member; Comparison comparison;
            struct { NameSpan callee, type; } call; } value;
    bool grouped;
} Expression;
typedef enum { IR_U32, IR_F32, IR_BOOL, IR_I32, IR_ABSTRACT_INT, IR_ABSTRACT_FLOAT } IrType;
typedef struct { uint64_t bits; IrType type; } NumericValue;
typedef enum { UPDATE_SET, UPDATE_ADD, UPDATE_SUBTRACT, UPDATE_MULTIPLY,
               UPDATE_INCREMENT, UPDATE_DECREMENT, UPDATE_DIVIDE, UPDATE_REMAINDER,
               UPDATE_BIT_AND, UPDATE_BIT_OR, UPDATE_BIT_XOR, UPDATE_SHIFT_LEFT, UPDATE_SHIFT_RIGHT } UpdateKind;
typedef struct {
    Reference target;
    uint32_t expression;
    UpdateKind operation;
    bool local;
} Assignment;
typedef struct {
    NameSpan name, annotation;
    bool annotated, mutable, initialized, constant;
    IrType type;
    uint32_t expression;
} LocalSyntax;
typedef enum {
    STATEMENT_ASSIGN, STATEMENT_RETURN, STATEMENT_IF, STATEMENT_LET,
    STATEMENT_VAR, STATEMENT_BLOCK, STATEMENT_FOR, STATEMENT_WHILE,
    STATEMENT_LOOP, STATEMENT_CONTINUING, STATEMENT_BREAK, STATEMENT_CONTINUE,
    STATEMENT_BREAK_IF, STATEMENT_EMPTY, STATEMENT_CONST
} StatementKind;
typedef struct {
    StatementKind kind;
    uint32_t source_offset;
    union { Assignment assignment; uint32_t condition; LocalSyntax local; } value;
    uint32_t body_start, else_start, end, initializer, update;
} Statement;
typedef struct {
    NameSpan name;
    Integer group, binding;
    DoeCompilerScalar scalar;
    DoeCompilerAccess access;
    uint32_t kind;
    NameSpan type_name;
} ResourceSyntax;
typedef struct { NameSpan name, annotation; DoeCompilerScalar scalar; } MemberDeclaration;
typedef struct {
    NameSpan name;
    MemberDeclaration members[DOE_COMPILER_MAX_STRUCT_MEMBERS];
    size_t member_count;
} StructSyntax;
typedef enum {
    INPUT_GLOBAL_INVOCATION, INPUT_LOCAL_INVOCATION, INPUT_WORKGROUP,
    INPUT_NUM_WORKGROUPS, INPUT_LOCAL_INDEX
} InputKind;
typedef struct { Name name; InputKind kind; bool vector_alias; } EntryParameter;
/* Dependency traversal state is dead before this record publishes its value. */
typedef struct {
    NameSpan name, annotation;
    uint32_t first_expression, expression;
    union { uint64_t bits; struct { uint32_t scan, parent; } dependency; } value;
    IrType type;
    uint32_t state;
} ConstantSyntax;
typedef struct {
    ResourceSyntax resources[DOE_COMPILER_MAX_BINDINGS];
    size_t resource_count;
    StructSyntax structs[DOE_COMPILER_MAX_STRUCTS];
    size_t struct_count;
    Name entry, alias, alias_source;
    EntryParameter parameters[MAX_ENTRY_PARAMETERS];
    size_t parameter_count;
    Integer local_size[3];
    bool has_alias;
    ConstantSyntax *constants;
    size_t constant_count, constant_capacity;
    Statement *statements;
    size_t statement_count, statement_capacity;
    Expression *expressions;
    size_t expression_count, expression_capacity, expression_budget_count;
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
    IR_INPUT, IR_ROBUST_LOAD, IR_UNIFORM_LOAD, IR_CONSTANT, IR_ADD, IR_SUBTRACT,
    IR_MULTIPLY, IR_MAX,
    IR_COMPARE, IR_ROBUST_STORE, IR_IF, IR_ELSE, IR_END_IF, IR_RETURN,
    IR_NOT, IR_SHORT_BEGIN, IR_SHORT_END, IR_LOCAL_LOAD, IR_LOCAL_STORE,
    IR_LOOP_BEGIN, IR_LOOP_TEST, IR_LOOP_CONTINUING, IR_END_LOOP,
    IR_BREAK, IR_CONTINUE, IR_BREAK_IF, IR_NEGATE, IR_COMPLEMENT,
    IR_DIVIDE, IR_REMAINDER, IR_BIT_AND, IR_BIT_OR, IR_BIT_XOR,
    IR_SHIFT_LEFT, IR_SHIFT_RIGHT, IR_CONVERT, IR_BITCAST
} IrOpcode;
typedef struct { uint32_t id; IrType type; size_t source_offset; } IrValue;
typedef struct {
    IrOpcode opcode;
    IrType type;
    Comparison comparison;
    IrType comparison_type;
    uint32_t result, left, right, binding, literal;
    size_t source_offset;
} IrInstruction;
typedef struct { IrType type; bool mutable; } IrLocal;
typedef struct {
    Name name; IrValue value; NumericValue constant_value;
    uint32_t local_id, declaration_offset; bool mutable, constant;
} LocalBinding;
typedef struct {
    Name entry;
    EntryParameter parameters[MAX_ENTRY_PARAMETERS];
    size_t parameter_count;
    IrBinding bindings[DOE_COMPILER_MAX_BINDINGS];
    size_t binding_count;
    IrStruct structs[DOE_COMPILER_MAX_STRUCTS];
    size_t struct_count;
    uint32_t local_size[3];
    IrInstruction *instructions;
    size_t instruction_count, instruction_capacity;
    IrLocal *locals;
    size_t local_count, local_capacity;
} IrProgram;
typedef struct { uint32_t *words; size_t count, capacity; } Spirv;
typedef struct {
    CompilerLimits limits;
    Syntax syntax;
    TokenList tokens;
    IrProgram program;
    Spirv output;
    LocalBinding *scope;
    uint32_t *target_values;
} CompilerWorkspace;
/* Fixed adapters own typed storage; extended jobs bind caller allocation slices. */
typedef struct {
    CompilerWorkspace job;
    Statement statements[MAX_STATEMENTS];
    Expression expressions[MAX_EXPRESSIONS + MAX_STATEMENTS + 1u];
    ConstantSyntax constants[MAX_EXPRESSIONS + 1u];
    union {
        Token tokens[DOE_COMPILER_MAX_TOKENS + 1u];
        struct {
            IrInstruction instructions[MAX_IR_INSTRUCTIONS];
            IrLocal locals[MAX_STATEMENTS + 1u];
            LocalBinding scope[MAX_STATEMENTS + 1u];
            uint32_t target_values[MAX_IR_VALUES];
            uint32_t words[DOE_COMPILER_MAX_WORDS];
        } lowered;
    } phase;
} LegacyWorkspace;
bool compiler_job_layout(const DoeCompilerJobCapacities *capacities,
                         CompilerLimits *limits, size_t *size_bytes);
void compiler_job_bind(CompilerWorkspace *job, void *memory, const CompilerLimits *limits);
void compiler_legacy_bind(LegacyWorkspace *workspace, bool uniforms);

bool source_error(Source *source, DoeCompilerStatus status, size_t offset, const char *message);
bool lex_source(Source *source, TokenList *tokens);
bool parse_tokens(Source *source, const TokenList *tokens, Syntax *syntax, bool allow_uniforms);
bool validate_syntax(Source *source, Syntax *syntax, IrProgram *program, LocalBinding *scope);
bool emit_spirv(Source *source, const IrProgram *program, Spirv *spirv, uint32_t *values);
void reflect_program(const IrProgram *program, DoeCompilerReflection *reflection);
void reflect_program_v2(const IrProgram *program, DoeCompilerReflectionV2 *reflection);
#endif
