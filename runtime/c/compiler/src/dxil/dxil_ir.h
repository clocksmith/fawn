/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_IR_H
#define DOE_DXIL_IR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dxil_error.h"

/* Doe IR subset for the DXIL emitters. Faithful port of the surface of
 * runtime/zig/src/compiler/wgsl/ir/ir.zig that emit_dxil_native.zig
 * reads, plus the ir_query.zig and layout_utils.zig helpers, in fixed
 * capacities (Zig uses growable lists). Every id is an index into its
 * table; DXIL_IR_INVALID_ID (UINT32_MAX) never names a valid entry.
 * All name strings are borrowed from the caller and must stay alive
 * while the module is walked, exactly as the DXIL builder borrows
 * names. The IR owns no heap storage: there is nothing to free, and
 * the Zig arena-versus-allocator distinction does not exist here
 * (see dxil_translate.h). Records are kept flat (no union) so a
 * partially built node can never be misread through the wrong member;
 * only the fields of the active kind are meaningful. */

#define DXIL_IR_INVALID_ID UINT32_MAX
#define DXIL_IR_MAX_TYPES 128u
#define DXIL_IR_MAX_STRUCTS 32u
#define DXIL_IR_MAX_STRUCT_FIELDS 16u
#define DXIL_IR_MAX_GLOBALS 128u
#define DXIL_IR_MAX_FUNCTIONS 16u
#define DXIL_IR_MAX_PARAMS 32u
#define DXIL_IR_MAX_LOCALS 64u
#define DXIL_IR_MAX_EXPRS 512u
#define DXIL_IR_MAX_EXPR_ARGS 512u
#define DXIL_IR_MAX_STMTS 256u
#define DXIL_IR_MAX_STMT_CHILDREN 256u
#define DXIL_IR_MAX_SWITCH_SELECTORS 128u
#define DXIL_IR_MAX_SWITCH_CASES 32u
#define DXIL_IR_MAX_ENTRY_POINTS 16u
#define DXIL_IR_MAX_CONSTANTS 1024u

typedef uint32_t DxilIrTypeId;
typedef uint32_t DxilIrExprId;
typedef uint32_t DxilIrStmtId;
typedef uint32_t DxilIrStructId;
typedef uint32_t DxilIrFunctionId;
typedef uint32_t DxilIrGlobalId;

typedef struct DxilIrRange {
    uint32_t start;
    uint32_t len;
} DxilIrRange;

typedef enum DxilIrAddressSpace {
    DXIL_IR_ADDR_FUNCTION,
    DXIL_IR_ADDR_PRIVATE,
    DXIL_IR_ADDR_WORKGROUP,
    DXIL_IR_ADDR_UNIFORM,
    DXIL_IR_ADDR_STORAGE,
    DXIL_IR_ADDR_HANDLE
} DxilIrAddressSpace;

typedef enum DxilIrAccessMode {
    DXIL_IR_ACCESS_READ,
    DXIL_IR_ACCESS_WRITE,
    DXIL_IR_ACCESS_READ_WRITE
} DxilIrAccessMode;

typedef enum DxilIrStage {
    DXIL_IR_STAGE_COMPUTE,
    DXIL_IR_STAGE_VERTEX,
    DXIL_IR_STAGE_FRAGMENT
} DxilIrStage;

typedef enum DxilIrBuiltin {
    DXIL_IR_BUILTIN_NONE,
    DXIL_IR_BUILTIN_POSITION,
    DXIL_IR_BUILTIN_FRAG_DEPTH,
    DXIL_IR_BUILTIN_FRONT_FACING,
    DXIL_IR_BUILTIN_GLOBAL_INVOCATION_ID,
    DXIL_IR_BUILTIN_LOCAL_INVOCATION_ID,
    DXIL_IR_BUILTIN_LOCAL_INVOCATION_INDEX,
    DXIL_IR_BUILTIN_WORKGROUP_ID,
    DXIL_IR_BUILTIN_NUM_WORKGROUPS,
    DXIL_IR_BUILTIN_SAMPLE_INDEX,
    DXIL_IR_BUILTIN_SAMPLE_MASK,
    DXIL_IR_BUILTIN_VERTEX_INDEX,
    DXIL_IR_BUILTIN_INSTANCE_INDEX,
    DXIL_IR_BUILTIN_SUBGROUP_SIZE,
    DXIL_IR_BUILTIN_SUBGROUP_INVOCATION_ID,
    DXIL_IR_BUILTIN_CLIP_DISTANCES,
    DXIL_IR_BUILTIN_PRIMITIVE_INDEX
} DxilIrBuiltin;

typedef enum DxilIrInterpolation {
    DXIL_IR_INTERP_PERSPECTIVE,
    DXIL_IR_INTERP_LINEAR,
    DXIL_IR_INTERP_FLAT
} DxilIrInterpolation;

typedef enum DxilIrInterpolationSampling {
    DXIL_IR_SAMPLING_CENTER,
    DXIL_IR_SAMPLING_CENTROID,
    DXIL_IR_SAMPLING_SAMPLE,
    DXIL_IR_SAMPLING_FIRST,
    DXIL_IR_SAMPLING_EITHER
} DxilIrInterpolationSampling;

typedef struct DxilIrIoAttr {
    DxilIrBuiltin builtin;
    bool has_location;
    uint32_t location;
    bool has_interpolation;
    DxilIrInterpolation interpolation;
    bool has_sampling;
    DxilIrInterpolationSampling sampling;
    bool invariant;
    bool has_blend_src;
    uint32_t blend_src;
} DxilIrIoAttr;

typedef struct DxilIrBindingPoint {
    uint32_t group;
    uint32_t binding;
} DxilIrBindingPoint;

typedef enum DxilIrGlobalClass {
    DXIL_IR_GLOBAL_VAR,
    DXIL_IR_GLOBAL_CONST,
    DXIL_IR_GLOBAL_OVERRIDE,
    DXIL_IR_GLOBAL_INPUT,
    DXIL_IR_GLOBAL_OUTPUT
} DxilIrGlobalClass;

typedef enum DxilIrScalar {
    DXIL_IR_SCALAR_VOID,
    DXIL_IR_SCALAR_BOOL,
    DXIL_IR_SCALAR_ABSTRACT_INT,
    DXIL_IR_SCALAR_ABSTRACT_FLOAT,
    DXIL_IR_SCALAR_I32,
    DXIL_IR_SCALAR_U32,
    DXIL_IR_SCALAR_F32,
    DXIL_IR_SCALAR_F16
} DxilIrScalar;

typedef enum DxilIrTextureFormat {
    DXIL_IR_TEXFMT_RGBA8UNORM,
    DXIL_IR_TEXFMT_RGBA8SNORM,
    DXIL_IR_TEXFMT_RGBA8UINT,
    DXIL_IR_TEXFMT_RGBA8SINT,
    DXIL_IR_TEXFMT_RGBA16UINT,
    DXIL_IR_TEXFMT_RGBA16SINT,
    DXIL_IR_TEXFMT_RGBA16FLOAT,
    DXIL_IR_TEXFMT_R32UINT,
    DXIL_IR_TEXFMT_R32SINT,
    DXIL_IR_TEXFMT_R32FLOAT,
    DXIL_IR_TEXFMT_RG32UINT,
    DXIL_IR_TEXFMT_RG32SINT,
    DXIL_IR_TEXFMT_RG32FLOAT,
    DXIL_IR_TEXFMT_RGBA32UINT,
    DXIL_IR_TEXFMT_RGBA32SINT,
    DXIL_IR_TEXFMT_RGBA32FLOAT
} DxilIrTextureFormat;

typedef enum DxilIrTypeKind {
    DXIL_IR_TYPE_SCALAR,
    DXIL_IR_TYPE_VECTOR,
    DXIL_IR_TYPE_MATRIX,
    DXIL_IR_TYPE_ARRAY,
    DXIL_IR_TYPE_ATOMIC,
    DXIL_IR_TYPE_STRUCT,
    DXIL_IR_TYPE_SAMPLER,
    DXIL_IR_TYPE_SAMPLER_COMPARISON,
    DXIL_IR_TYPE_TEXTURE_1D,
    DXIL_IR_TYPE_TEXTURE_2D,
    DXIL_IR_TYPE_TEXTURE_2D_ARRAY,
    DXIL_IR_TYPE_TEXTURE_CUBE,
    DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D,
    DXIL_IR_TYPE_TEXTURE_DEPTH_2D,
    DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE,
    DXIL_IR_TYPE_TEXTURE_3D,
    DXIL_IR_TYPE_STORAGE_TEXTURE_2D,
    DXIL_IR_TYPE_REF
} DxilIrTypeKind;

typedef struct DxilIrType {
    DxilIrTypeKind kind;
    DxilIrScalar scalar;
    DxilIrTypeId elem;
    uint8_t len;
    uint8_t columns;
    uint8_t rows;
    bool has_len;
    uint32_t array_len;
    DxilIrStructId struct_id;
    DxilIrTextureFormat format;
    DxilIrAccessMode access;
    DxilIrAddressSpace addr_space;
} DxilIrType;

typedef struct DxilIrStructField {
    const char *name;
    DxilIrTypeId ty;
    bool has_io;
    DxilIrIoAttr io;
} DxilIrStructField;

typedef struct DxilIrStructDef {
    const char *name;
    DxilIrStructField fields[DXIL_IR_MAX_STRUCT_FIELDS];
    uint32_t field_count;
} DxilIrStructDef;

typedef enum DxilIrConstKind {
    DXIL_IR_CONST_BOOL,
    DXIL_IR_CONST_INT,
    DXIL_IR_CONST_FLOAT
} DxilIrConstKind;

typedef struct DxilIrGlobal {
    const char *name;
    DxilIrTypeId ty;
    DxilIrGlobalClass class;
    bool has_addr_space;
    DxilIrAddressSpace addr_space;
    bool has_access;
    DxilIrAccessMode access;
    bool has_binding;
    DxilIrBindingPoint binding;
    bool has_io;
    DxilIrIoAttr io;
    bool has_initializer;
    DxilIrConstKind init_kind;
    bool init_bool;
    uint64_t init_int;
    double init_float;
    bool has_override_id;
    uint32_t override_id;
    bool has_init_tree;
    uint32_t init_tree;
} DxilIrGlobal;

/* Typed initializer tree for aggregate module constants. Scalar legacy fields
 * remain valid; a tree is explicit and never encoded as an invented zero. */
typedef struct DxilIrConstant {
    DxilIrTypeId ty;
    bool composite;
    DxilIrConstKind kind;
    bool bool_value;
    uint64_t int_value;
    double float_value;
    DxilIrRange children;
} DxilIrConstant;

typedef struct DxilIrParam {
    const char *name;
    DxilIrTypeId ty;
    bool has_io;
    DxilIrIoAttr io;
} DxilIrParam;

typedef struct DxilIrLocal {
    const char *name;
    DxilIrTypeId ty;
    bool mutable;
} DxilIrLocal;

typedef enum DxilIrExprCategory {
    DXIL_IR_EXPR_VALUE,
    DXIL_IR_EXPR_REF
} DxilIrExprCategory;

typedef enum DxilIrUnaryOp {
    DXIL_IR_UNARY_NEG,
    DXIL_IR_UNARY_NOT,
    DXIL_IR_UNARY_BIT_NOT
} DxilIrUnaryOp;

typedef enum DxilIrBinaryOp {
    DXIL_IR_BINARY_ADD,
    DXIL_IR_BINARY_SUB,
    DXIL_IR_BINARY_MUL,
    DXIL_IR_BINARY_DIV,
    DXIL_IR_BINARY_REM,
    DXIL_IR_BINARY_BIT_AND,
    DXIL_IR_BINARY_BIT_OR,
    DXIL_IR_BINARY_BIT_XOR,
    DXIL_IR_BINARY_SHIFT_LEFT,
    DXIL_IR_BINARY_SHIFT_RIGHT,
    DXIL_IR_BINARY_EQUAL,
    DXIL_IR_BINARY_NOT_EQUAL,
    DXIL_IR_BINARY_LESS,
    DXIL_IR_BINARY_LESS_EQUAL,
    DXIL_IR_BINARY_GREATER,
    DXIL_IR_BINARY_GREATER_EQUAL,
    DXIL_IR_BINARY_LOGICAL_AND,
    DXIL_IR_BINARY_LOGICAL_OR
} DxilIrBinaryOp;

typedef enum DxilIrAssignOp {
    DXIL_IR_ASSIGN_ASSIGN,
    DXIL_IR_ASSIGN_ADD,
    DXIL_IR_ASSIGN_SUB,
    DXIL_IR_ASSIGN_MUL,
    DXIL_IR_ASSIGN_DIV,
    DXIL_IR_ASSIGN_REM,
    DXIL_IR_ASSIGN_BIT_AND,
    DXIL_IR_ASSIGN_BIT_OR,
    DXIL_IR_ASSIGN_BIT_XOR,
    DXIL_IR_ASSIGN_SHIFT_LEFT,
    DXIL_IR_ASSIGN_SHIFT_RIGHT
} DxilIrAssignOp;

typedef enum DxilIrCallKind {
    DXIL_IR_CALL_USER,
    DXIL_IR_CALL_BUILTIN
} DxilIrCallKind;

typedef enum DxilIrExprKind {
    DXIL_IR_EXPR_BOOL_LIT,
    DXIL_IR_EXPR_INT_LIT,
    DXIL_IR_EXPR_FLOAT_LIT,
    DXIL_IR_EXPR_PARAM_REF,
    DXIL_IR_EXPR_LOCAL_REF,
    DXIL_IR_EXPR_GLOBAL_REF,
    DXIL_IR_EXPR_LOAD,
    DXIL_IR_EXPR_ADDRESS_OF,
    DXIL_IR_EXPR_DEREF,
    DXIL_IR_EXPR_UNARY,
    DXIL_IR_EXPR_BINARY,
    DXIL_IR_EXPR_CALL,
    DXIL_IR_EXPR_CONSTRUCT,
    DXIL_IR_EXPR_MEMBER,
    DXIL_IR_EXPR_INDEX
} DxilIrExprKind;

typedef struct DxilIrExpr {
    DxilIrExprKind kind;
    DxilIrTypeId ty;
    DxilIrExprCategory category;
    bool bool_value;
    uint64_t uint_value;
    double float_value;
    uint32_t index;
    DxilIrExprId inner;
    DxilIrUnaryOp unary_op;
    DxilIrExprId operand;
    DxilIrBinaryOp binary_op;
    DxilIrExprId lhs;
    DxilIrExprId rhs;
    const char *name;
    DxilIrCallKind call_kind;
    DxilIrRange args;
    bool robustness_generated;
    DxilIrTypeId construct_ty;
    DxilIrExprId base;
    const char *field_name;
    uint32_t field_index;
    DxilIrExprId index_expr;
} DxilIrExpr;

typedef enum DxilIrLoopKind {
    DXIL_IR_LOOP_LOOP,
    DXIL_IR_LOOP_WHILE,
    DXIL_IR_LOOP_FOR
} DxilIrLoopKind;

typedef struct DxilIrSwitchCase {
    DxilIrRange selectors;
    DxilIrStmtId body;
    bool is_default;
} DxilIrSwitchCase;

typedef enum DxilIrStmtKind {
    DXIL_IR_STMT_BLOCK,
    DXIL_IR_STMT_LOCAL_DECL,
    DXIL_IR_STMT_EXPR,
    DXIL_IR_STMT_ASSIGN,
    DXIL_IR_STMT_RETURN,
    DXIL_IR_STMT_IF,
    DXIL_IR_STMT_LOOP,
    DXIL_IR_STMT_SWITCH,
    DXIL_IR_STMT_BREAK,
    DXIL_IR_STMT_CONTINUE,
    DXIL_IR_STMT_DISCARD
} DxilIrStmtKind;

typedef struct DxilIrStmt {
    DxilIrStmtKind kind;
    DxilIrRange range;
    uint32_t local;
    bool has_initializer;
    DxilIrExprId initializer;
    bool is_const;
    DxilIrExprId expr;
    DxilIrAssignOp assign_op;
    DxilIrExprId assign_lhs;
    DxilIrExprId assign_rhs;
    bool has_return_value;
    DxilIrExprId return_value;
    DxilIrExprId cond;
    DxilIrStmtId then_block;
    bool has_else_block;
    DxilIrStmtId else_block;
    DxilIrLoopKind loop_kind;
    bool has_init;
    DxilIrStmtId init;
    bool has_cond;
    bool has_continuing;
    DxilIrStmtId continuing;
    DxilIrStmtId body;
    DxilIrExprId switch_expr;
    DxilIrRange cases;
} DxilIrStmt;

typedef struct DxilIrFunction {
    const char *name;
    DxilIrTypeId return_type;
    bool has_return_io;
    DxilIrIoAttr return_io;
    bool has_stage;
    DxilIrStage stage;
    uint32_t workgroup_size[3];
    DxilIrParam params[DXIL_IR_MAX_PARAMS];
    uint32_t param_count;
    DxilIrLocal locals[DXIL_IR_MAX_LOCALS];
    uint32_t local_count;
    DxilIrExpr exprs[DXIL_IR_MAX_EXPRS];
    uint32_t expr_count;
    DxilIrExprId expr_args[DXIL_IR_MAX_EXPR_ARGS];
    uint32_t expr_arg_count;
    DxilIrStmt stmts[DXIL_IR_MAX_STMTS];
    uint32_t stmt_count;
    DxilIrStmtId stmt_children[DXIL_IR_MAX_STMT_CHILDREN];
    uint32_t stmt_child_count;
    DxilIrExprId switch_selectors[DXIL_IR_MAX_SWITCH_SELECTORS];
    uint32_t switch_selector_count;
    DxilIrSwitchCase switch_cases[DXIL_IR_MAX_SWITCH_CASES];
    uint32_t switch_case_count;
    DxilIrStmtId root_stmt;
} DxilIrFunction;

typedef struct DxilIrEntryPoint {
    DxilIrFunctionId function;
    DxilIrStage stage;
    uint32_t workgroup_size[3];
} DxilIrEntryPoint;

typedef struct DxilIrModule {
    DxilIrType types[DXIL_IR_MAX_TYPES];
    uint32_t type_count;
    DxilIrStructDef structs[DXIL_IR_MAX_STRUCTS];
    uint32_t struct_count;
    DxilIrGlobal globals[DXIL_IR_MAX_GLOBALS];
    uint32_t global_count;
    DxilIrFunction functions[DXIL_IR_MAX_FUNCTIONS];
    uint32_t function_count;
    DxilIrEntryPoint entry_points[DXIL_IR_MAX_ENTRY_POINTS];
    uint32_t entry_point_count;
    DxilIrConstant constants[DXIL_IR_MAX_CONSTANTS];
    uint32_t constant_count;
    uint32_t constant_children[DXIL_IR_MAX_CONSTANTS];
    uint32_t constant_child_count;
} DxilIrModule;

void dxil_ir_init(DxilIrModule *module);

/* Bounded structural validation of caller-built tables, identities, ranges,
 * names and type/constant graph cycles. This is not WGSL semantic admission. */
DxilStatus dxil_ir_validate(const DxilIrModule *module);

/* Interns a type with Zig type_eql dedup; returns the existing id for
 * an equal type. Fails with DXIL_OUTPUT_TOO_LARGE at 128 types. */
DxilStatus dxil_ir_intern_type(DxilIrModule *module,
    const DxilIrType *type, DxilIrTypeId *out_id);

/* Copies the type at id into *out. DXIL_IR_INVALID_ID or an
 * out-of-range id fails with DXIL_INVALID_IR. */
DxilStatus dxil_ir_type_get(const DxilIrModule *module, DxilIrTypeId id,
    DxilIrType *out);

/* Appends an empty struct definition; fields are added with
 * dxil_ir_add_struct_field. */
DxilStatus dxil_ir_add_struct(DxilIrModule *module, const char *name,
    DxilIrStructId *out_id);
DxilStatus dxil_ir_add_struct_field(DxilIrModule *module,
    DxilIrStructId struct_id, const DxilIrStructField *field);

/* Appends a global, function slot, or entry point by value copy of
 * the header record (function bodies are built afterwards through
 * the returned function pointer; the module array never moves, so
 * the pointer stays valid). */
DxilStatus dxil_ir_add_global(DxilIrModule *module,
    const DxilIrGlobal *global, DxilIrGlobalId *out_id);
DxilStatus dxil_ir_add_function(DxilIrModule *module, const char *name,
    DxilIrTypeId return_type, DxilIrFunction **out_function);
DxilStatus dxil_ir_add_entry_point(DxilIrModule *module,
    const DxilIrEntryPoint *entry);

/* Function-body append helpers. Each fails with DXIL_OUTPUT_TOO_LARGE
 * at its table capacity. */
DxilStatus dxil_ir_append_expr(DxilIrFunction *function,
    const DxilIrExpr *expr, DxilIrExprId *out_id);
DxilStatus dxil_ir_append_stmt(DxilIrFunction *function,
    const DxilIrStmt *stmt, DxilIrStmtId *out_id);
DxilStatus dxil_ir_append_expr_arg(DxilIrFunction *function,
    DxilIrExprId arg);
DxilStatus dxil_ir_append_stmt_child(DxilIrFunction *function,
    DxilIrStmtId child);
DxilStatus dxil_ir_append_selector(DxilIrFunction *function,
    DxilIrExprId selector);
DxilStatus dxil_ir_append_switch_case(DxilIrFunction *function,
    const DxilIrSwitchCase *switch_case);

/* The native emitter's own type predicates (emit_dxil_native.zig).
 * An invalid type id is not float and not signed. */
bool dxil_ir_is_float_type(const DxilIrModule *module, DxilIrTypeId ty);
bool dxil_ir_is_signed_type(const DxilIrModule *module, DxilIrTypeId ty);

/* Storage layout (layout_utils.zig, std430-compatible). An invalid
 * type id behaves like Zig's else branch: alignment 4, size 4. */
uint32_t dxil_ir_type_alignment(const DxilIrModule *module,
    DxilIrTypeId ty);
uint32_t dxil_ir_type_size(const DxilIrModule *module, DxilIrTypeId ty);
uint32_t dxil_ir_round_up(uint32_t value, uint32_t alignment);
DxilStatus dxil_ir_struct_field_offset(const DxilIrModule *module,
    DxilIrStructId struct_id, uint32_t target_field, uint32_t *out);
DxilStatus dxil_ir_minimum_binding_size(const DxilIrModule *module,
    DxilIrTypeId ty, uint32_t *out);

/* Queries (ir_query.zig and layout_utils.resolve_member_global).
 * Found-results use a bool plus an out value; containment checks
 * return false for out-of-range ids and never fail. Recursion is
 * bounded: a cyclic hand-built chain stops after one pass over the
 * table instead of looping (see gaps.md). */
bool dxil_ir_is_compute_only_module(const DxilIrModule *module);
bool dxil_ir_reference_root_local(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint32_t *out_local);
bool dxil_ir_parameter_is_assigned(const DxilIrFunction *function,
    uint32_t param_index);
DxilIrExprId dxil_ir_resolve_value_alias(
    const DxilIrFunction *function, DxilIrExprId expr_id);
bool dxil_ir_resolve_const_local_initializer(
    const DxilIrFunction *function, uint32_t local_idx,
    DxilIrExprId *out_expr);
DxilIrTypeId dxil_ir_resolve_indexable_type(
    const DxilIrModule *module, DxilIrTypeId ty);
bool dxil_ir_classify_builtin_component(const DxilIrFunction *function,
    DxilIrExprId expr_id, DxilIrBuiltin builtin, uint8_t *out_component);
bool dxil_ir_match_int_literal(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint64_t *out_value);
bool dxil_ir_matches_int_literal(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint64_t expected);
bool dxil_ir_type_has_io_struct_field(const DxilIrModule *module,
    DxilIrTypeId ty);
bool dxil_ir_expr_is_texture_1d(const DxilIrModule *module,
    const DxilIrFunction *function, DxilIrExprId expr_id);
bool dxil_ir_resolve_runtime_array_element_stride(
    const DxilIrModule *module, const DxilIrFunction *function,
    DxilIrExprId base_id, uint64_t *out_stride);
bool dxil_ir_find_global_base(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint32_t *out_global);
bool dxil_ir_resolve_member_global(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint32_t *out_global);
bool dxil_ir_is_local_ref(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint32_t local_idx);
bool dxil_ir_stmt_writes_local(const DxilIrFunction *function,
    DxilIrStmtId stmt_id, uint32_t local_idx);
bool dxil_ir_stmt_contains_expr(const DxilIrFunction *function,
    DxilIrStmtId stmt_id, DxilIrExprId target_expr_id);
bool dxil_ir_expr_contains_expr(const DxilIrFunction *function,
    DxilIrExprId expr_id, DxilIrExprId target_expr_id);
bool dxil_ir_runtime_array_write_guard(const DxilIrFunction *function,
    DxilIrExprId lhs_expr_id, DxilIrExprId *out_unclamped_index,
    DxilIrExprId *out_array_length);

#endif /* DOE_DXIL_IR_H */
