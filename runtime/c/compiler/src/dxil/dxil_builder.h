/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_BUILDER_H
#define DOE_DXIL_BUILDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dxil_error.h"

/* DXIL module builder. Faithful port of dxil_builder.zig: fixed-capacity
 * tables for types, functions, constants, metadata, named metadata, and
 * the value symbol table, plus function bodies. No allocation happens
 * here: the builder owns its tables by value, and every name string is
 * borrowed from the caller (the caller must keep names alive until
 * serialization finishes, exactly as Zig borrows the slices).
 *
 * Value-id sequencing matches Zig: functions and constants share one
 * monotonically increasing counter starting at 0, assigned in insertion
 * order; a function body's next_local_value starts at the counter value
 * at body creation. */

#define DXIL_BUILDER_MAX_TYPES 128u
#define DXIL_BUILDER_MAX_VALUES 512u
#define DXIL_BUILDER_MAX_METADATA 256u
#define DXIL_BUILDER_MAX_FUNCTIONS 32u
#define DXIL_BUILDER_MAX_NAMED_MD 32u
#define DXIL_BUILDER_MAX_SYMTAB_ENTRIES 64u
#define DXIL_BUILDER_MAX_BODY_OPS 1024u
#define DXIL_BUILDER_INVALID_TYPE UINT32_MAX

typedef uint32_t DxilTypeIndex;
typedef uint32_t DxilValueIndex;
typedef uint32_t DxilMdIndex;

typedef enum DxilTypeKind {
    DXIL_TKIND_VOID,
    DXIL_TKIND_I1,
    DXIL_TKIND_I8,
    DXIL_TKIND_I16,
    DXIL_TKIND_I32,
    DXIL_TKIND_I64,
    DXIL_TKIND_F16,
    DXIL_TKIND_F32,
    DXIL_TKIND_F64,
    DXIL_TKIND_POINTER,
    DXIL_TKIND_FUNCTION,
    DXIL_TKIND_STRUCT,
    DXIL_TKIND_ARRAY,
    DXIL_TKIND_VECTOR,
    DXIL_TKIND_METADATA,
    DXIL_TKIND_LABEL
} DxilTypeKind;

/* One LLVM type. Only the fields of the active kind are meaningful, but
 * the record is kept flat (no union) so a partially built type can never
 * be misread through the wrong member. */
typedef struct DxilType {
    DxilTypeKind kind;
    DxilTypeIndex pointee;
    DxilTypeIndex return_type;
    DxilTypeIndex params[8];
    uint8_t param_count;
    bool is_vararg;
    DxilTypeIndex members[16];
    uint8_t member_count;
    const char *name;
    DxilTypeIndex elem;
    uint64_t len;
} DxilType;

typedef struct DxilFuncDecl {
    const char *name;
    DxilTypeIndex type_index;
    uint32_t linkage;
    DxilValueIndex value_index;
    bool is_definition;
} DxilFuncDecl;

typedef enum DxilConstKind {
    DXIL_CONST_INT,
    DXIL_CONST_UINT,
    DXIL_CONST_FLOAT,
    DXIL_CONST_NULL,
    DXIL_CONST_UNDEF
} DxilConstKind;

typedef struct DxilConstantEntry {
    DxilTypeIndex type_index;
    DxilConstKind kind;
    int64_t int_value;
    uint64_t uint_value;
    uint64_t float_bits;
} DxilConstantEntry;

typedef enum DxilMdKind {
    DXIL_MD_STRING,
    DXIL_MD_VALUE,
    DXIL_MD_NODE
} DxilMdKind;

typedef struct DxilMdNode {
    DxilMdKind kind;
    const char *text;
    DxilTypeIndex type_index;
    DxilValueIndex value_index;
    bool operand_present[16];
    DxilMdIndex operands[16];
    uint8_t count;
} DxilMdNode;

typedef struct DxilNamedMd {
    const char *name;
    DxilMdIndex node_indices[16];
    uint8_t count;
} DxilNamedMd;

typedef struct DxilSymtabEntry {
    DxilValueIndex value_index;
    const char *name;
} DxilSymtabEntry;

typedef struct DxilBuilder {
    DxilType types[DXIL_BUILDER_MAX_TYPES];
    uint32_t type_count;
    DxilFuncDecl functions[DXIL_BUILDER_MAX_FUNCTIONS];
    uint32_t function_count;
    DxilConstantEntry constants[DXIL_BUILDER_MAX_VALUES];
    uint32_t constant_count;
    DxilMdNode metadata[DXIL_BUILDER_MAX_METADATA];
    uint32_t md_count;
    DxilNamedMd named_md[DXIL_BUILDER_MAX_NAMED_MD];
    uint32_t named_md_count;
    DxilSymtabEntry symtab[DXIL_BUILDER_MAX_SYMTAB_ENTRIES];
    uint32_t symtab_count;
    DxilValueIndex next_value_id;
} DxilBuilder;

void dxil_builder_init(DxilBuilder *builder);

/* Appends a type unconditionally. Fails with DXIL_OUTPUT_TOO_LARGE at
 * 128 types. */
DxilStatus dxil_builder_add_type(DxilBuilder *builder, const DxilType *type,
    DxilTypeIndex *out_index);

/* Returns the existing index when a structurally equal type is already
 * present, else appends. Struct types are never equal to anything, so
 * they always append (Zig's type_eql returns false for structs). */
DxilStatus dxil_builder_find_or_add_type(DxilBuilder *builder,
    const DxilType *type, DxilTypeIndex *out_index);

DxilStatus dxil_builder_type_void(DxilBuilder *builder, DxilTypeIndex *out);
DxilStatus dxil_builder_type_i1(DxilBuilder *builder, DxilTypeIndex *out);
DxilStatus dxil_builder_type_i8(DxilBuilder *builder, DxilTypeIndex *out);
/* Note: Zig has no type_i16 helper; i16 is reachable only through
 * dxil_builder_add_type with kind DXIL_TKIND_I16. */
DxilStatus dxil_builder_type_i32(DxilBuilder *builder, DxilTypeIndex *out);
DxilStatus dxil_builder_type_i64(DxilBuilder *builder, DxilTypeIndex *out);
DxilStatus dxil_builder_type_f16(DxilBuilder *builder, DxilTypeIndex *out);
DxilStatus dxil_builder_type_f32(DxilBuilder *builder, DxilTypeIndex *out);
DxilStatus dxil_builder_type_pointer(DxilBuilder *builder,
    DxilTypeIndex pointee, DxilTypeIndex *out);
DxilStatus dxil_builder_type_function(DxilBuilder *builder,
    DxilTypeIndex return_type, const DxilTypeIndex *params,
    size_t param_count, bool is_vararg, DxilTypeIndex *out);
DxilStatus dxil_builder_type_struct(DxilBuilder *builder,
    const DxilTypeIndex *members, size_t member_count, const char *name,
    DxilTypeIndex *out);
DxilStatus dxil_builder_type_array(DxilBuilder *builder, DxilTypeIndex elem,
    uint64_t len, DxilTypeIndex *out);
DxilStatus dxil_builder_type_vector(DxilBuilder *builder, DxilTypeIndex elem,
    uint32_t len, DxilTypeIndex *out);
DxilStatus dxil_builder_type_metadata(DxilBuilder *builder,
    DxilTypeIndex *out);
DxilStatus dxil_builder_type_label(DxilBuilder *builder, DxilTypeIndex *out);

/* Adds a function declaration; assigns its value_index from the shared
 * counter and returns the function index. Fails with
 * DXIL_OUTPUT_TOO_LARGE at 32 functions. */
DxilStatus dxil_builder_add_function(DxilBuilder *builder, const char *name,
    DxilTypeIndex type_index, uint32_t linkage, bool is_definition,
    uint32_t *out_index);

/* Adds a constant; assigns its value index from the shared counter.
 * Fails with DXIL_OUTPUT_TOO_LARGE at 512 constants. */
DxilStatus dxil_builder_add_constant(DxilBuilder *builder,
    DxilTypeIndex type_index, const DxilConstantEntry *value,
    DxilValueIndex *out_value);

DxilStatus dxil_builder_add_const_i32(DxilBuilder *builder, int32_t value,
    DxilValueIndex *out_value);
/* Uses the i32 type, exactly as Zig does (there is no u32 LLVM type). */
DxilStatus dxil_builder_add_const_u32(DxilBuilder *builder, uint32_t value,
    DxilValueIndex *out_value);
DxilStatus dxil_builder_add_const_f32(DxilBuilder *builder, float value,
    DxilValueIndex *out_value);

DxilStatus dxil_builder_add_metadata_string(DxilBuilder *builder,
    const char *text, DxilMdIndex *out_index);
DxilStatus dxil_builder_add_metadata_value(DxilBuilder *builder,
    DxilTypeIndex type_index, DxilValueIndex value_index,
    DxilMdIndex *out_index);
DxilStatus dxil_builder_add_metadata_node(DxilBuilder *builder,
    const bool *present, const DxilMdIndex *operands, size_t operand_count,
    DxilMdIndex *out_index);
DxilStatus dxil_builder_add_named_metadata(DxilBuilder *builder,
    const char *name, const DxilMdIndex *nodes, size_t node_count);
DxilStatus dxil_builder_add_symtab_entry(DxilBuilder *builder,
    DxilValueIndex value_index, const char *name);

/* Function body operations. One flat record per op; only the fields of
 * the active kind are meaningful. */
typedef enum DxilBodyOpKind {
    DXIL_OP_RET_VOID,
    DXIL_OP_RET_VALUE,
    DXIL_OP_CALL,
    DXIL_OP_BINOP,
    DXIL_OP_CAST,
    DXIL_OP_LOAD,
    DXIL_OP_STORE,
    DXIL_OP_ALLOCA,
    DXIL_OP_GEP,
    DXIL_OP_EXTRACT_VALUE,
    DXIL_OP_INSERT_VALUE,
    DXIL_OP_CMP,
    DXIL_OP_BR,
    DXIL_OP_BR_COND,
    DXIL_OP_PHI,
    DXIL_OP_SWITCH
} DxilBodyOpKind;

typedef struct DxilPhiIncoming {
    DxilValueIndex value;
    uint32_t block;
} DxilPhiIncoming;

typedef struct DxilSwitchCase {
    DxilValueIndex value;
    uint32_t block;
} DxilSwitchCase;

typedef struct DxilBodyOp {
    DxilBodyOpKind kind;
    DxilValueIndex value;
    DxilTypeIndex result_type;
    DxilValueIndex callee;
    DxilValueIndex args[8];
    uint8_t arg_count;
    DxilValueIndex lhs;
    DxilValueIndex rhs;
    uint32_t opcode;
    DxilValueIndex operand;
    DxilTypeIndex dest_type;
    DxilValueIndex ptr;
    DxilTypeIndex type_index;
    uint32_t align_log2;
    DxilValueIndex count_value;
    bool inbounds;
    DxilTypeIndex base_type;
    DxilValueIndex indices[4];
    uint8_t index_count;
    DxilValueIndex aggregate;
    uint32_t index;
    uint32_t predicate;
    uint32_t target;
    DxilValueIndex cond;
    uint32_t true_block;
    uint32_t false_block;
    DxilPhiIncoming incoming[8];
    uint8_t incoming_count;
    uint32_t default_block;
    DxilSwitchCase cases[16];
    uint8_t case_count;
} DxilBodyOp;

typedef struct DxilFunctionBody {
    uint32_t func_index;
    uint32_t num_blocks;
    DxilBodyOp ops[DXIL_BUILDER_MAX_BODY_OPS];
    DxilTypeIndex op_types[DXIL_BUILDER_MAX_BODY_OPS];
    uint32_t op_count;
    DxilValueIndex next_local_value;
} DxilFunctionBody;

/* Starts a body for an existing function; its next_local_value is the
 * builder's current counter. An out-of-range function index fails with
 * DXIL_INVALID_IR (Zig would panic here). */
DxilStatus dxil_builder_create_function_body(DxilBuilder *builder,
    uint32_t func_index, DxilFunctionBody *body);

/* Appends an op, returns the pre-increment next_local_value, then
 * increments it. Fails with DXIL_OUTPUT_TOO_LARGE at 1024 ops. */
DxilStatus dxil_body_add_op(DxilFunctionBody *body, const DxilBodyOp *op,
    DxilTypeIndex result_type, DxilValueIndex *out_value);

/* Appends an op with result type INVALID_TYPE and does NOT advance
 * next_local_value. Fails with DXIL_OUTPUT_TOO_LARGE at 1024 ops. */
DxilStatus dxil_body_add_void_op(DxilFunctionBody *body,
    const DxilBodyOp *op);

#endif /* DOE_DXIL_BUILDER_H */
