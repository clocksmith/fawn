/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_DIAGNOSTIC_H
#define DOE_DXIL_DIAGNOSTIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dxil_error.h"

/* Allocation-free diagnostic record, mirroring
 * pipeline/diagnostic.zig: a fixed message buffer, a fixed source
 * context buffer, a compilation stage, an optional error kind, and a
 * source line/column. There is deliberately NO thread-local instance:
 * every emitter takes an explicit DxilDiagnostic pointer, and NULL
 * means "report the status, drop the message", which is the C analogue
 * of Zig's compatibility-diagnostic variants. Message text is
 * formatted as Zig formats it: "{stage}: {kind}" or, with a detail,
 * "{stage}: {kind}: {detail}". The buffer is always NUL-terminated;
 * overlong text is truncated, never overflows.
 *
 * The kind set is the translate error set from diagnostic.zig, not
 * the narrower emit status set: in Zig the diagnostic's kind field is
 * a TranslateError, and emit errors coerce into it by name. Emit
 * call sites map their DxilStatus through
 * dxil_diagnostic_kind_from_status first. */

#define DXIL_DIAGNOSTIC_MESSAGE_CAP 256u
#define DXIL_DIAGNOSTIC_CONTEXT_CAP 96u

typedef enum DxilDiagnosticStage {
    DXIL_DIAG_STAGE_NONE,
    DXIL_DIAG_STAGE_PARSER,
    DXIL_DIAG_STAGE_SEMA,
    DXIL_DIAG_STAGE_IR_BUILDER,
    DXIL_DIAG_STAGE_IR_VALIDATE,
    DXIL_DIAG_STAGE_IR_TRANSFORM,
    DXIL_DIAG_STAGE_MSL_EMIT,
    DXIL_DIAG_STAGE_HLSL_EMIT,
    DXIL_DIAG_STAGE_SPIRV_EMIT,
    DXIL_DIAG_STAGE_DXIL_EMIT,
    DXIL_DIAG_STAGE_CSL_EMIT
} DxilDiagnosticStage;

/* Mirrors analysis.TranslateError, in Zig declaration order. */
typedef enum DxilDiagKind {
    DXIL_DIAG_INVALID_WGSL,
    DXIL_DIAG_INVALID_IR,
    DXIL_DIAG_DUPLICATE_SYMBOL,
    DXIL_DIAG_INVALID_ATTRIBUTE,
    DXIL_DIAG_INVALID_TYPE,
    DXIL_DIAG_OUTPUT_TOO_LARGE,
    DXIL_DIAG_OUT_OF_MEMORY,
    DXIL_DIAG_SHADER_TOOLCHAIN_UNAVAILABLE,
    DXIL_DIAG_UNEXPECTED_TOKEN,
    DXIL_DIAG_TYPE_MISMATCH,
    DXIL_DIAG_UNKNOWN_IDENTIFIER,
    DXIL_DIAG_UNKNOWN_TYPE,
    DXIL_DIAG_UNSUPPORTED_BUILTIN,
    DXIL_DIAG_UNSUPPORTED_CONSTRUCT,
    DXIL_DIAG_UNSUPPORTED_PATTERN,
    DXIL_DIAG_UNSUPPORTED_WGSL
} DxilDiagKind;

typedef struct DxilDiagnostic {
    char message[DXIL_DIAGNOSTIC_MESSAGE_CAP];
    size_t message_len;
    /* CAP counts source bytes; reserve a separate C string terminator. */
    char context[DXIL_DIAGNOSTIC_CONTEXT_CAP + 1u];
    size_t context_len;
    DxilDiagnosticStage stage;
    bool has_kind;
    DxilDiagKind kind;
    uint32_t line;
    uint32_t column;
} DxilDiagnostic;

void dxil_diagnostic_clear(DxilDiagnostic *diagnostic);

/* Maps an emit status to its diagnostic kind by name. DXIL_OK has no
 * kind and returns false; every other status maps and returns true. */
bool dxil_diagnostic_kind_from_status(DxilStatus status,
    DxilDiagKind *out_kind);

/* Records stage plus kind, computing line, column, and the context
 * snippet from source and a byte offset, exactly as Zig's
 * recordSourceContext does: line and column count from 1, the snippet
 * is the source line containing the offset, windowed to 96 bytes
 * around the token when the line is longer. A NULL source or an
 * out-of-range offset records no location and no context. The message
 * becomes "{stage}: {kind}" plus, when a location and context exist,
 * " at {line}:{column} near `{context}`". NULL diagnostic is a
 * no-op. */
void dxil_diagnostic_set(DxilDiagnostic *diagnostic,
    DxilDiagnosticStage stage, DxilDiagKind kind, const char *source,
    size_t source_len, size_t offset);

/* Records stage plus kind plus a detail string. The message becomes
 * "{stage}: {kind}: {detail}". This is Zig's setLastErrorDetail
 * shape, and it is how the translate layer preserves Zig's
 * double-prefix diagnostic when it passes an emission message on as
 * a detail (see dxil_translate.h). NULL diagnostic is a no-op. */
void dxil_diagnostic_set_detail(DxilDiagnostic *diagnostic,
    DxilDiagnosticStage stage, DxilDiagKind kind, const char *detail);

/* Stage tag name (Zig's @tagName output) and kind error name (Zig's
 * @errorName output, e.g. "InvalidIr"). Never NULL. */
const char *dxil_diagnostic_stage_name(DxilDiagnosticStage stage);
const char *dxil_diagnostic_kind_name(DxilDiagKind kind);

#endif /* DOE_DXIL_DIAGNOSTIC_H */
