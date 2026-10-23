/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Experimental caller-workspace API; not the production WebGPU ABI. */
#ifndef DOE_COMPILER_V2_H
#define DOE_COMPILER_V2_H
#include "doe_compiler.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_ABI_VERSION_2 2u
#define DOE_COMPILER_MAX_STRUCTS 8u
#define DOE_COMPILER_MAX_STRUCT_MEMBERS 8u
#define DOE_COMPILER_NO_STRUCT UINT32_MAX
#define DOE_COMPILER_STORAGE_BUFFER 1u
#define DOE_COMPILER_UNIFORM_BUFFER 2u

typedef struct {
    size_t size_bytes;
    size_t alignment_bytes;
} DoeCompilerWorkspaceRequirements;
typedef struct {
    char name[64];
    uint32_t scalar; /* DOE_COMPILER_U32 or DOE_COMPILER_F32. */
    uint32_t offset_bytes, size_bytes, alignment_bytes;
} DoeCompilerMemberV2;
typedef struct {
    char name[64];
    uint32_t member_count, size_bytes, alignment_bytes;
    DoeCompilerMemberV2 members[DOE_COMPILER_MAX_STRUCT_MEMBERS];
} DoeCompilerStructV2;
typedef struct {
    char name[64];
    uint32_t group, binding;
    uint32_t kind; /* DOE_COMPILER_STORAGE_BUFFER or DOE_COMPILER_UNIFORM_BUFFER. */
    uint32_t access; /* DOE_COMPILER_READ or DOE_COMPILER_READ_WRITE. */
    uint32_t scalar; /* Storage element scalar; zero for a uniform struct. */
    uint32_t element_stride, runtime_sized; /* Zero for a uniform struct. */
    uint32_t minimum_binding_size_bytes;
    uint32_t type_alignment_bytes, address_space_alignment_bytes;
    uint32_t struct_index; /* DOE_COMPILER_NO_STRUCT for storage arrays. */
} DoeCompilerBindingV2;
typedef struct {
    char entry_point[64];
    uint32_t workgroup_size[3];
    uint32_t binding_count;
    DoeCompilerBindingV2 bindings[DOE_COMPILER_MAX_BINDINGS];
    uint32_t struct_count;
    DoeCompilerStructV2 structs[DOE_COMPILER_MAX_STRUCTS];
} DoeCompilerReflectionV2;
typedef struct {
    size_t word_count;
    DoeCompilerReflectionV2 reflection;
    DoeCompilerDiagnostic diagnostic;
} DoeCompilerResultV2;

/* Requirements describe this loaded library, not a stable/serialized layout.
 * NULL requirements returns INPUT_ERROR. Wrong ABI/exact struct size returns
 * ABI_MISMATCH and leaves requirements untouched. Success writes both fields.
 * Required alignment is no greater than alignment of max_align_t. */
DoeCompilerStatus doe_compiler_workspace_requirements(
    uint32_t abi_version, DoeCompilerWorkspaceRequirements *requirements,
    size_t requirements_size);

/* Use malloc/aligned allocation providing at least the queried bytes/alignment,
 * not an arbitrary declared char array cast to the private typed workspace.
 * The caller owns allocation/free. Workspace must be disjoint from valid source,
 * words and result spans, which are mutually disjoint as in ABI1. No pointers
 * are retained; no library allocation/free or initialization/destruction call.
 * Only one active call may use a workspace. Distinct workspaces are independent.
 * Reuse after success or failure needs no reset; contents are unspecified after
 * an admitted call. NULL/misaligned/short workspace is INPUT_ERROR before any
 * workspace access. ABI/result-size checks precede all workspace access and leave
 * result, words and workspace untouched on ABI_MISMATCH. NULL result returns
 * INPUT_ERROR without writing. Other failures zero word_count/reflection, report
 * a diagnostic, preserve all words, and leave workspace reusable. Source/output
 * admission and source-diagnostic rules are the same as ABI1. */
DoeCompilerStatus doe_compiler_compile_v2(
    uint32_t abi_version, void *workspace, size_t workspace_size,
    const char *source, size_t source_length,
    uint32_t *words, size_t word_capacity,
    DoeCompilerResultV2 *result, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
