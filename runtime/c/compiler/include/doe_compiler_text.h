/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_TEXT_H
#define DOE_COMPILER_TEXT_H
#include "doe_compiler_stages.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_TEXT_ABI_VERSION 10u
#define DOE_COMPILER_TARGET_METAL 1u
#define DOE_COMPILER_TEXT_CODE 1u
#define DOE_COMPILER_TEXT_REFLECTION 2u
#define DOE_COMPILER_TEXT_MEASURE 4u
#define DOE_COMPILER_POOL_TEXT_BYTES 25u
#define DOE_COMPILER_TEXT_TABLE_BUFFER 1u
#define DOE_COMPILER_TEXT_TABLE_TEXTURE 2u
#define DOE_COMPILER_TEXT_TABLE_SAMPLER 3u

typedef struct {
    DoeCompilerStageCapacities stages;
    size_t text_bytes;
} DoeCompilerTextCapacities;
/* Sparse, source-resource ordered records for the emitted entry union only.
 * Existing entry_references retain per-entry membership. Separate Metal buffer,
 * texture and sampler namespaces share no numeric enum domain with resource kind.
 * size_index is NONE except for runtime-tail buffers. The corresponding sizes
 * element is the actual bound byte length as uint32, not element count. */
typedef struct {
    uint32_t resource_index, table, slot, size_index;
} DoeCompilerTextBindingV10;
/* One record per emitted entry, in original entry order. name is the actual
 * Metal host_name, not the internal doe_entryN spelling. Size metadata is local
 * to this entry: NONE/zero when unused, otherwise the minimum uint32 array byte
 * span at sizes_slot. Unreferenced size elements may be zero. */
typedef struct {
    uint32_t entry_index, stage, sizes_slot, sizes_bytes;
    char name[64];
} DoeCompilerTextEntryV10;
typedef struct {
    DoeCompilerStageOutputs stages;
    char *text;
    size_t text_capacity;
    DoeCompilerTextBindingV10 *bindings;
    size_t binding_capacity, binding_record_size;
    DoeCompilerTextEntryV10 *entries;
    size_t entry_capacity, entry_record_size;
} DoeCompilerTextOutputs;
typedef struct {
    DoeCompilerStageResult stages;
    size_t required_text_bytes, published_text_bytes;
    size_t required_bindings, published_bindings;
    size_t required_entries, published_entries;
    uint32_t target, language_version;
} DoeCompilerTextResult;

/* Exactly two additive exports. ABI1-9 layouts, requirements and behavior remain
 * unchanged. METAL currently targets MSL3.0; unsupported targets reject rather
 * than falling back. No Metal SDK compilation or device qualification is implied.
 * One source pipeline validates the complete module, including unused siblings,
 * before target selection. NONE emits all entries; AUTO/NAME selects one closure.
 * Reflection remains whole-module ABI9 metadata; the two text spans describe
 * emitted entries/resources only. Stage word spans must always be NULL/zero.
 * CODE and/or REFLECTION must be selected. MEASURE requires NULL outputs and
 * reports exact counts without publication. Text is exact bytes, not terminated.
 * Entryless NONE succeeds with zero target text and zero target records.
 * Query appends exclusive target closure scratch, per-capacity resource/entry
 * metadata staging records and text_bytes staging storage
 * to the existing stage workspace. MEASURE and reflection-only calls may use
 * text_bytes=0; code publication needs at least the measured text length there.
 * Caller spans/objects must be aligned, live and pairwise disjoint. The compiler
 * allocates nothing, retains nothing, restores errno/FP state and publishes no
 * bulk output on failure. Zero-capacity selected spans are valid sizing probes.
 * Exact descriptor/record sizes are checked before descriptor use, as in ABI9.
 */
DoeCompilerStatus doe_compiler_text_requirements(
    uint32_t abi_version, uint32_t target,
    const DoeCompilerTextCapacities *, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *, size_t requirements_size);
DoeCompilerStatus doe_compiler_compile_text(
    uint32_t abi_version, uint32_t target,
    const DoeCompilerTextCapacities *, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    const DoeCompilerEntrySelectionV5 *, size_t selection_size, uint32_t flags,
    const DoeCompilerTextOutputs *, size_t outputs_size,
    DoeCompilerTextResult *, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
