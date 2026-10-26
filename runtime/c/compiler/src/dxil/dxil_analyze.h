/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_ANALYZE_H
#define DOE_DXIL_ANALYZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dxil_diagnostic.h"
#include "dxil_ir.h"

/* Bounded WGSL subset analyzer, porting the shape of
 * pipeline/analysis.zig (parse, sema, lower) for the modules the
 * DXIL paths consume. It genuinely parses WGSL text into the
 * dxil_ir.h module: every accepted construct builds IR the emitters
 * walk, and every rejected construct fails visibly. The full
 * frontend (complete WGSL grammar, full sema type checking,
 * ir_validate, the robustness transform, the opt rewrite, pipeline
 * overrides) is NOT ported; see parity-test.md section 15 for the
 * exact subset and gaps.md for what is out of scope.
 *
 * Name ownership: every name stored in the returned module (struct,
 * global, function, parameter, local, call, and member field names)
 * is a NUL-terminated copy inside the caller-provided name buffer.
 * The caller keeps name_buf alive while using the module; the
 * module itself owns no heap storage. A name buffer overflow fails
 * with OutputTooLarge. The module's other tables are caller-owned
 * fixed capacities in dxil_ir.h; exceeding one fails with
 * OutputTooLarge.
 *
 * Return convention: true on success, false on failure. On failure
 * *out_error holds the translate status (a DxilDiagKind value) when
 * out_error is non-NULL, and the diagnostic holds the message when
 * diagnostic is non-NULL (stage parser for lexical/syntactic
 * failures, sema for name/type failures, ir_builder for IR capacity
 * failures). The diagnostic is cleared at entry. Source text is read
 * by length, never by NUL termination. */

bool dxil_analyze(const char *source, size_t source_len,
    DxilIrModule *module, char *name_buf, size_t name_buf_len,
    DxilDiagnostic *diagnostic, DxilDiagKind *out_error);

#endif /* DOE_DXIL_ANALYZE_H */
