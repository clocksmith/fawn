/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_METAL_PIPELINE_H
#define DOE_COMPILER_METAL_PIPELINE_H
#include "doe_compiler_specialization.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_METAL_PIPELINE_ABI_VERSION 15u
typedef struct { uint32_t sample_mask; } DoeCompilerMetalPipelineOptionsV15;
/* Selected fragment specialization over ABI13's unchanged Metal workspace
 * requirements and output records. Options are exact-sized and borrowed only
 * during this call. The wrapper applies sample_mask after calling the shader,
 * ANDing its coverage output when present; UINT32_MAX preserves existing output.
 * Shader interface reflection continues to describe the declared WGSL interface,
 * not the generated target coverage field. Zero does not elide shader effects.
 * Non-fragment selections reject. Source, override, flags, transactional output,
 * FP and errno rules are those of ABI13; wrong sizes/ABI leave result untouched. */
DoeCompilerStatus doe_compiler_specialize_metal_pipeline(
    uint32_t abi_version, const DoeCompilerMetalPipelineOptionsV15 *, size_t options_size,
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
