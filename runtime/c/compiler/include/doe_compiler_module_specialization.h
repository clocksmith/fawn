/* SPDX-License-Identifier: MIT
 * Additive all-entry specialization over the existing typed module owner. */
#ifndef DOE_COMPILER_MODULE_SPECIALIZATION_H
#define DOE_COMPILER_MODULE_SPECIALIZATION_H
#include "doe_compiler_specialization.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_MODULE_SPECIALIZATION_ABI_VERSION 14u

/* Specialize every shader entry in source order, retaining compute and graphics
 * stages and the union of their statically accessed declarations. A module must
 * contain at least one shader entry. Every supplied key/value validates; missing
 * values reject only when statically accessed and lacking an initializer.
 * Unreachable functions and workgroup roots do not force deferred expressions.
 * Existing target capacities, override values, output records and transactional
 * publication/FP/errno contracts are reused exactly. No selection, allocation,
 * source substitution, target fallback or retained caller pointer is introduced.
 * ABI13 selected-entry requirements, layout and NONE rejection are unchanged.
 */
DoeCompilerStatus doe_compiler_module_specialization_requirements(
    uint32_t abi_version, uint32_t target,
    const DoeCompilerSpecializationCapacities *, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *, size_t requirements_size);
DoeCompilerStatus doe_compiler_specialize_module(
    uint32_t abi_version, uint32_t target,
    const DoeCompilerSpecializationCapacities *, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    const DoeCompilerOverrideValueV13 *, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSpecializationOutputs *, size_t outputs_size,
    DoeCompilerTextResult *, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
