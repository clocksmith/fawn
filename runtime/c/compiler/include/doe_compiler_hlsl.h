/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_HLSL_H
#define DOE_COMPILER_HLSL_H
#include "doe_compiler_text.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_HLSL_ABI_VERSION 11u
#define DOE_COMPILER_TARGET_HLSL 2u
#define DOE_COMPILER_HLSL_REGISTER_CBV 1u
#define DOE_COMPILER_HLSL_REGISTER_SRV 2u
#define DOE_COMPILER_HLSL_REGISTER_UAV 3u
#define DOE_COMPILER_HLSL_REGISTER_SAMPLER 4u
#define DOE_COMPILER_HLSL_ENTRY_WAVES 1u
/* Requirements of the emitted entry, not device feature availability. */
#define DOE_COMPILER_HLSL_ENTRY_NATIVE16 2u
#define DOE_COMPILER_HLSL_ENTRY_RAW_STORAGE 4u
#define DOE_COMPILER_HLSL_ENTRY_WAVE_COUNTER 8u
#define DOE_COMPILER_HLSL_ENTRY_ARRAY_VIEWS 16u
#define DOE_COMPILER_HLSL_ENTRY_DRAW_OFFSETS 32u
/* Vertex-stage uint2: first vertex / signed base-vertex bits, first instance. */
#define DOE_COMPILER_HLSL_DRAW_OFFSETS_REGISTER 3u
#define DOE_COMPILER_HLSL_DRAW_OFFSETS_SPACE 7u
#define DOE_COMPILER_HLSL_DRAW_OFFSETS_BYTES 8u
#define DOE_COMPILER_HLSL_WAVE_COUNTER_BYTES 4u
#define DOE_COMPILER_HLSL_STORAGE_SIZES_REGISTER 2u
#define DOE_COMPILER_HLSL_STORAGE_SIZES_SPACE 7u

/* Optional DXC producer projection for a validated native VS/PS pair. The
 * compiler-generated VS carrier keeps only user-output locations whose bits
 * are set; its original return computation and ABI7 reflection stay intact.
 * User fields are ordered by location before builtins in both stages. The
 * consumer validates exact type/width/interpolation/sampling matches first.
 * Absence preserves all output locations, including locations above31. When
 * present, any declared VS location above31 fails target compilation explicitly.
 * The define is a uint32 decimal value, valid only for vertex compilation; zero
 * retains position with no user outputs. It is not a WGSL semantic option. */
#define DOE_COMPILER_HLSL_INTERSTAGE_MASK_DEFINE "DOE_HLSL_INTERSTAGE_MASK"
#define DOE_COMPILER_HLSL_INTERSTAGE_MASK_BITS 32u

/* Sparse, source-resource ordered union of emitted entries. register_kind is a
 * separate namespace, not a resource-kind enum. Structured buffers require
 * stride four; raw storage buffers and all other descriptors use zero.
 * RAW_STORAGE entries require a packed uint size per canonical resource_index
 * at STORAGE_SIZES_REGISTER/SPACE, rounded to uint4 carriers. Values are exact
 * logical binding byte sizes, not padded native RAW view extents. The declared WGSL byte layout
 * remains in the shared resource/type reflection; CBV padding is runtime-owned. */
typedef struct {
    uint32_t resource_index, register_kind, shader_register, register_space, stride;
} DoeCompilerHlslBindingV11;
/* One emitted entry in source order. entry_index is also DOE_HLSL_ENTRY's value
 * when compiling a NONE-selection bundle; NAME/AUTO output needs no define.
 * name is the actual DXC entry. dispatch_register/space are NONE and bytes zero
 * when unused; otherwise they identify a uint3 group-count CBV plus uint padding.
 * flags declare native requirements; WAVE_COUNTER adds WAVE_COUNTER_BYTES to
 * shared entry workgroup_storage_bytes for target resource admission. They are not
 * claims of device support. Resource membership uses shared entry_references. */
typedef struct {
    uint32_t entry_index, stage, flags;
    uint32_t dispatch_register, dispatch_space, dispatch_bytes;
    char name[64];
} DoeCompilerHlslEntryV11;
typedef struct {
    DoeCompilerStageOutputs stages;
    char *text;
    size_t text_capacity;
    DoeCompilerHlslBindingV11 *bindings;
    size_t binding_capacity, binding_record_size;
    DoeCompilerHlslEntryV11 *entries;
    size_t entry_capacity, entry_record_size;
} DoeCompilerHlslOutputs;

/* Additive typed text facade: ABI1-10 signatures/layouts/behavior are unchanged.
 * Capacities, flags, result and failure-publication rules are the text API's.
 * target is HLSL and language_version is 600 (Shader Model 6.0). Allocation-free,
 * one whole-module typed pipeline; no source replay, DXC invocation or fallback.
 * All selected entry closures are validated/emitted, even in reflection/measure.
 * NONE emits separately guarded entries, not one simultaneously compiled module.
 * Entryless NONE succeeds with no text/target records. Text is not terminated.
 * Outputs/records have exact sizes, are aligned/live/pairwise disjoint and remain
 * untouched on failure; result also remains untouched on ABI mismatch. Staging
 * metadata and text belong to the queried workspace. errno/FP state is restored.
 * Host consumption does not qualify DXC, DXIL, Windows SDK or GPU execution. */
DoeCompilerStatus doe_compiler_hlsl_requirements(
    uint32_t abi_version, const DoeCompilerTextCapacities *, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *, size_t requirements_size);
DoeCompilerStatus doe_compiler_compile_hlsl(
    uint32_t abi_version, const DoeCompilerTextCapacities *, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    const DoeCompilerEntrySelectionV5 *, size_t selection_size, uint32_t flags,
    const DoeCompilerHlslOutputs *, size_t outputs_size,
    DoeCompilerTextResult *, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
