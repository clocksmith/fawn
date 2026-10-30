/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_HLSL_PROFILE_H
#define DOE_COMPILER_HLSL_PROFILE_H
#include "doe_compiler_translation.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_HLSL_PROFILE_ABI_VERSION 19u

/* A conservative requirement for the exact selected HLSL translation unit,
 * including emitted declarations/helpers. 501 certifies the closed ordinary
 * loop-free buffer-compute subset; 600/620 retain the existing DXC baseline/native16
 * requirements. language_version and all ABI11 records remain unchanged.
 * This record selects a target compiler profile, never an optimization policy.
 * entry_index addresses the published selected entry; reserved words are zero. */
typedef struct {
    uint32_t entry_index, stage, shader_model, reserved;
} DoeCompilerHlslProfileV19;
typedef struct {
    DoeCompilerTranslationResultV17 translation;
    DoeCompilerHlslProfileV19 profile;
} DoeCompilerHlslProfileResultV19;

/* Explicit selected-HLSL facade over the same ABI17 transaction and caller
 * budgets. No new workspace allocation or second parse/emit pass. Existing
 * ABI1-18 calls, layouts, workspace sizes and outputs remain unchanged.
 * The same input/output span and failure contracts as ABI17 apply. */
DoeCompilerStatus doe_compiler_hlsl_profile_requirements(uint32_t abi_version,
    const DoeCompilerTranslationCapacitiesV17 *, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *, size_t requirements_size);
DoeCompilerStatus doe_compiler_specialize_hlsl_profile(uint32_t abi_version,
    const DoeCompilerSamplingOptionsV16 *, size_t options_size,
    const DoeCompilerTranslationCapacitiesV17 *, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    const DoeCompilerEntrySelectionV5 *, size_t selection_size,
    const DoeCompilerOverrideValueV13 *, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerTranslationOutputsV17 *, size_t outputs_size,
    DoeCompilerHlslProfileResultV19 *, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
