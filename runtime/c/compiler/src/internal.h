/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_INTERNAL_H
#define DOE_COMPILER_INTERNAL_H
#include "../include/doe_compiler_job.h"
#include "../include/doe_compiler_resources.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NO_ID UINT32_MAX
/* Primitive logical identities are not entries in the aggregate type pool. */
typedef enum { IR_VOID, IR_BOOL, IR_I32, IR_U32, IR_F32, IR_ABSTRACT_INT, IR_ABSTRACT_FLOAT } IrType;
typedef uint32_t TypeId;
typedef enum {
    TOKEN_END = 256, TOKEN_WORD, TOKEN_NUMBER, TOKEN_DECIMAL,
    TOKEN_EQUAL, TOKEN_NOT_EQUAL, TOKEN_LESS_EQUAL, TOKEN_GREATER_EQUAL,
    TOKEN_AND, TOKEN_OR, TOKEN_ADD_ASSIGN, TOKEN_SUB_ASSIGN, TOKEN_MUL_ASSIGN,
    TOKEN_INCREMENT, TOKEN_DECREMENT, TOKEN_DIV_ASSIGN, TOKEN_REM_ASSIGN,
    TOKEN_BIT_AND_ASSIGN, TOKEN_BIT_OR_ASSIGN, TOKEN_BIT_XOR_ASSIGN,
    TOKEN_SHIFT_LEFT, TOKEN_SHIFT_RIGHT, TOKEN_SHIFT_LEFT_ASSIGN, TOKEN_SHIFT_RIGHT_ASSIGN, TOKEN_ARROW
} TokenKind;
enum { MAX_STATEMENTS = 16, MAX_IF_DEPTH = 4, MAX_EXPRESSIONS = 320,
    MAX_EXPRESSION_DEPTH = 18, MAX_ENTRY_PARAMETERS = 5,
    MAX_IR_INSTRUCTIONS = 385, MAX_IR_VALUES = 386,
    FIXED_TYPES = 32, FIXED_MEMBERS = 64, FIXED_RESOURCES = 8,
    FIXED_ACCESSES = 337, FIXED_CONSTANTS = 64, FIXED_EDGES = 128,
    FIXED_GENERIC = 32, FIXED_WALK = 97, FIXED_TARGETS = 464 };
typedef struct { unsigned kind; uint32_t offset, length; } Token;
typedef struct { uint32_t offset, length; } NameSpan;
typedef struct { char text[64]; size_t offset; } Name;
typedef struct { uint32_t offset, syntax; } TypeRef;
typedef struct { uint32_t value, expression, offset; } Integer;
typedef enum { COMPARE_EQUAL, COMPARE_NOT_EQUAL, COMPARE_LESS,
    COMPARE_LESS_EQUAL, COMPARE_GREATER, COMPARE_GREATER_EQUAL } Comparison;
typedef enum {
    EXPRESSION_LOAD, EXPRESSION_INTEGER, EXPRESSION_DECIMAL, EXPRESSION_NAME,
    EXPRESSION_MEMBER, EXPRESSION_ADD, EXPRESSION_SUBTRACT,
    EXPRESSION_MULTIPLY, EXPRESSION_MAX, EXPRESSION_ABSTRACT_INTEGER,
    EXPRESSION_BOOLEAN, EXPRESSION_COMPARE, EXPRESSION_NOT, EXPRESSION_AND, EXPRESSION_OR,
    EXPRESSION_LITERAL, EXPRESSION_NEGATE, EXPRESSION_COMPLEMENT,
    EXPRESSION_DIVIDE, EXPRESSION_REMAINDER, EXPRESSION_BIT_AND, EXPRESSION_BIT_OR,
    EXPRESSION_BIT_XOR, EXPRESSION_SHIFT_LEFT, EXPRESSION_SHIFT_RIGHT,
    EXPRESSION_CONSTRUCT, EXPRESSION_BITCAST, EXPRESSION_CALL, EXPRESSION_ADDRESS, EXPRESSION_DEREFERENCE
} ExpressionKind;
typedef struct {
    uint32_t source_offset; ExpressionKind kind; unsigned depth; uint32_t left, right;
    union { uint32_t integer; NameSpan name; Comparison comparison;
        struct { NameSpan object, member; } member;
        struct { NameSpan storage; uint32_t index; } reference;
        struct { NameSpan callee; TypeRef type; } call; } value;
    bool grouped;
} Expression;
typedef enum {
    BUILTIN_MIN, BUILTIN_MAX, BUILTIN_CLAMP, BUILTIN_EXP, BUILTIN_TANH,
    BUILTIN_SQRT, BUILTIN_DOT, BUILTIN_LENGTH, BUILTIN_DISTANCE,
    BUILTIN_NORMALIZE, BUILTIN_CROSS,
    BUILTIN_SUBGROUP_ADD, BUILTIN_SUBGROUP_MIN, BUILTIN_SUBGROUP_MAX, BUILTIN_SUBGROUP_MUL,
    BUILTIN_SUBGROUP_AND, BUILTIN_SUBGROUP_OR, BUILTIN_SUBGROUP_XOR,
    BUILTIN_SUBGROUP_EXCLUSIVE_ADD, BUILTIN_SUBGROUP_INCLUSIVE_ADD,
    BUILTIN_SUBGROUP_EXCLUSIVE_MUL, BUILTIN_SUBGROUP_INCLUSIVE_MUL,
    BUILTIN_TRANSPOSE, BUILTIN_DETERMINANT, BUILTIN_CEIL
} NumericBuiltinKind;
typedef struct { uint64_t bits; IrType type; } NumericValue;
typedef struct { TypeId type; uint32_t flags; uint64_t bits; } ConstantValue;
enum { CONSTANT_AGGREGATE = 1u };
typedef enum { UPDATE_SET, UPDATE_ADD, UPDATE_SUBTRACT, UPDATE_MULTIPLY,
    UPDATE_INCREMENT, UPDATE_DECREMENT, UPDATE_DIVIDE, UPDATE_REMAINDER,
    UPDATE_BIT_AND, UPDATE_BIT_OR, UPDATE_BIT_XOR, UPDATE_SHIFT_LEFT, UPDATE_SHIFT_RIGHT } UpdateKind;
enum { TARGET_ROOT, TARGET_INDEX, TARGET_MEMBER, TARGET_REFERENCE };
typedef struct { NameSpan root; uint32_t path, index, expression; UpdateKind operation; uint32_t kind; } Assignment;
typedef struct { NameSpan name; TypeRef annotation; uint32_t expression, flags, reserved; } LocalSyntax;
enum { LOCAL_ANNOTATED = 1u, LOCAL_MUTABLE = 2u, LOCAL_INITIALIZED = 4u, LOCAL_CONSTANT = 8u };
typedef enum {
    STATEMENT_ASSIGN, STATEMENT_RETURN, STATEMENT_IF, STATEMENT_LET,
    STATEMENT_VAR, STATEMENT_BLOCK, STATEMENT_FOR, STATEMENT_WHILE,
    STATEMENT_LOOP, STATEMENT_CONTINUING, STATEMENT_BREAK, STATEMENT_CONTINUE,
    STATEMENT_BREAK_IF, STATEMENT_EMPTY, STATEMENT_CONST, STATEMENT_CALL, STATEMENT_DISCARD
} StatementKind;
typedef struct {
    StatementKind kind; uint32_t source_offset;
    union { Assignment assignment; uint32_t condition; LocalSyntax local; } value;
    uint32_t body_start, else_start, end, initializer, update;
} Statement;
typedef struct {
    NameSpan name; TypeRef annotation; uint32_t first_expression, expression;
    union { uint64_t bits; struct { uint32_t scan, parent; } dependency; } value;
    TypeId type; uint32_t state;
} ConstantSyntax;
enum { FUNCTION_ENTRY = 1u, FUNCTION_MUST_USE = 2u, FUNCTION_VERTEX = 4u, FUNCTION_FRAGMENT = 8u,
    FUNCTION_POINTER_USE = 16u, FUNCTION_COLLECTIVE_USE = 32u };
typedef struct {
    NameSpan name; TypeRef annotation;
    union { uint32_t formal_start, entry_index; };
    uint32_t formal_count, statement_start, statement_end;
    uint32_t expression_start, expression_end, instruction_start, instruction_end;
    uint32_t local_start, local_count, result_type, flags, value_base;
    union { struct { uint32_t scan, parent, color; } graph;
        struct { uint32_t function, signature, label; } target; } state;
} FunctionRecord;
typedef struct { NameSpan name; union { TypeRef annotation;
    struct { TypeId type; uint32_t offset; } resolved; } type; } FormalRecord;
typedef struct { uint32_t expression, next; } ArgumentSyntax;
typedef struct { NameSpan name; TypeRef element; uint32_t count_expression, flags; } GenericSyntax;
enum { TYPE_VECTOR = 1u, TYPE_ARRAY, TYPE_RUNTIME_ARRAY, TYPE_STRUCT, TYPE_TEXTURE, TYPE_SAMPLER, TYPE_MATRIX, TYPE_ATOMIC, TYPE_ATOMIC_RESULT, TYPE_POINTER };
enum { TYPE_CONSTRUCTIBLE = 1u, TYPE_HOST = 2u, TYPE_FIXED = 4u, TYPE_TAIL = 8u,
    TYPE_RESOLVING = 16u, TYPE_RESOLVED = 32u, TYPE_ABSTRACT = 64u, TYPE_UNIFORM = 128u, TYPE_ATOMIC_CONTENT = 256u };
typedef struct { uint32_t dimension, format, access, flags; } ImageType;
typedef struct { uint32_t kind, flags, element, count; NameSpan name;
    union { struct { uint64_t size, alignment, stride; } layout;
        ImageType image;
        struct { uint32_t first_expression, end_expression, scan, parent, member, parent_kind; } dependency; } state;
    uint32_t target, aux; } TypeRecord;
typedef struct { NameSpan name; TypeRef annotation; uint32_t align_expression, size_expression;
    TypeId type; uint32_t flags; uint64_t offset, size, alignment; } MemberRecord;
typedef struct { NameSpan name; TypeRef annotation;
    uint32_t group_expression, binding_expression, group, binding, kind, access; TypeId type; uint32_t flags;
    uint64_t minimum, alignment, required_alignment, tail_offset, tail_stride;
    TypeId tail_type; uint32_t target, source_offset, reserved; } ResourceRecord;
typedef struct { NameSpan name; TypeRef annotation; TypeId type; uint32_t target; } WorkgroupRoot;
enum { CONSTANT_ZERO, CONSTANT_REPEAT, CONSTANT_COMPOSITE };
typedef struct { uint32_t kind; TypeId type; uint32_t first, count;
    uint64_t payload; uint32_t target, converted_from; } ConstantRecord;
enum { ACCESS_MEMBER, ACCESS_INDEX_U32, ACCESS_INDEX_I32, ACCESS_COMPONENT };
typedef struct { uint32_t parent, source_offset; TypeId input_type, output_type; uint32_t kind, index; } AccessStep;
typedef enum {
    IR_INPUT, IR_ROBUST_LOAD, IR_UNIFORM_LOAD, IR_CONSTANT, IR_ADD, IR_SUBTRACT,
    IR_MULTIPLY, IR_MAX, IR_COMPARE, IR_ROBUST_STORE, IR_IF, IR_ELSE, IR_END_IF, IR_RETURN,
    IR_NOT, IR_SHORT_BEGIN, IR_SHORT_END, IR_LOCAL_LOAD, IR_LOCAL_STORE,
    IR_LOOP_BEGIN, IR_LOOP_TEST, IR_LOOP_CONTINUING, IR_END_LOOP,
    IR_BREAK, IR_CONTINUE, IR_BREAK_IF, IR_NEGATE, IR_COMPLEMENT,
    IR_DIVIDE, IR_REMAINDER, IR_BIT_AND, IR_BIT_OR, IR_BIT_XOR,
    IR_SHIFT_LEFT, IR_SHIFT_RIGHT, IR_CONVERT, IR_BITCAST, IR_CALL, IR_BUILTIN,
    IR_COMPOSITE, IR_ACCESS_LOAD, IR_ACCESS_STORE, IR_VALUE_ACCESS, IR_SWIZZLE, IR_WORKGROUP_BARRIER, IR_HANDLE, IR_IMAGE, IR_STAGE_INPUT, IR_ARRAY_LENGTH, IR_DISCARD,
    IR_ATOMIC_LOAD, IR_ATOMIC_STORE, IR_ATOMIC_ADD, IR_ATOMIC_SUBTRACT, IR_ATOMIC_MAX,
    IR_ATOMIC_MIN, IR_ATOMIC_AND, IR_ATOMIC_OR, IR_ATOMIC_XOR, IR_ATOMIC_EXCHANGE, IR_ATOMIC_COMPARE_EXCHANGE, IR_ADDRESS
} IrOpcode;
typedef struct { uint32_t id; TypeId type; uint32_t source_offset; } IrValue;
/* Four words have operation-specific, named meanings. Scalar aliases preserve
 * the accepted instruction count; aggregate expansion belongs to the target. */
typedef struct {
    IrOpcode opcode; TypeId type; uint32_t result, source_offset;
    union {
        struct { uint32_t left, right, binding, literal; } scalar;
        struct { uint32_t root, path, value, category; } access;
        struct { uint32_t first, count, kind, reserved; } composite;
    } operands;
} IrInstruction;
typedef struct { TypeId type; uint32_t flags; } IrLocal;
typedef struct { TypeId type; uint32_t category, root, path; } PointerValue;
typedef struct { NameSpan name; uint32_t value; TypeId type;
    union { ConstantValue constant_value; PointerValue pointer_value; };
    uint32_t local_id, declaration_offset, flags, level; } LocalBinding;
typedef enum { IMAGE_LOAD, IMAGE_STORE, IMAGE_DIMENSIONS, IMAGE_LEVELS, IMAGE_LAYERS, IMAGE_SAMPLES, IMAGE_SAMPLE_LEVEL } ImageOperation;
enum { ROOT_LOCAL, ROOT_RESOURCE, ROOT_VALUE, ROOT_INPUT, ROOT_WORKGROUP, ROOT_POINTER };
typedef struct { uint32_t kind, id; TypeId type; uint32_t storage, first, count; uint64_t payload; } TargetDeclaration;
typedef struct { uint32_t kind, object, cursor, parent; } WalkItem;
typedef enum { INPUT_GLOBAL_INVOCATION, INPUT_LOCAL_INVOCATION, INPUT_WORKGROUP,
    INPUT_NUM_WORKGROUPS, INPUT_LOCAL_INDEX, INPUT_SUBGROUP_SIZE, INPUT_SUBGROUP_INVOCATION,
    INPUT_NUM_SUBGROUPS, INPUT_SUBGROUP_ID } InputKind;
typedef struct { NameSpan name; InputKind kind; uint32_t flags, target; } EntryInput;
typedef struct {
    uint32_t function, input_first, input_count, source_input_mask;
    union {
        Integer dimensions[3];
        struct {
            uint32_t workgroup_size[3], resource_first, resource_count;
            uint32_t workgroup_first, workgroup_count, local_index_target, target_input_mask;
            uint64_t workgroup_storage_bytes;
        } resolved;
    } state;
} EntryRecord;
typedef enum { SOURCE_NONE, SOURCE_FUNCTION, SOURCE_BARRIER, SOURCE_RESOURCE,
    SOURCE_WORKGROUP, SOURCE_INPUT, SOURCE_SUBGROUP, SOURCE_IMAGE, SOURCE_DISCARD, SOURCE_ATOMIC, SOURCE_POINTER_LOCAL, SOURCE_POINTER_FORMAL,
    SOURCE_POINTER_READ, SOURCE_POINTER_WRITE } SourceReferenceKind;
typedef struct { SourceReferenceKind kind; uint32_t index; } SourceReference;
typedef struct {
    uint32_t source_bytes, tokens, statements, expressions, physical_expressions, instructions;
    uint32_t types, members, resources, accesses, constants, edges, targets, generic_syntax, traversal;
    size_t words;
    uint32_t operands, expression_depth, control_depth, type_depth, flags;
    uint32_t entries, entry_inputs, entry_references;
} CompilerLimits;
enum { LIMIT_LEGACY = 1u, LIMIT_SINGULAR = 2u, LIMIT_MODULE = 4u };
enum { MODULE_SUBGROUPS = 1u };
typedef struct CompilerWorkspace CompilerWorkspace;
typedef struct StageWorkspace StageWorkspace;
typedef struct { const char *source; size_t length; DoeCompilerDiagnostic *diagnostic;
    const CompilerLimits *limits; CompilerWorkspace *compiler;
    uint32_t *exhausted_pool; size_t *minimum_pool_capacity; StageWorkspace *stages; } Source;
struct CompilerWorkspace {
    CompilerLimits limits;
    union { Token *tokens; IrInstruction *instructions; }; Expression *expressions; Statement *statements; ConstantSyntax *constant_decls;
    FunctionRecord *functions; FormalRecord *formals; ArgumentSyntax *arguments; GenericSyntax *generic_syntax;
    TypeRecord *types; MemberRecord *members; ResourceRecord *resources; ConstantRecord *constants;
    ConstantValue *constant_edges; AccessStep *accesses; IrLocal *locals;
    LocalBinding *scope; TargetDeclaration *targets; WalkItem *walk; uint32_t *values, *typed_operands, *words, *reflection_map;
    SourceReference *expression_references, *statement_references;
    EntryRecord *entries; EntryInput *entry_inputs; uint32_t *entry_references;
    uint32_t token_count, expression_count, expression_budget_count, statement_count, constant_decl_count;
    uint32_t function_count, formal_count, argument_count, generic_count, type_count, member_count;
    uint32_t resource_count, constant_count, edge_count, access_count, instruction_count, local_count;
    uint32_t operand_count, target_count, walk_count, current_function;
    uint32_t entry_count, entry_input_count, entry_reference_count;
    size_t word_count;
    NameSpan alias, alias_source;
    WorkgroupRoot *workgroups; unsigned char *uniformity;
    uint32_t workgroup_count, module_flags;
    unsigned char *statement_behaviors;
};
typedef CompilerWorkspace Syntax;
typedef CompilerWorkspace IrProgram;
typedef struct {
    CompilerWorkspace job;
    Expression expressions[337]; Statement statements[16]; ConstantSyntax constant_decls[321];
    FunctionRecord functions[341]; FormalRecord formals[512]; ArgumentSyntax arguments[320];
    GenericSyntax generic_syntax[32]; TypeRecord types[32]; MemberRecord members[64]; ResourceRecord resources[8];
    WorkgroupRoot workgroups[256];
    ConstantRecord constants[64]; ConstantValue constant_edges[128]; AccessStep accesses[337];
    EntryRecord entries[1]; EntryInput entry_inputs[5]; uint32_t entry_references[264];
    union { Token tokens[2049]; struct {
        IrInstruction instructions[385]; IrLocal locals[354]; LocalBinding scope[17];
        uint32_t values[898], typed_operands[320]; unsigned char statement_behaviors[16];
        union { struct { uint32_t reflection_map[39];
            TargetDeclaration targets[464]; WalkItem walk[97]; uint32_t words[4096];
        } target; struct {
            SourceReference expression_references[337], statement_references[16];
            union {
                struct {
                    unsigned char expression_bits[337], current_local[17], formal_summary[512], function_summary[341];
                    uint32_t metadata[30][8]; unsigned char frame_maps[30*6*17];
                } uniformity;
                struct { unsigned char marks[264]; uint32_t sorted[8]; } closure;
                WalkItem semantic_walk[97];
            } analysis;
        } references; uint64_t alignment; } scratch;
    } lowered; } phase;
} LegacyWorkspace;

bool source_error(Source *, DoeCompilerStatus, size_t, const char *);
bool source_pool_error(Source *, uint32_t, size_t, size_t, const char *);
bool compiler_job_layout(const DoeCompilerJobCapacities *, CompilerLimits *, size_t *);
bool compiler_module_layout(const DoeCompilerModuleCapacities *, CompilerLimits *, size_t *);
bool compiler_typed_layout(const DoeCompilerTypedCapacities *, CompilerLimits *, size_t *);
bool compiler_target_phase(Source *);
void compiler_job_bind(CompilerWorkspace *, void *, const CompilerLimits *);
void compiler_legacy_bind(LegacyWorkspace *, bool);
bool lex_source(Source *, CompilerWorkspace *);
bool parse_tokens(Source *, CompilerWorkspace *);
bool validate_syntax(Source *, CompilerWorkspace *);
bool module_closures(Source *);
bool input_scalar(InputKind);
bool input_subgroup(InputKind);
bool source_reference_record(Source *, SourceReference *, SourceReferenceKind, uint32_t, uint32_t);
bool emit_spirv(Source *, CompilerWorkspace *);
bool reflect_prepare(Source *, CompilerWorkspace *, DoeCompilerTypedOutputCounts *);
void reflect_module(const Source *, const CompilerWorkspace *, const DoeCompilerModuleOutputs *, uint32_t, DoeCompilerModuleV5 *);
void reflect_typed(const Source *, const CompilerWorkspace *, const DoeCompilerTypedOutputs *, DoeCompilerModuleV4 *);
bool reflect_version_admit(Source *, uint32_t);
bool reflect_legacy_admit(Source *, const CompilerWorkspace *, bool);
void reflect_program(const Source *, const CompilerWorkspace *, DoeCompilerReflection *);
void reflect_program_v2(const Source *, const CompilerWorkspace *, DoeCompilerReflectionV2 *);
NameSpan source_name_at(const Source *, uint32_t);
bool source_spans_equal(const Source *, NameSpan, NameSpan);
bool source_span_is(const Source *, NameSpan, const char *);
bool type_matrix(const CompilerWorkspace *, TypeId);
bool type_pointer(const CompilerWorkspace *, TypeId);
bool atomic_opcode(IrOpcode);
TypeId atomic_element(const CompilerWorkspace *, const IrInstruction *);
bool type_matrix_name(const Source *, NameSpan, uint32_t *, uint32_t *);
bool type_matrix_intern(Source *, TypeId, uint32_t, uint32_t, TypeId *);
TypeId type_leaf(const CompilerWorkspace *, TypeId);
uint32_t type_vector_width(const CompilerWorkspace *, TypeId);
bool type_abstract(const CompilerWorkspace *, TypeId);
bool type_constructible(const CompilerWorkspace *, TypeId);
bool type_handle(const CompilerWorkspace *, TypeId);
bool type_image_named(const Source *, NameSpan);
bool type_image_intern(Source *, uint32_t, TypeId, ImageType, TypeId *);
uint32_t image_coordinate_width(uint32_t);
bool image_arrayed(uint32_t);
bool image_format(const Source *, NameSpan, uint32_t *, TypeId *);
size_t reflect_handle_count(const CompilerWorkspace *);
bool reflect_buffers_only(Source *);
void reflect_handles(const CompilerWorkspace *, DoeCompilerHandleV6 *);
bool type_intern(Source *, uint32_t, TypeId, uint32_t, TypeId *);
uint64_t type_size(const CompilerWorkspace *, TypeId);
uint64_t type_alignment(const CompilerWorkspace *, TypeId);
bool type_layout_struct(Source *, TypeId);
bool type_resource_layout(Source *, ResourceRecord *);
uint32_t type_flags(const CompilerWorkspace *, TypeId);
uint32_t type_children(const CompilerWorkspace *, TypeId);
TypeId type_child(const CompilerWorkspace *, TypeId, uint32_t);
#endif
