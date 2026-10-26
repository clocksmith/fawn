/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_TRANSLATE_H
#define DOE_DXIL_TRANSLATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dxil_diagnostic.h"
#include "dxil_emit.h"

/* WGSL-to-DXIL translate entry points. The native compatibility path
 * uses the canonical native projection in dxil_analyze.h; the toolchain path
 * uses dxil_source.h over the shared typed C compiler and HLSL owner.
 * All four Zig variants are present; all take WGSL
 * source text and a caller output buffer, and write the byte count
 * on success.
 *
 * Return convention (Zig returns a size or an error; C has no error
 * union): each function returns true on success and false on
 * failure. On failure, *out_error holds the translate status when
 * out_error is non-NULL, and the diagnostic holds the message when
 * diagnostic is non-NULL. At least one of the two should be provided
 * by callers that need to know why a translation failed.
 *
 * The translate status set IS the diagnostic kind set (see
 * dxil_diagnostic.h): Zig's diagnostic kind is TranslateError, and
 * the translate functions report that same set. DxilTranslateStatus
 * is an alias so call sites name the layer they are in.
 *
 * Lifetime model, explicit: Zig's WithDiagnostic variant allocates
 * the IR module in an arena freed at return, and its toolchain
 * variant allocates with the caller allocator and deinits after
 * emit. The C native path owns a bounded heap IR module and temporary compiler
 * workspace, both freed before returning.
 * The toolchain path owns temporary compiler workspace, reflection and
 * exact HLSL bytes and frees them before return. Output bytes are
 * caller-owned and never alias temporary storage. The two spellings
 * of each path differ only in caller diagnostic preference.
 *
 * Diagnostic rule, mirroring Zig: on analyze failure the analyzer's
 * diagnostic stands and its status is returned unchanged. On emit
 * failure the caller diagnostic receives stage dxil_emit, the mapped
 * kind, and, when the internal emission diagnostic message is
 * non-empty, that whole message as its detail (Zig's double-prefix
 * shape is preserved deliberately: for gate failures the caller
 * message reads "dxil_emit: <Kind>: dxil_emit: <Kind>: <detail>").
 * When the internal message is empty the caller diagnostic records
 * stage dxil_emit and the kind with no detail. The plain spellings
 * accept a NULL diagnostic (the C analogue of Zig's
 * compatibility-diagnostic variants); there is no thread-local
 * diagnostic in C. */

typedef DxilDiagKind DxilTranslateStatus;

/* Maps an emit status to its translate status, exhaustively over the
 * emit set. DXIL_OK is not a valid input (success returns before any
 * mapping); it defensively maps to InvalidIr. */
DxilTranslateStatus dxil_translate_map_emit_error(DxilStatus status);

bool dxil_translate_with_diagnostic(const char *source,
    size_t source_len, uint8_t *out, size_t out_len, size_t *written,
    DxilDiagnostic *diagnostic, DxilTranslateStatus *out_error);
bool dxil_translate_toolchain_with_diagnostic(const char *source,
    size_t source_len, uint8_t *out, size_t out_len,
    const DxilToolchainConfig *config, const char *cache_dir,
    size_t *written, DxilDiagnostic *diagnostic,
    DxilTranslateStatus *out_error);
bool dxil_translate(const char *source, size_t source_len, uint8_t *out,
    size_t out_len, size_t *written, DxilDiagnostic *diagnostic,
    DxilTranslateStatus *out_error);
bool dxil_translate_toolchain(const char *source, size_t source_len,
    uint8_t *out, size_t out_len, const DxilToolchainConfig *config,
    const char *cache_dir, size_t *written,
    DxilDiagnostic *diagnostic, DxilTranslateStatus *out_error);

#endif /* DOE_DXIL_TRANSLATE_H */
