/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Experimental resource-aware module ABI, version 6. */
#ifndef DOE_COMPILER_RESOURCES_H
#define DOE_COMPILER_RESOURCES_H
#include "doe_compiler_module.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_RESOURCES_ABI_VERSION 6u
#define DOE_COMPILER_RESOURCES_SCHEMA_VERSION 6u
#define DOE_COMPILER_SAMPLED_TEXTURE 3u
#define DOE_COMPILER_STORAGE_TEXTURE 4u
#define DOE_COMPILER_SAMPLER 5u
#define DOE_COMPILER_WRITE 3u
#define DOE_COMPILER_VIEW_1D 1u
#define DOE_COMPILER_VIEW_2D 2u
#define DOE_COMPILER_VIEW_2D_ARRAY 3u
#define DOE_COMPILER_VIEW_CUBE 4u
#define DOE_COMPILER_VIEW_CUBE_ARRAY 5u
#define DOE_COMPILER_VIEW_3D 6u
#define DOE_COMPILER_HANDLE_DEPTH 1u
#define DOE_COMPILER_HANDLE_MULTISAMPLED 2u
#define DOE_COMPILER_HANDLE_COMPARISON 4u
/* Storage formats use the corresponding SPIR-V Image Format value. Zero is
 * not a storage format. Unsupported formats are rejected, never approximated. */
#define DOE_COMPILER_FORMAT_RGBA32FLOAT 1u
#define DOE_COMPILER_FORMAT_RGBA16FLOAT 2u
#define DOE_COMPILER_FORMAT_R32FLOAT 3u
#define DOE_COMPILER_FORMAT_RGBA8UNORM 4u
#define DOE_COMPILER_FORMAT_RGBA8SNORM 5u
#define DOE_COMPILER_FORMAT_RG32FLOAT 6u
#define DOE_COMPILER_FORMAT_RGBA32SINT 21u
#define DOE_COMPILER_FORMAT_RGBA16SINT 22u
#define DOE_COMPILER_FORMAT_RGBA8SINT 23u
#define DOE_COMPILER_FORMAT_R32SINT 24u
#define DOE_COMPILER_FORMAT_RG32SINT 25u
#define DOE_COMPILER_FORMAT_RGBA32UINT 30u
#define DOE_COMPILER_FORMAT_RGBA16UINT 31u
#define DOE_COMPILER_FORMAT_RGBA8UINT 32u
#define DOE_COMPILER_FORMAT_R32UINT 33u
#define DOE_COMPILER_FORMAT_RG32UINT 35u

typedef struct {
    uint32_t resource_index; /* Index into module.resources; source order. */
    uint32_t view_dimension, sample_scalar, storage_format, flags;
    uint32_t reserved[3];
} DoeCompilerHandleV6;
typedef struct {
    DoeCompilerModuleOutputs module;
    DoeCompilerHandleV6 *handles;
    size_t handle_capacity, handle_record_size;
} DoeCompilerResourceOutputs;
typedef struct {
    DoeCompilerModuleResult module;
    size_t required_handles, published_handles;
} DoeCompilerResourceResult;

/* Same capacities, selection and flags as ABI5. Requirements do not inspect
 * source; query the loaded library rather than caching its private byte size.
 * All descriptor/record sizes must match exactly. Failure publishes no bulk
 * output. MEASURE reports exact counts after the ordinary complete pipeline.
 * Handles is sparse: one descriptor per texture/sampler resource, none for
 * buffers. For handles, ResourceV4.type_index/runtime_array_type are NONE;
 * every buffer layout/size field is zero. Handles are not host data types and
 * do not enter the TypeV4 graph. Scalar kind uses DOE_COMPILER_LEAF_*; samplers
 * have zero dimension/scalar/format. Sampled textures have zero storage format.
 * Schema6 resource kinds extend the enum, not the frozen ABI1–5 record layouts.
 * Physical format, view, sampling and device capability admission is the
 * runtime's responsibility. Successful compilation is not device admission. */
DoeCompilerStatus doe_compiler_resource_requirements(
    uint32_t abi_version,
    const DoeCompilerModuleCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size);
DoeCompilerStatus doe_compiler_compile_resources(
    uint32_t abi_version,
    const DoeCompilerModuleCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size,
    const char *source, size_t source_length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    uint32_t flags,
    const DoeCompilerResourceOutputs *outputs, size_t outputs_size,
    DoeCompilerResourceResult *result, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
