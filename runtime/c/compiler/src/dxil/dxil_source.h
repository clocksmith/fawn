/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_SOURCE_H
#define DOE_DXIL_SOURCE_H
#include "dxil_emit.h"

/* Allocating host adapter over the canonical C semantic/specialization/HLSL
 * owners. The first source entry is selected, matching retained DXIL lowering.
 * Source is borrowed only during the call. On success text is caller-owned,
 * exact bytes (not terminated); entry.name points into this result. Release via
 * dxil_source_hlsl_free before reusing the result. On failure the result is zero
 * and the first source or allocation diagnostic is retained. No Zig fallback.
 * Explicit host limits: 2 MiB source and 256 MiB individual workspace.
 * This is not the allocation-free public compiler ABI or device qualification. */
typedef struct DxilSourceHlsl {
    char *text;
    size_t text_length;
    DxilLoweredModule entry;
    char entry_name[64];
} DxilSourceHlsl;

bool dxil_source_hlsl(const char *source, size_t length, DxilSourceHlsl *out,
    DxilDiagnostic *diagnostic, DxilDiagKind *out_error);
void dxil_source_hlsl_free(DxilSourceHlsl *result);
#endif
