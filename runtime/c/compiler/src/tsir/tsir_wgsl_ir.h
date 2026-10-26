/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_WGSL_IR_H
#define DOE_TSIR_WGSL_IR_H

#include "tsir_schema.h"

/* C mirror of the WGSL IR types the TSIR frontend actually reads
 * (runtime/zig/src/compiler/wgsl/ir/ir.zig, ir_query.zig,
 * layout_utils.zig). Only the node kinds the frontend walks are
 * modelled in full; type kinds the frontend never distinguishes
 * (samplers, textures, storage textures) collapse to
 * TSIR_IR_TYPE_OTHER, and a global of that kind gets the same
 * tsir_binding_type_unlowerable rejection Zig's `else => return
 * null` branch produces. All strings are caller-owned views; this
 * module never frees them and never copies them. Optional byte lengths follow
 * tsir_schema.h, including embedded-NUL and nonterminated spans. Index fields use
 * TSIR_IR_NONE (UINT32_MAX) for "absent", matching Zig's optionals.
 * Expression and statement trees are caller-built flat arrays plus
 * ranges, exactly like Zig's ArrayListUnmanaged stores; ids are
 * indices into those arrays. */

#define TSIR_IR_NONE ((uint32_t)0xffffffffu)

typedef enum TsirIrScalar {
    TSIR_IR_F32 = 0,
    TSIR_IR_F16 = 1,
    TSIR_IR_I32 = 2,
    TSIR_IR_U32 = 3,
    TSIR_IR_ABSTRACT_INT = 4,
    TSIR_IR_ABSTRACT_FLOAT = 5,
    TSIR_IR_BOOL = 6,
    TSIR_IR_VOID = 7
} TsirIrScalar;

typedef enum TsirIrTypeKind {
    TSIR_IR_TYPE_SCALAR = 0,
    TSIR_IR_TYPE_VECTOR = 1,
    TSIR_IR_TYPE_MATRIX = 2,
    TSIR_IR_TYPE_ARRAY = 3,
    TSIR_IR_TYPE_ATOMIC = 4,
    TSIR_IR_TYPE_STRUCT = 5,
    TSIR_IR_TYPE_REF = 6,
    TSIR_IR_TYPE_OTHER = 7
} TsirIrTypeKind;

typedef struct TsirIrType {
    TsirIrTypeKind kind;
    TsirIrScalar scalar;
    uint32_t elem;
    uint32_t len;
    uint32_t columns;
    uint32_t rows;
    bool has_len;
    uint32_t array_len;
    uint32_t struct_id;
    uint32_t ref_addr_space;
    uint32_t ref_access;
    /* Borrowed retained-IR tag for nonlowerable opaque types. NULL keeps the
     * historical generic OTHER tag for callers without source metadata. */
    const char *unlowerable_tag;
    size_t unlowerable_tag_length;
} TsirIrType;

typedef struct TsirIrStructField {
    const char *name;
    uint32_t ty;
    size_t name_length;
} TsirIrStructField;

typedef struct TsirIrStructDef {
    const char *name;
    const TsirIrStructField *fields;
    size_t field_count;
    size_t name_length;
} TsirIrStructDef;

typedef enum TsirIrAddrSpace {
    TSIR_IR_SPACE_FUNCTION = 0,
    TSIR_IR_SPACE_PRIVATE = 1,
    TSIR_IR_SPACE_WORKGROUP = 2,
    TSIR_IR_SPACE_UNIFORM = 3,
    TSIR_IR_SPACE_STORAGE = 4,
    TSIR_IR_SPACE_HANDLE = 5
} TsirIrAddrSpace;

typedef enum TsirIrAccessMode {
    TSIR_IR_ACCESS_READ = 0,
    TSIR_IR_ACCESS_WRITE = 1,
    TSIR_IR_ACCESS_READ_WRITE = 2
} TsirIrAccessMode;

typedef enum TsirIrGlobalClass {
    TSIR_IR_GLOBAL_VAR = 0,
    TSIR_IR_GLOBAL_CONST = 1,
    TSIR_IR_GLOBAL_OVERRIDE = 2,
    TSIR_IR_GLOBAL_INPUT = 3,
    TSIR_IR_GLOBAL_OUTPUT = 4
} TsirIrGlobalClass;

typedef struct TsirIrGlobal {
    const char *name;
    uint32_t ty;
    TsirIrGlobalClass class;
    bool has_addr_space;
    TsirIrAddrSpace addr_space;
    bool has_access;
    TsirIrAccessMode access;
    bool has_binding;
    uint32_t group;
    uint32_t binding;
    bool has_override_id;
    uint32_t override_id;
    size_t name_length;
} TsirIrGlobal;

typedef enum TsirIrBuiltin {
    TSIR_IR_BUILTIN_NONE = 0,
    TSIR_IR_BUILTIN_GLOBAL_INVOCATION_ID = 1,
    TSIR_IR_BUILTIN_OTHER = 2
} TsirIrBuiltin;

typedef struct TsirIrParam {
    const char *name;
    uint32_t ty;
    bool has_io;
    TsirIrBuiltin builtin;
    size_t name_length;
} TsirIrParam;

typedef struct TsirIrLocal {
    const char *name;
    uint32_t ty;
    bool mutable;
    size_t name_length;
} TsirIrLocal;

typedef enum TsirIrBinaryOp {
    TSIR_IR_BIN_ADD = 0,
    TSIR_IR_BIN_SUB = 1,
    TSIR_IR_BIN_MUL = 2,
    TSIR_IR_BIN_DIV = 3,
    TSIR_IR_BIN_REM = 4,
    TSIR_IR_BIN_BIT_AND = 5,
    TSIR_IR_BIN_BIT_OR = 6,
    TSIR_IR_BIN_BIT_XOR = 7,
    TSIR_IR_BIN_SHIFT_LEFT = 8,
    TSIR_IR_BIN_SHIFT_RIGHT = 9,
    TSIR_IR_BIN_EQUAL = 10,
    TSIR_IR_BIN_NOT_EQUAL = 11,
    TSIR_IR_BIN_LESS = 12,
    TSIR_IR_BIN_LESS_EQUAL = 13,
    TSIR_IR_BIN_GREATER = 14,
    TSIR_IR_BIN_GREATER_EQUAL = 15,
    TSIR_IR_BIN_LOGICAL_AND = 16,
    TSIR_IR_BIN_LOGICAL_OR = 17
} TsirIrBinaryOp;

typedef enum TsirIrUnaryOp {
    TSIR_IR_UN_NEG = 0,
    TSIR_IR_UN_NOT = 1,
    TSIR_IR_UN_BIT_NOT = 2
} TsirIrUnaryOp;

typedef enum TsirIrAssignOp {
    TSIR_IR_ASSIGN = 0,
    TSIR_IR_ASSIGN_ADD = 1,
    TSIR_IR_ASSIGN_SUB = 2,
    TSIR_IR_ASSIGN_MUL = 3,
    TSIR_IR_ASSIGN_DIV = 4,
    TSIR_IR_ASSIGN_REM = 5,
    TSIR_IR_ASSIGN_BIT_AND = 6,
    TSIR_IR_ASSIGN_BIT_OR = 7,
    TSIR_IR_ASSIGN_BIT_XOR = 8,
    TSIR_IR_ASSIGN_SHIFT_LEFT = 9,
    TSIR_IR_ASSIGN_SHIFT_RIGHT = 10
} TsirIrAssignOp;

typedef enum TsirIrExprKind {
    TSIR_IR_EXPR_BOOL_LIT = 0,
    TSIR_IR_EXPR_INT_LIT = 1,
    TSIR_IR_EXPR_FLOAT_LIT = 2,
    TSIR_IR_EXPR_PARAM_REF = 3,
    TSIR_IR_EXPR_LOCAL_REF = 4,
    TSIR_IR_EXPR_GLOBAL_REF = 5,
    TSIR_IR_EXPR_LOAD = 6,
    TSIR_IR_EXPR_ADDRESS_OF = 7,
    TSIR_IR_EXPR_DEREF = 8,
    TSIR_IR_EXPR_UNARY = 9,
    TSIR_IR_EXPR_BINARY = 10,
    TSIR_IR_EXPR_CALL = 11,
    TSIR_IR_EXPR_CONSTRUCT = 12,
    TSIR_IR_EXPR_MEMBER = 13,
    TSIR_IR_EXPR_INDEX = 14
} TsirIrExprKind;

typedef struct TsirIrExpr {
    TsirIrExprKind kind;
    uint32_t ty;
    bool bool_value;
    uint64_t int_value;
    double float_value;
    uint32_t ref_index;
    uint32_t inner;
    TsirIrUnaryOp unary_op;
    uint32_t unary_operand;
    TsirIrBinaryOp binary_op;
    uint32_t binary_lhs;
    uint32_t binary_rhs;
    const char *call_name;
    bool call_is_builtin;
    bool call_robustness_generated;
    uint32_t args_start;
    uint32_t args_len;
    const char *member_field_name;
    uint32_t member_field_index;
    uint32_t member_base;
    uint32_t index_base;
    uint32_t index_index;
    size_t call_name_length;
    size_t member_field_name_length;
} TsirIrExpr;

typedef enum TsirIrLoopKind {
    TSIR_IR_LOOP_BARE = 0,
    TSIR_IR_LOOP_WHILE = 1,
    TSIR_IR_LOOP_FOR = 2
} TsirIrLoopKind;

typedef enum TsirIrStmtKind {
    TSIR_IR_STMT_BLOCK = 0,
    TSIR_IR_STMT_LOCAL_DECL = 1,
    TSIR_IR_STMT_EXPR = 2,
    TSIR_IR_STMT_ASSIGN = 3,
    TSIR_IR_STMT_RETURN = 4,
    TSIR_IR_STMT_IF = 5,
    TSIR_IR_STMT_LOOP = 6,
    TSIR_IR_STMT_SWITCH = 7,
    TSIR_IR_STMT_BREAK = 8,
    TSIR_IR_STMT_CONTINUE = 9,
    TSIR_IR_STMT_DISCARD = 10
} TsirIrStmtKind;

typedef struct TsirIrStmt {
    TsirIrStmtKind kind;
    uint32_t range_start;
    uint32_t range_len;
    uint32_t local;
    bool has_initializer;
    uint32_t initializer;
    bool is_const;
    uint32_t expr;
    TsirIrAssignOp assign_op;
    uint32_t assign_lhs;
    uint32_t assign_rhs;
    bool has_return_expr;
    uint32_t return_expr;
    uint32_t if_cond;
    uint32_t if_then;
    bool has_if_else;
    uint32_t if_else;
    TsirIrLoopKind loop_kind;
    bool has_loop_init;
    uint32_t loop_init;
    bool has_loop_cond;
    uint32_t loop_cond;
    bool has_loop_continuing;
    uint32_t loop_continuing;
    uint32_t loop_body;
} TsirIrStmt;

typedef struct TsirIrFunction {
    const char *name;
    uint32_t return_type;
    const TsirIrParam *params;
    size_t param_count;
    const TsirIrLocal *locals;
    size_t local_count;
    const TsirIrExpr *exprs;
    size_t expr_count;
    const uint32_t *expr_args;
    size_t expr_arg_count;
    const TsirIrStmt *stmts;
    size_t stmt_count;
    const uint32_t *stmt_children;
    size_t stmt_child_count;
    uint32_t root_stmt;
    uint32_t workgroup_size[3];
    size_t name_length;
} TsirIrFunction;

typedef struct TsirWgslModule {
    const TsirIrType *types;
    size_t type_count;
    const TsirIrStructDef *structs;
    size_t struct_count;
    const TsirIrGlobal *globals;
    size_t global_count;
    const TsirIrFunction *functions;
    size_t function_count;
} TsirWgslModule;

/* layout_utils.zig: std430-compatible storage layout. Returns false
 * (not an error code) when a type id is out of range; callers treat
 * that as "not representable". Sizes/alignments saturate at UINT32
 * bounds only through checked arithmetic that returns false on
 * overflow. */
bool tsir_ir_type_is_scalar(const TsirWgslModule *module, uint32_t ty,
    TsirIrScalar scalar);
bool tsir_ir_type_size(const TsirWgslModule *module, uint32_t ty,
    uint32_t *out);
bool tsir_ir_type_alignment(const TsirWgslModule *module, uint32_t ty,
    uint32_t *out);
bool tsir_ir_struct_field_offset(const TsirWgslModule *module,
    const TsirIrStructDef *def, uint32_t field_index, uint32_t *out);

/* ir_query.zig, subset used by the frontend: walk global_ref /
 * index / member / load chains to the root global, resolve a value
 * alias through loads, single-arg constructs, and const local
 * initializers, and match an int literal through the same alias
 * resolution. All return false / TSIR_IR_NONE on out-of-range ids
 * instead of reading out of bounds. */
bool tsir_ir_find_global_base(const TsirIrFunction *func,
    uint32_t expr_id, uint32_t *out);
uint32_t tsir_ir_resolve_value_alias(const TsirIrFunction *func,
    uint32_t expr_id);
bool tsir_ir_match_int_literal(const TsirIrFunction *func,
    uint32_t expr_id, uint64_t *out);
bool tsir_ir_resolve_const_local(const TsirIrFunction *func,
    uint32_t local_index, uint32_t *out);
bool tsir_ir_unwrap_ref(const TsirWgslModule *module, uint32_t ty,
    uint32_t *out);

#endif
