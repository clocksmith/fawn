/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_SAMPLING_H
#define DOE_COMPILER_SAMPLING_H
#include "doe_compiler_specialization.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_SAMPLING_ABI_VERSION 16u
#define DOE_COMPILER_POOL_SAMPLING_PAIRS 27u
#define DOE_COMPILER_SAMPLING_METAL_PIPELINE 1u

typedef struct {
    uint32_t entry_index, texture_resource_index, sampler_resource_index, reserved;
} DoeCompilerSamplingPairV16;
typedef struct {
    DoeCompilerSpecializationCapacities specialization;
    size_t sampling_pairs;
} DoeCompilerSamplingCapacitiesV16;
typedef struct {
    uint32_t metal_sample_mask, flags;
} DoeCompilerSamplingOptionsV16;
typedef struct {
    DoeCompilerSpecializationOutputs target;
    DoeCompilerSamplingPairV16 *pairs;
    size_t pair_capacity, pair_record_size;
} DoeCompilerSamplingOutputsV16;
typedef struct {
    DoeCompilerTextResult target;
    size_t required_pairs, published_pairs;
} DoeCompilerSamplingResultV16;

/* Additive selected-entry specialization with complete sampling relationships.
 * Pairs reference the returned entry/resource arrays, including helper calls
 * with formal handles substituted at each call site. Only sample/gather
 * operations create pairs. Duplicate pairs are removed in first-use order.
 * Reflection is required. The options' mask is the ABI15 Metal coverage mask;
 * set METAL_PIPELINE only for ABI15 fragment specialization. Ordinary compilation
 * uses flags0 and mask UINT32_MAX; other flag bits reject.
 * Existing ABI1-15 records/calls are unchanged. Explicit staging capacity is a
 * caller budget, not a language limit. Measurement reports required_pairs;
 * insufficient staging reports pool27 and minimum required capacity. All code,
 * reflection and pair spans publish atomically after every fallible operation.
 * Caller spans must be aligned, live and mutually disjoint. No allocation,
 * retained pointers, source substitution or target fallback is introduced.
 */
DoeCompilerStatus doe_compiler_sampling_requirements(
    uint32_t abi_version, uint32_t target,
    const DoeCompilerSamplingCapacitiesV16 *, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *, size_t requirements_size);
DoeCompilerStatus doe_compiler_specialize_sampling(
    uint32_t abi_version, uint32_t target,
    const DoeCompilerSamplingOptionsV16 *, size_t options_size,
    const DoeCompilerSamplingCapacitiesV16 *, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    const DoeCompilerEntrySelectionV5 *, size_t selection_size,
    const DoeCompilerOverrideValueV13 *, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSamplingOutputsV16 *, size_t outputs_size,
    DoeCompilerSamplingResultV16 *, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
