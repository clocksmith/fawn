/* SPDX-License-Identifier: MIT
 * Additive module-validation and selected-entry specialization contract. */
#ifndef DOE_COMPILER_SPECIALIZATION_H
#define DOE_COMPILER_SPECIALIZATION_H
#include "doe_compiler_metal.h"
#include "doe_compiler_hlsl.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_SPECIALIZATION_ABI_VERSION 13u
#define DOE_COMPILER_TARGET_SPIRV 0u
#define DOE_COMPILER_POOL_OVERRIDES 26u
#define DOE_COMPILER_OVERRIDE_HAS_ID 1u
#define DOE_COMPILER_OVERRIDE_HAS_INITIALIZER 2u

typedef struct {
    DoeCompilerTextCapacities target;
    size_t overrides;
} DoeCompilerSpecializationCapacities;
typedef struct {
    const char *key;
    size_t key_length;
    double value;
} DoeCompilerOverrideValueV13;
typedef struct {
    DoeCompilerNameV4 name;
    uint32_t scalar_kind, flags, id, reserved;
} DoeCompilerOverrideV13;
typedef struct {
    DoeCompilerNameV4 name;
    uint32_t stage, reserved;
} DoeCompilerModuleEntryV13;
typedef struct {
    DoeCompilerOverrideV13 *overrides;
    size_t override_capacity, override_record_size;
    DoeCompilerModuleEntryV13 *entries;
    size_t entry_capacity, entry_record_size;
    char *names;
    size_t name_capacity;
} DoeCompilerValidationOutputs;
typedef struct {
    size_t required_overrides, required_entries, required_name_bytes;
    size_t published_overrides, published_entries, published_name_bytes;
    uint32_t required_is_exact, exhausted_pool;
    size_t minimum_pool_capacity;
    DoeCompilerDiagnostic diagnostic;
} DoeCompilerValidationResult;
typedef union {
    DoeCompilerStageOutputs spirv;
    DoeCompilerMetalOutputs metal;
    DoeCompilerHlslOutputs hlsl;
} DoeCompilerSpecializationOutputs;

/* ABI1-12 layouts, workspace requirements and override admission remain unchanged.
 * ABI13 adds explicit override storage, not a hidden allocator or prepared handle.
 * Validation checks the whole source, including unused functions and initializer
 * types, without substituting values or publishing executable layouts/code.
 * NULL outputs is measurement. Output record sizes are exact; names are copied.
 * Specialization accepts AUTO/NAME selection, checks every supplied key/value,
 * and evaluates only the selected shader's override expressions. Missing values
 * are errors only for statically accessed declarations without initializers.
 * An @id override uses its canonical decimal ID key; otherwise its declared name.
 * No source substitution, target fallback, retained pointers or library allocation.
 * Target output union member/records follow SPIRV9/Metal12/HLSL11 contracts;
 * the common text result embeds SPIR-V counts in stages, with text counts zero.
 * Measure/fill run the same semantic/target work. Failed calls publish no bulk
 * outputs, wrong version/exact descriptor sizes touch nothing, and errno/FP state
 * is restored before publication. Caller spans are stable/aligned/disjoint.
 */
DoeCompilerStatus doe_compiler_specialization_requirements(
    uint32_t abi_version, uint32_t target,
    const DoeCompilerSpecializationCapacities *, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *, size_t requirements_size);
DoeCompilerStatus doe_compiler_validate_module(
    uint32_t abi_version, const DoeCompilerSpecializationCapacities *, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    uint32_t semantic_flags, const DoeCompilerValidationOutputs *, size_t outputs_size,
    DoeCompilerValidationResult *, size_t result_size);
DoeCompilerStatus doe_compiler_specialize(
    uint32_t abi_version, uint32_t target,
    const DoeCompilerSpecializationCapacities *, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    const DoeCompilerEntrySelectionV5 *, size_t selection_size,
    const DoeCompilerOverrideValueV13 *, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSpecializationOutputs *, size_t outputs_size,
    DoeCompilerTextResult *, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
