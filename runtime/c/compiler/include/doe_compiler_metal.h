/* SPDX-License-Identifier: MIT
 * Additive entry-specific Metal argument metadata over the shared C pipeline. */
#ifndef DOE_COMPILER_METAL_H
#define DOE_COMPILER_METAL_H
#include "doe_compiler_text.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_METAL_ABI_VERSION 12u
#define DOE_COMPILER_METAL_SIZES_SLOT 30u
/* Rows are entry-specific and source-resource ordered within each entry span.
 * Native buffer/texture/sampler namespaces are independent dense prefixes.
 * Runtime-tail size_index equals its physical buffer slot; otherwise NONE. */
typedef struct {
    uint32_t entry_index, resource_index, table, slot, size_index;
} DoeCompilerMetalBindingV12;
/* first_binding/binding_count select exactly this entry's rows. Buffer resources
 * occupy 0..buffer_count-1; slot 30 is reserved for the size table when used.
 * The consumer places actual vertex streams around those explicit occupied slots.
 * sizes_bytes covers the highest referenced size_index, never source binding IDs. */
typedef struct {
    uint32_t entry_index, stage, first_binding, binding_count, buffer_count;
    uint32_t sizes_slot, sizes_bytes;
    char name[64];
} DoeCompilerMetalEntryV12;
typedef struct {
    DoeCompilerStageOutputs stages;
    char *text;
    size_t text_capacity;
    DoeCompilerMetalBindingV12 *bindings;
    size_t binding_capacity, binding_record_size;
    DoeCompilerMetalEntryV12 *entries;
    size_t entry_capacity, entry_record_size;
} DoeCompilerMetalOutputs;
/* ABI1-11 layouts/requirements/accepted behavior remain unchanged. ABI12 uses
 * per-entry dense argument allocation; ABI10 retains its explicit legacy mapping.
 * Existing TextCapacities/output flags/result/publication/FP rules apply. Staging
 * charges binding rows to the existing entry_references capacity and emits MSL3.
 * Source resources retain their logical identity across entry-specific slots.
 * Text is not terminated. No native SDK/GPU qualification is implied. */
DoeCompilerStatus doe_compiler_metal_requirements(
    uint32_t abi_version, const DoeCompilerTextCapacities *, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *, size_t requirements_size);
DoeCompilerStatus doe_compiler_compile_metal(
    uint32_t abi_version, const DoeCompilerTextCapacities *, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    const DoeCompilerEntrySelectionV5 *, size_t selection_size, uint32_t flags,
    const DoeCompilerMetalOutputs *, size_t outputs_size,
    DoeCompilerTextResult *, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
