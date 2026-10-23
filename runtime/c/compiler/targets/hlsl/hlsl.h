/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_TARGET_HLSL_H
#define DOE_COMPILER_TARGET_HLSL_H
#include "../../src/internal.h"

/* Private, allocation-free adapter over borrowed validated module IR. The source,
 * module, result and output must not overlap or change during the call. Output is
 * an exact byte span, not NUL terminated. NULL/zero measures; failure leaves output
 * bytes alone. selected_entry chooses one entry. NO_ID produces an entry bundle:
 * compile with DOE_HLSL_ENTRY set to the entry index, not all sections together.
 * Buffers use 4-byte StructuredBuffer<uint> views; CBVs preserve WGSL byte offsets.
 * Dispatch metadata, when required, follows retained b0, space7 uint3 + padding. */
enum { DOE_HLSL_MAX_OUTPUT = 1024 * 1024, DOE_HLSL_MAX_DEPTH = 96,
       DOE_HLSL_DISPATCH_SLOT = 0, DOE_HLSL_DISPATCH_SPACE = 7 };
typedef struct {
    size_t required_bytes, written_bytes;
    uint32_t entry_count;
    bool needs_dispatch_info, uses_subgroups;
} DoeHlslResult;
DoeCompilerStatus doe_hlsl_emit(Source *, const CompilerWorkspace *, uint32_t selected_entry,
                                char *output, size_t capacity, DoeHlslResult *result);
#endif
