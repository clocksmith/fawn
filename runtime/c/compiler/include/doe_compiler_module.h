/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * PROPOSED ABI5. Design only; not an implemented or frozen public header. */
#ifndef DOE_COMPILER_MODULE_H
#define DOE_COMPILER_MODULE_H
#include "doe_compiler_typed.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_MODULE_ABI_VERSION 5u
#define DOE_COMPILER_MODULE_SCHEMA_VERSION 5u
#define DOE_COMPILER_MODULE_NONE UINT32_MAX
#define DOE_COMPILER_MODULE_WORDS 1u
#define DOE_COMPILER_MODULE_REFLECTION 2u
#define DOE_COMPILER_MODULE_MEASURE 4u
#define DOE_COMPILER_SELECT_NONE 0u
#define DOE_COMPILER_SELECT_AUTO 1u
#define DOE_COMPILER_SELECT_NAME 2u
#define DOE_COMPILER_STAGE_COMPUTE 1u
#define DOE_COMPILER_ENTRY_WORKGROUP_INITIALIZATION 1u
#define DOE_COMPILER_POOL_ENTRIES 20u
#define DOE_COMPILER_POOL_ENTRY_INPUTS 21u
#define DOE_COMPILER_POOL_ENTRY_REFERENCES 22u
#define DOE_COMPILER_INPUT_GLOBAL_INVOCATION_ID 1u
#define DOE_COMPILER_INPUT_LOCAL_INVOCATION_ID 2u
#define DOE_COMPILER_INPUT_WORKGROUP_ID 4u
#define DOE_COMPILER_INPUT_NUM_WORKGROUPS 8u
#define DOE_COMPILER_INPUT_LOCAL_INVOCATION_INDEX 16u

typedef struct {
    DoeCompilerTypedCapacities typed; /* Same logical pools, not an ABI4 invocation. */
    size_t entries, entry_inputs, entry_references;
} DoeCompilerModuleCapacities;

typedef struct {
    uint32_t mode, reserved;
    const char *name;
    size_t name_length; /* Borrowed exact bytes, not a required C string. */
} DoeCompilerEntrySelectionV5;

typedef struct {
    DoeCompilerNameV4 name;
    uint32_t type_index, reserved;
    uint64_t size_bytes, alignment_bytes;
} DoeCompilerWorkgroupV5;

typedef struct {
    uint32_t stage, flags;
    DoeCompilerNameV4 name;
    uint32_t workgroup_size[3];
    uint32_t declared_builtin_mask, static_builtin_mask;
    uint32_t first_resource_reference, resource_reference_count;
    uint32_t first_workgroup_reference, workgroup_reference_count;
    uint64_t workgroup_storage_bytes;
} DoeCompilerEntryV5;

typedef struct {
    size_t words, types, members, resources, workgroups;
    size_t entries, entry_references, name_bytes;
} DoeCompilerModuleOutputCounts;

typedef struct {
    uint32_t *words;
    size_t word_capacity;
    DoeCompilerTypeV4 *types;
    size_t type_capacity, type_record_size;
    DoeCompilerMemberV4 *members;
    size_t member_capacity, member_record_size;
    DoeCompilerResourceV4 *resources;
    size_t resource_capacity, resource_record_size;
    DoeCompilerWorkgroupV5 *workgroups;
    size_t workgroup_capacity, workgroup_record_size;
    DoeCompilerEntryV5 *entries;
    size_t entry_capacity, entry_record_size;
    uint32_t *entry_references;
    size_t entry_reference_capacity;
    char *names;
    size_t name_capacity;
} DoeCompilerModuleOutputs;

typedef struct {
    uint32_t schema_version, layout_profile, declared_struct_count, selected_entry;
} DoeCompilerModuleV5;

typedef struct {
    DoeCompilerModuleOutputCounts required, published;
    uint32_t required_is_exact, exhausted_pool;
    size_t minimum_pool_capacity;
    DoeCompilerModuleV5 module;
    DoeCompilerDiagnostic diagnostic;
} DoeCompilerModuleResult;

/* Exactly two additional exported symbols. Old seven symbols/layouts stay intact.
 * Requirements query does not read source and leaves requirements unchanged on error.
 * Each public size argument must equal sizeof its named type, before dereferencing
 * that descriptor. Each populated record, including padding, is initialized.
 * Caller owns valid, aligned, pairwise-disjoint live objects and full-capacity spans.
 * No overlap-detection, library allocation, retained pointer or prepared handle.
 * Independent workspaces may compile concurrently; reuse needs no reset.
 * All modes perform the same whole-module validation and private SPIR-V1.3 emission.
 * Selection changes metadata validation/projection only, never emitted entry set.
 * NONE/AUTO require NULL name and zero length; NAME borrows a nonempty ASCII
 * identifier span bounded by the supplied source length.
 * WORDS-only/MEASURE/failure module fields are all zero. In published reflection,
 * selected_entry is NONE for NONE, otherwise an index into the entries span.
 * Each entry's resource range contains indices into resources; its workgroup
 * range contains indices into workgroups. Both ranges index entry_references.
 * All failure publication, capacities, exact counts and errno/FP rules: ../MILESTONE_13.md.
 */
DoeCompilerStatus doe_compiler_module_requirements(
    uint32_t abi_version,
    const DoeCompilerModuleCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size);

DoeCompilerStatus doe_compiler_compile_module(
    uint32_t abi_version,
    const DoeCompilerModuleCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size,
    const char *source, size_t source_length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    uint32_t flags,
    const DoeCompilerModuleOutputs *outputs, size_t outputs_size,
    DoeCompilerModuleResult *result, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
