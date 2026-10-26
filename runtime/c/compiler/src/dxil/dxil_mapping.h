/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_MAPPING_H
#define DOE_DXIL_MAPPING_H

#include <stdbool.h>
#include <stdint.h>

#include "dxil_spec.h"

/* Decision tables from emit_dxil_native.zig, ported as pure functions
 * over scalar inputs. The IR walk that feeds these tables (statements,
 * expressions, resource collection over a Doe IR module) is NOT ported
 * here: there is no C Doe IR in this directory, and inventing one
 * would be a stub, not a port. See gaps.md. Every table row below is
 * directly observable from the Zig source and is covered by tests. */

#define DXIL_MAX_OUTPUT (256u * 1024u)
#define DXIL_NATIVE_MAX_BITCODE_SIZE (192u * 1024u)
#define DXIL_NATIVE_MAX_DXIL_PART_SIZE (200u * 1024u)
#define DXIL_NATIVE_MAX_GLOBALS 64u

/* DXIL metadata values emitted by the native emitter. */
#define DXIL_MD_DX_VERSION_MAJOR 1u
#define DXIL_MD_DX_VERSION_MINOR 0u
#define DXIL_MD_DX_VALVER_MAJOR 1u
#define DXIL_MD_DX_VALVER_MINOR 7u
#define DXIL_MD_SHADER_MODEL_MAJOR 6u
#define DXIL_MD_SHADER_MODEL_MINOR 0u

typedef enum DxilStage {
    DXIL_STAGE_COMPUTE,
    DXIL_STAGE_VERTEX,
    DXIL_STAGE_FRAGMENT
} DxilStage;

/* Stage -> shader kind in the DXIL program header. */
uint32_t dxil_stage_shader_kind(DxilStage stage);

/* Stage -> shader-model metadata string and DXC profile prefix:
 * compute "cs", vertex "vs", fragment "ps". */
const char *dxil_stage_prefix(DxilStage stage);

/* Parameter builtins relevant to DXIL emission. OTHER covers every
 * builtin the Zig walk ignores for the given stage. */
typedef enum DxilParamBuiltin {
    DXIL_BUILTIN_GLOBAL_INVOCATION_ID,
    DXIL_BUILTIN_LOCAL_INVOCATION_ID,
    DXIL_BUILTIN_WORKGROUP_ID,
    DXIL_BUILTIN_LOCAL_INVOCATION_INDEX,
    DXIL_BUILTIN_VERTEX_INDEX,
    DXIL_BUILTIN_INSTANCE_INDEX,
    DXIL_BUILTIN_POSITION,
    DXIL_BUILTIN_FRONT_FACING,
    DXIL_BUILTIN_OTHER
} DxilParamBuiltin;

/* Compute entry point: which dx.op opcode (if any) a parameter builtin
 * loads, and how many dx.op calls it produces (3 component calls, or 1
 * for local_invocation_index, or 0 for an ignored builtin). */
bool dxil_compute_param_opcode(DxilParamBuiltin builtin, uint32_t *opcode,
    uint32_t *call_count);

/* Vertex entry point: vertex_index and instance_index each produce one
 * LOAD_INPUT call; everything else produces none. */
bool dxil_vertex_param_loads_input(DxilParamBuiltin builtin);

/* Fragment entry point: a parameter with a location, or with the
 * position or front_facing builtin, produces one LOAD_INPUT call. */
bool dxil_fragment_param_loads_input(bool has_location,
    DxilParamBuiltin builtin);

/* How a WGSL builtin call maps to a dx.op call. */
typedef enum DxilBuiltinArgMode {
    /* No extra argument (subgroupElect). */
    DXIL_BUILTIN_ARG_NONE,
    /* The first argument, when present, else no argument
     * (subgroupAll, subgroupAny, subgroupBallot). */
    DXIL_BUILTIN_ARG_FIRST_IF_PRESENT,
    /* A constant mode argument (barriers). */
    DXIL_BUILTIN_ARG_MODE_CONSTANT,
    /* The first argument plus the WaveOp SUM constant (subgroupAdd). */
    DXIL_BUILTIN_ARG_FIRST_PLUS_WAVE_SUM
} DxilBuiltinArgMode;

typedef struct DxilBuiltinCall {
    uint32_t opcode;
    DxilBuiltinArgMode arg_mode;
    /* Meaningful only for DXIL_BUILTIN_ARG_MODE_CONSTANT. */
    uint32_t mode_value;
    /* False only for builtins Zig does not recognize: their arguments
     * are still evaluated and the result is constant 0. */
    bool recognized;
} DxilBuiltinCall;

/* Maps a builtin name to its dx.op call. Never fails: an unrecognized
 * name yields recognized=false, opcode 0, arg mode NONE. */
void dxil_builtin_call(const char *name, DxilBuiltinCall *out);

/* Unary operator lowering. */
typedef enum DxilUnaryOp {
    DXIL_UNARY_NEG,
    DXIL_UNARY_NOT,
    DXIL_UNARY_BIT_NOT
} DxilUnaryOp;

typedef enum DxilUnaryForm {
    /* Result is `0 - operand`. */
    DXIL_UNARY_ZERO_MINUS,
    /* Result is `operand ^ -1` (all-ones i32 constant). */
    DXIL_UNARY_XOR_ALL_ONES
} DxilUnaryForm;

void dxil_unary_opcode(DxilUnaryOp op, bool is_float, uint32_t *opcode,
    DxilUnaryForm *form);

/* Binary operator lowering. Comparisons set *is_comparison and return
 * the comparison predicate in *opcode; all other ops return the binop
 * opcode and leave *is_comparison false. */
typedef enum DxilBinaryOp {
    DXIL_BINARY_ADD,
    DXIL_BINARY_SUB,
    DXIL_BINARY_MUL,
    DXIL_BINARY_DIV,
    DXIL_BINARY_REM,
    DXIL_BINARY_BIT_AND,
    DXIL_BINARY_BIT_OR,
    DXIL_BINARY_BIT_XOR,
    DXIL_BINARY_SHIFT_LEFT,
    DXIL_BINARY_SHIFT_RIGHT,
    DXIL_BINARY_EQUAL,
    DXIL_BINARY_NOT_EQUAL,
    DXIL_BINARY_LESS,
    DXIL_BINARY_LESS_EQUAL,
    DXIL_BINARY_GREATER,
    DXIL_BINARY_GREATER_EQUAL,
    DXIL_BINARY_LOGICAL_AND,
    DXIL_BINARY_LOGICAL_OR
} DxilBinaryOp;

void dxil_binary_opcode(DxilBinaryOp op, bool is_float, bool is_signed,
    bool *is_comparison, uint32_t *opcode);

/* Resource classification inputs (the IR-free form of classify_resource
 * and classify_storage_kind). */
typedef enum DxilAddrSpace {
    DXIL_ADDR_UNIFORM,
    DXIL_ADDR_STORAGE,
    DXIL_ADDR_HANDLE,
    DXIL_ADDR_OTHER
} DxilAddrSpace;

typedef enum DxilAccess {
    /* A missing access on a storage global defaults to read_write. */
    DXIL_ACCESS_NONE,
    DXIL_ACCESS_READ,
    DXIL_ACCESS_READ_WRITE,
    DXIL_ACCESS_WRITE
} DxilAccess;

typedef enum DxilResourceTypeKind {
    DXIL_RESTYPE_ARRAY,
    DXIL_RESTYPE_STRUCT,
    DXIL_RESTYPE_OTHER,
    DXIL_RESTYPE_SAMPLER,
    DXIL_RESTYPE_SAMPLER_COMPARISON,
    DXIL_RESTYPE_TEXTURE_1D,
    DXIL_RESTYPE_TEXTURE_2D,
    DXIL_RESTYPE_TEXTURE_2D_ARRAY,
    DXIL_RESTYPE_TEXTURE_DEPTH_2D,
    DXIL_RESTYPE_TEXTURE_3D,
    DXIL_RESTYPE_TEXTURE_CUBE,
    DXIL_RESTYPE_TEXTURE_DEPTH_CUBE,
    DXIL_RESTYPE_TEXTURE_MULTISAMPLED_2D,
    DXIL_RESTYPE_STORAGE_TEXTURE_2D
} DxilResourceTypeKind;

/* Returns false when the global is not a resource (missing address
 * space, an unhandled address space, or an unhandled handle type):
 * Zig skips such globals. Otherwise fills class and kind. */
bool dxil_classify_resource(DxilAddrSpace addr_space, DxilAccess access,
    DxilResourceTypeKind type_kind, uint32_t *out_class,
    uint32_t *out_kind);

#endif /* DOE_DXIL_MAPPING_H */
