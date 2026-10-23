/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_STAGES_H
#define DOE_COMPILER_STAGES_H
#include "doe_compiler_resources.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_STAGES_ABI_VERSION 7u
#define DOE_COMPILER_STAGES_SCHEMA_VERSION 7u
#define DOE_COMPILER_STAGE_VERTEX 2u
#define DOE_COMPILER_STAGE_FRAGMENT 3u
#define DOE_COMPILER_POOL_INTERFACE_ATTRIBUTES 23u
#define DOE_COMPILER_POOL_STAGE_INTERFACES 24u
#define DOE_COMPILER_INTERFACE_INPUT 1u
#define DOE_COMPILER_INTERFACE_OUTPUT 2u
#define DOE_COMPILER_INTERFACE_INVARIANT 1u
#define DOE_COMPILER_INTERPOLATION_PERSPECTIVE 1u
#define DOE_COMPILER_INTERPOLATION_LINEAR 2u
#define DOE_COMPILER_INTERPOLATION_FLAT 3u
#define DOE_COMPILER_SAMPLING_CENTER 1u
#define DOE_COMPILER_SAMPLING_CENTROID 2u
#define DOE_COMPILER_SAMPLING_SAMPLE 3u
#define DOE_COMPILER_SAMPLING_FIRST 4u
#define DOE_COMPILER_SAMPLING_EITHER 5u
/* Builtin identities are independent of SPIR-V numeric enumerants. */
#define DOE_COMPILER_BUILTIN_POSITION 1u
#define DOE_COMPILER_BUILTIN_VERTEX_INDEX 2u
#define DOE_COMPILER_BUILTIN_INSTANCE_INDEX 3u
#define DOE_COMPILER_BUILTIN_FRONT_FACING 4u
#define DOE_COMPILER_BUILTIN_FRAG_DEPTH 5u
#define DOE_COMPILER_BUILTIN_SAMPLE_INDEX 6u
#define DOE_COMPILER_BUILTIN_SAMPLE_MASK 7u

typedef struct {
    DoeCompilerModuleCapacities module;
    size_t interface_attributes, stage_interfaces;
} DoeCompilerStageCapacities;
/* One record per declared graphics scalar/vector IO leaf, in entry/source order.
 * Flat structures are flattened only at the target interface, not in typed IR.
 * A builtin leaf has location NONE; a location leaf has builtin zero.
 * Integer vertex outputs/fragment inputs are flat. Other non-interpolated leaves
 * have interpolation/sampling zero. No implicit location allocation occurs.
 * name is a source parameter/member name copied into the shared names span.
 */
typedef struct {
    uint32_t entry_index, direction, location, builtin;
    uint32_t scalar_kind, lane_count, interpolation, sampling, flags, reserved;
    DoeCompilerNameV4 name;
} DoeCompilerStageInterfaceV7;
typedef struct {
    DoeCompilerResourceOutputs resources;
    DoeCompilerStageInterfaceV7 *interfaces;
    size_t interface_capacity, interface_record_size;
} DoeCompilerStageOutputs;
typedef struct {
    DoeCompilerResourceResult resources;
    size_t required_interfaces, published_interfaces;
} DoeCompilerStageResult;
/* The same complete typed pipeline emits all compute, vertex and fragment entries.
 * Stage metadata is additive: existing entry/resource records retain their layouts.
 * Graphics entry workgroup sizes and compute builtin masks are zero. Interface
 * records describe graphics entries only; compute inputs retain ABI5 masks.
 * Position components preserve source semantics. WebGPU-to-native Vulkan Y
 * mapping is an explicit viewport/raster consumer obligation, not a hidden flip.
 * ABI1..6 do not provide the explicit graphics interface pools.
 * Same exact descriptor-size, span alignment, checked capacity, errno/FP and
 * failure-atomic publication rules as ABI6. Selected zero-capacity spans are valid
 * and report exact required counts; unselected spans must be NULL/zero.
 */
DoeCompilerStatus doe_compiler_stage_requirements(
    uint32_t abi_version, const DoeCompilerStageCapacities *, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *, size_t requirements_size);
DoeCompilerStatus doe_compiler_compile_stages(
    uint32_t abi_version, const DoeCompilerStageCapacities *, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    const DoeCompilerEntrySelectionV5 *, size_t selection_size, uint32_t flags,
    const DoeCompilerStageOutputs *, size_t outputs_size,
    DoeCompilerStageResult *, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
