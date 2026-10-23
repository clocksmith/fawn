/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Experimental typed standalone compiler ABI, version 4. */
#ifndef DOE_COMPILER_TYPED_H
#define DOE_COMPILER_TYPED_H
#include "doe_compiler_v2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_TYPED_ABI_VERSION 4u
#define DOE_COMPILER_TYPED_SCHEMA_VERSION 4u
#define DOE_COMPILER_TYPED_NONE UINT32_MAX
#define DOE_COMPILER_TYPED_MAX_EXPRESSION_DEPTH 64u
#define DOE_COMPILER_TYPED_MAX_CONTROL_DEPTH 32u
#define DOE_COMPILER_TYPED_MAX_TYPE_SYNTAX_DEPTH 64u

#define DOE_COMPILER_TYPED_WORDS 1u
#define DOE_COMPILER_TYPED_REFLECTION 2u
#define DOE_COMPILER_TYPED_MEASURE 4u
#define DOE_COMPILER_TYPED_PROFILE_CONSERVATIVE_UNIFORM 1u
#define DOE_COMPILER_TYPED_PROFILE_STANDARD_UNIFORM 2u
/* Explicit implementation support, shared by flags-bearing ABI4-11 calls.
 * Unset preserves conservative uniform admission. `requires` checks this bit;
 * it never enables the feature. Natural type/member layouts remain unchanged. */
#define DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT 8u

#define DOE_COMPILER_TYPE_SCALAR 1u
#define DOE_COMPILER_TYPE_VECTOR 2u
#define DOE_COMPILER_TYPE_ARRAY 3u
#define DOE_COMPILER_TYPE_STRUCT 4u
#define DOE_COMPILER_TYPE_RUNTIME_ARRAY 5u
#define DOE_COMPILER_LEAF_BOOL 1u
#define DOE_COMPILER_LEAF_I32 2u
#define DOE_COMPILER_LEAF_U32 3u
#define DOE_COMPILER_LEAF_F32 4u
#define DOE_COMPILER_TYPE_CONSTRUCTIBLE 1u
#define DOE_COMPILER_TYPE_HOST_SHAREABLE 2u
#define DOE_COMPILER_TYPE_FIXED_SIZE 4u
#define DOE_COMPILER_TYPE_HAS_RUNTIME_TAIL 8u
#define DOE_COMPILER_MEMBER_EXPLICIT_ALIGN 1u
#define DOE_COMPILER_MEMBER_EXPLICIT_SIZE 2u
#define DOE_COMPILER_RESOURCE_RUNTIME_TAIL 1u

#define DOE_COMPILER_POOL_NONE 0u
#define DOE_COMPILER_POOL_SOURCE 1u
#define DOE_COMPILER_POOL_TOKENS 2u
#define DOE_COMPILER_POOL_STATEMENTS 3u
#define DOE_COMPILER_POOL_EXPRESSIONS 4u
#define DOE_COMPILER_POOL_TYPES 5u
#define DOE_COMPILER_POOL_MEMBERS 6u
#define DOE_COMPILER_POOL_RESOURCES 7u
#define DOE_COMPILER_POOL_ACCESS_STEPS 8u
#define DOE_COMPILER_POOL_CONSTANTS 9u
#define DOE_COMPILER_POOL_CONSTANT_EDGES 10u
#define DOE_COMPILER_POOL_IR 11u
#define DOE_COMPILER_POOL_IR_OPERANDS 12u
#define DOE_COMPILER_POOL_TARGET_DECLARATIONS 13u
#define DOE_COMPILER_POOL_TYPE_SYNTAX 14u
#define DOE_COMPILER_POOL_TRAVERSAL 15u
#define DOE_COMPILER_POOL_WORDS 16u
#define DOE_COMPILER_POOL_EXPRESSION_DEPTH 17u
#define DOE_COMPILER_POOL_CONTROL_DEPTH 18u
#define DOE_COMPILER_POOL_TYPE_SYNTAX_DEPTH 19u

/* Limits name logical pools, not private record sizes or stable byte offsets. */
typedef struct {
    size_t source_bytes, tokens, statements, expressions;
    size_t types, members, resources, access_steps;
    size_t constants, constant_edges, ir_instructions, ir_operands;
    size_t target_declarations, traversal_items, words;
    uint32_t expression_depth, control_depth, type_syntax_depth, reserved;
} DoeCompilerTypedCapacities;

typedef struct {
    uint32_t offset, length; /* In the names byte span; excludes trailing NUL. */
} DoeCompilerNameV4;

typedef struct {
    uint32_t kind, flags;
    DoeCompilerNameV4 name;
    uint32_t scalar_kind, element_type, vector_width, array_count;
    uint32_t first_member, member_count;
    uint64_t size_bytes, alignment_bytes, stride_bytes;
} DoeCompilerTypeV4;

typedef struct {
    DoeCompilerNameV4 name;
    uint32_t type_index, flags;
    uint64_t offset_bytes, size_bytes, alignment_bytes;
    uint64_t explicit_align_bytes, explicit_size_bytes;
} DoeCompilerMemberV4;

typedef struct {
    DoeCompilerNameV4 name;
    uint32_t group, binding, kind, access, type_index, flags;
    uint64_t minimum_binding_size_bytes;
    uint64_t type_alignment_bytes, address_space_alignment_bytes;
    uint64_t runtime_array_offset_bytes, runtime_array_stride_bytes;
    uint32_t runtime_array_type, reserved;
} DoeCompilerResourceV4;

typedef struct {
    size_t words, types, members, resources, name_bytes;
} DoeCompilerTypedOutputCounts;

/* Record sizes must equal sizeof(the named V4 record) in this ABI. */
typedef struct {
    uint32_t *words;
    size_t word_capacity;
    DoeCompilerTypeV4 *types;
    size_t type_capacity, type_record_size;
    DoeCompilerMemberV4 *members;
    size_t member_capacity, member_record_size;
    DoeCompilerResourceV4 *resources;
    size_t resource_capacity, resource_record_size;
    char *names;
    size_t name_capacity;
} DoeCompilerTypedOutputs;

typedef struct {
    uint32_t schema_version, layout_profile;
    DoeCompilerNameV4 entry_point;
    uint32_t workgroup_size[3];
    uint32_t declared_struct_count;
} DoeCompilerModuleV4;

typedef struct {
    DoeCompilerTypedOutputCounts required;
    DoeCompilerTypedOutputCounts published;
    uint32_t required_is_exact, exhausted_pool;
    size_t minimum_pool_capacity;
    DoeCompilerModuleV4 module;
    DoeCompilerDiagnostic diagnostic;
} DoeCompilerTypedResult;

/* Query the loaded library. Failure leaves requirements unchanged. */
DoeCompilerStatus doe_compiler_typed_requirements(
    uint32_t abi_version,
    const DoeCompilerTypedCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size);

/* Synchronous, no allocation, no retained pointers, no prepared handle.
 * flags selects WORDS and/or REFLECTION, optionally MEASURE. */
DoeCompilerStatus doe_compiler_compile_typed(
    uint32_t abi_version,
    const DoeCompilerTypedCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size,
    const char *source, size_t source_length, uint32_t flags,
    const DoeCompilerTypedOutputs *outputs, size_t outputs_size,
    DoeCompilerTypedResult *result, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
