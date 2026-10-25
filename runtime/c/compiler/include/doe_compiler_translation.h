/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_TRANSLATION_H
#define DOE_COMPILER_TRANSLATION_H
#include "doe_compiler_sampling.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_TRANSLATION_ABI_VERSION 17u
#define DOE_COMPILER_POOL_TRANSLATION_ENTRIES 28u
#define DOE_COMPILER_METAL_IMMEDIATE_SLOT 29u
#define DOE_COMPILER_HLSL_IMMEDIATE_REGISTER 1u
#define DOE_COMPILER_HLSL_IMMEDIATE_SPACE 7u

typedef struct {
    uint32_t entry_index, immediate_bytes, flags, reserved;
    /* Exactly the accessible bytes in [0,64); padding bits are zero.
     * Consumers with a 64-byte immediate limit reject larger immediate_bytes
     * before consulting this mask. Zero bytes means explicit absence. */
    uint64_t immediate_accessible_bytes_low64;
} DoeCompilerTranslationEntryV17;
typedef struct {
    DoeCompilerSamplingCapacitiesV16 sampling;
    size_t translation_entries;
} DoeCompilerTranslationCapacitiesV17;
typedef struct {
    DoeCompilerSamplingOutputsV16 sampling;
    DoeCompilerTranslationEntryV17 *entries;
    size_t entry_capacity, entry_record_size;
} DoeCompilerTranslationOutputsV17;
typedef struct {
    DoeCompilerSamplingResultV16 sampling;
    size_t required_entries, published_entries;
} DoeCompilerTranslationResultV17;

/* Selected-entry target requirements share the semantic module, staging and
 * atomic publication transaction with ABI16 reflection and sampling pairs.
 * Entry indices address the returned entry array. flags/reserved are zero in
 * this version. Capacities are caller budgets; no hidden allocation occurs.
 * Existing ABI1-16 calls and record layouts remain unchanged. */
DoeCompilerStatus doe_compiler_translation_requirements(uint32_t abi_version, uint32_t target,
    const DoeCompilerTranslationCapacitiesV17 *, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *, size_t requirements_size);
DoeCompilerStatus doe_compiler_specialize_translation(uint32_t abi_version, uint32_t target,
    const DoeCompilerSamplingOptionsV16 *, size_t options_size,
    const DoeCompilerTranslationCapacitiesV17 *, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    const DoeCompilerEntrySelectionV5 *, size_t selection_size,
    const DoeCompilerOverrideValueV13 *, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerTranslationOutputsV17 *, size_t outputs_size,
    DoeCompilerTranslationResultV17 *, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
