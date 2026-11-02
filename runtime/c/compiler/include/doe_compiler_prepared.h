/* SPDX-License-Identifier: MIT
 * Explicit caller-owned analysis lifetime; existing ABI1-19 calls are unchanged. */
#ifndef DOE_COMPILER_PREPARED_H
#define DOE_COMPILER_PREPARED_H
#include "doe_compiler_diagnostics.h"
#include "doe_compiler_hlsl_profile.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_PREPARED_ABI_VERSION 20u
#define DOE_COMPILER_PREPARE_MODULE 0u
#define DOE_COMPILER_PREPARE_LEGACY 1u
#define DOE_COMPILER_PREPARE_SPECIALIZED 2u

typedef struct {
    uint32_t kind, target, semantic_flags, reserved;
    DoeCompilerTranslationCapacitiesV17 capacities;
    DoeCompilerSamplingOptionsV16 sampling;
    DoeCompilerEntrySelectionV5 selection;
    const DoeCompilerOverrideValueV13 *values;
    size_t value_count, value_record_size;
} DoeCompilerPrepareOptionsV20;
typedef struct {
    DoeCompilerDiagnosticResultV18 validation;
    DoeCompilerHlslProfileResultV19 compiled;
    size_t publication_bytes;
} DoeCompilerPreparedResultV20;

/* The query includes private retained state and the existing compiler workspace.
 * prepare analyzes once and owns no allocation or process-global cache. Source,
 * selection-name and override-value/key spans are borrowed, stable and disjoint
 * until publication finishes. The caller keeps the queried workspace alive and
 * unchanged; no unrelated compiler call may reuse any of it in that interval.
 * The options are captured by value. Wrong ABI/record sizes preserve outputs.
 * Legacy/module modes reject override values; module selection must be NONE.
 * Publication does not parse, validate or specialize source again. It checks all
 * output extents before writing. Text targets need a separate disjoint caller
 * scratch span of publication_bytes; target serialization and FP restoration
 * finish there before any output bytes are published. Failed publication leaves
 * bulk outputs intact and permits retry with corrected extents. No shader-size
 * heuristic, callback allocator, hidden handle or change to legacy ABIs exists.
 */
DoeCompilerStatus doe_compiler_prepare_requirements(uint32_t abi_version,
    const DoeCompilerPrepareOptionsV20 *, size_t options_size,
    DoeCompilerWorkspaceRequirements *, size_t requirements_size);
DoeCompilerStatus doe_compiler_prepare(uint32_t abi_version,
    const DoeCompilerPrepareOptionsV20 *, size_t options_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    DoeCompilerPreparedResultV20 *, size_t result_size);
DoeCompilerStatus doe_compiler_publish_module(uint32_t abi_version,
    void *workspace, size_t workspace_size,
    const DoeCompilerDiagnosticOutputsV18 *, size_t outputs_size,
    DoeCompilerPreparedResultV20 *, size_t result_size);
DoeCompilerStatus doe_compiler_publish(uint32_t abi_version,
    void *workspace, size_t workspace_size, void *publication_scratch, size_t scratch_size,
    const DoeCompilerTranslationOutputsV17 *, size_t outputs_size,
    DoeCompilerPreparedResultV20 *, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
