/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_TARGET_HLSL_H
#define DOE_COMPILER_TARGET_HLSL_H
#include "../scratch.h"
#include "../../include/doe_compiler_hlsl.h"

/* Private, allocation-free adapter over borrowed validated module IR. The source,
 * module, scratch, result and output must not overlap or change during the call. Output is
 * Scratch is caller-owned and sized by doe_target_scratch_bytes. Output is
 * an exact byte span, not NUL terminated. NULL/zero measures; failure leaves output
 * bytes alone. selected_entry chooses one entry. NO_ID produces an entry bundle:
 * compile with DOE_HLSL_ENTRY set to the entry index, not all sections together.
 * Buffers use 4-byte StructuredBuffer<uint> views; CBVs preserve WGSL byte offsets.
 * Dispatch metadata, when required, follows retained b0, space7 uint3 + padding.
 * Graphics requires the shared Source stage workspace and maps its typed leaves;
 * invariant output is unsupported without qualified target compilation policy.
 * LINKAGE.md owns canonical target carrier ordering and the optional compiler-
 * named VS output projection define; logical stage reflection is unchanged. */
enum { DOE_HLSL_MAX_OUTPUT = 1024 * 1024, DOE_HLSL_MAX_DEPTH = 96,
       DOE_HLSL_DISPATCH_SLOT = 0, DOE_HLSL_DISPATCH_SPACE = 7 };
typedef struct {
    size_t required_bytes, written_bytes;
    uint32_t entry_count;
    bool needs_dispatch_info, uses_subgroups;
} DoeHlslResult;
/* Optional private staging spans, at least resource_count/entry_count records.
 * These are filled during real target measurement, never directly published. */
typedef struct {
    DoeCompilerHlslBindingV11 *bindings;
    DoeCompilerHlslEntryV11 *entries;
} DoeHlslMetadata;
DoeCompilerStatus doe_hlsl_emit(Source *, const CompilerWorkspace *, uint32_t selected_entry, DoeTargetScratch scratch,
                                char *output, size_t capacity, DoeHlslResult *result);
DoeCompilerStatus doe_hlsl_emit_metadata(Source *, const CompilerWorkspace *, uint32_t selected_entry,
    DoeTargetScratch scratch, DoeHlslMetadata, DoeHlslResult *result);
#endif
