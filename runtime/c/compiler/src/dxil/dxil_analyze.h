/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_ANALYZE_H
#define DOE_DXIL_ANALYZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dxil_diagnostic.h"
#include "dxil_ir.h"

/* Canonical C WGSL analysis with a checked native DxilIrModule projection.
 * Parsing, types, semantic validation and constant values belong to the shared
 * compiler. Temporary compiler storage is heap-owned and freed before return;
 * the output module and copied source names remain in caller-provided storage.
 * Generated builtin names may point to immutable static strings. The caller
 * must keep name_buf alive while using the module. No temporary pointer escapes.
 *
 * Explicit module/name/workspace capacities fail with OutputTooLarge rather
 * than truncating. Both errno and the floating-point environment are restored.
 * Return true on success. On failure, clear the output module and publish the
 * narrow diagnostic category in out_error (when non-NULL), preserving the first
 * canonical diagnostic. diagnostic is cleared at entry. Source is read by
 * length, not NUL termination. See source-integration.md for ownership/limits. */

bool dxil_analyze(const char *source, size_t source_len,
    DxilIrModule *module, char *name_buf, size_t name_buf_len,
    DxilDiagnostic *diagnostic, DxilDiagKind *out_error);

#endif /* DOE_DXIL_ANALYZE_H */
