/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_diagnostic.h"

#include <stdio.h>
#include <string.h>

void dxil_diagnostic_clear(DxilDiagnostic *diagnostic) {
    if (diagnostic == NULL) {
        return;
    }
    diagnostic->message[0] = '\0';
    diagnostic->message_len = 0;
    diagnostic->context[0] = '\0';
    diagnostic->context_len = 0;
    diagnostic->stage = DXIL_DIAG_STAGE_NONE;
    diagnostic->has_kind = false;
    diagnostic->kind = DXIL_DIAG_INVALID_IR;
    diagnostic->line = 0;
    diagnostic->column = 0;
}

bool dxil_diagnostic_kind_from_status(DxilStatus status,
    DxilDiagKind *out_kind) {
    switch (status) {
        case DXIL_OK:
            return false;
        case DXIL_OUT_OF_MEMORY:
            *out_kind = DXIL_DIAG_OUT_OF_MEMORY;
            return true;
        case DXIL_OUTPUT_TOO_LARGE:
            *out_kind = DXIL_DIAG_OUTPUT_TOO_LARGE;
            return true;
        case DXIL_UNSUPPORTED_CONSTRUCT:
            *out_kind = DXIL_DIAG_UNSUPPORTED_CONSTRUCT;
            return true;
        case DXIL_INVALID_IR:
            *out_kind = DXIL_DIAG_INVALID_IR;
            return true;
        case DXIL_UNSUPPORTED_BUILTIN:
            *out_kind = DXIL_DIAG_UNSUPPORTED_BUILTIN;
            return true;
        case DXIL_SHADER_TOOLCHAIN_UNAVAILABLE:
            *out_kind = DXIL_DIAG_SHADER_TOOLCHAIN_UNAVAILABLE;
            return true;
    }
    return false;
}

const char *dxil_diagnostic_stage_name(DxilDiagnosticStage stage) {
    switch (stage) {
        case DXIL_DIAG_STAGE_NONE:
            return "none";
        case DXIL_DIAG_STAGE_PARSER:
            return "parser";
        case DXIL_DIAG_STAGE_SEMA:
            return "sema";
        case DXIL_DIAG_STAGE_IR_BUILDER:
            return "ir_builder";
        case DXIL_DIAG_STAGE_IR_VALIDATE:
            return "ir_validate";
        case DXIL_DIAG_STAGE_IR_TRANSFORM:
            return "ir_transform";
        case DXIL_DIAG_STAGE_MSL_EMIT:
            return "msl_emit";
        case DXIL_DIAG_STAGE_HLSL_EMIT:
            return "hlsl_emit";
        case DXIL_DIAG_STAGE_SPIRV_EMIT:
            return "spirv_emit";
        case DXIL_DIAG_STAGE_DXIL_EMIT:
            return "dxil_emit";
        case DXIL_DIAG_STAGE_CSL_EMIT:
            return "csl_emit";
    }
    return "none";
}

const char *dxil_diagnostic_kind_name(DxilDiagKind kind) {
    switch (kind) {
        case DXIL_DIAG_INVALID_WGSL:
            return "InvalidWgsl";
        case DXIL_DIAG_INVALID_IR:
            return "InvalidIr";
        case DXIL_DIAG_DUPLICATE_SYMBOL:
            return "DuplicateSymbol";
        case DXIL_DIAG_INVALID_ATTRIBUTE:
            return "InvalidAttribute";
        case DXIL_DIAG_INVALID_TYPE:
            return "InvalidType";
        case DXIL_DIAG_OUTPUT_TOO_LARGE:
            return "OutputTooLarge";
        case DXIL_DIAG_OUT_OF_MEMORY:
            return "OutOfMemory";
        case DXIL_DIAG_SHADER_TOOLCHAIN_UNAVAILABLE:
            return "ShaderToolchainUnavailable";
        case DXIL_DIAG_UNEXPECTED_TOKEN:
            return "UnexpectedToken";
        case DXIL_DIAG_TYPE_MISMATCH:
            return "TypeMismatch";
        case DXIL_DIAG_UNKNOWN_IDENTIFIER:
            return "UnknownIdentifier";
        case DXIL_DIAG_UNKNOWN_TYPE:
            return "UnknownType";
        case DXIL_DIAG_UNSUPPORTED_BUILTIN:
            return "UnsupportedBuiltin";
        case DXIL_DIAG_UNSUPPORTED_CONSTRUCT:
            return "UnsupportedConstruct";
        case DXIL_DIAG_UNSUPPORTED_PATTERN:
            return "UnsupportedPattern";
        case DXIL_DIAG_UNSUPPORTED_WGSL:
            return "UnsupportedWgsl";
    }
    return "InvalidIr";
}

static void record_context(DxilDiagnostic *diagnostic,
    const char *source, size_t source_len, size_t offset) {
    size_t line_start = 0;
    size_t line_end = 0;
    size_t token_rel = 0;
    size_t snippet_start = 0;
    size_t snippet_len = 0;
    size_t i = 0;
    uint32_t line = 1;
    uint32_t column = 1;
    diagnostic->line = 0;
    diagnostic->column = 0;
    diagnostic->context[0] = '\0';
    diagnostic->context_len = 0;
    if (source == NULL || offset > source_len) {
        return;
    }
    for (i = 0; i < offset; i++) {
        if (source[i] == '\n') {
            line += 1;
            column = 1;
            line_start = i + 1;
        } else {
            column += 1;
        }
    }
    line_end = offset;
    while (line_end < source_len && source[line_end] != '\n'
        && source[line_end] != '\r') {
        line_end += 1;
    }
    token_rel = offset - line_start;
    if (line_end - line_start > DXIL_DIAGNOSTIC_CONTEXT_CAP
        && token_rel > DXIL_DIAGNOSTIC_CONTEXT_CAP / 2) {
        snippet_start = token_rel - DXIL_DIAGNOSTIC_CONTEXT_CAP / 2;
        if (snippet_start + DXIL_DIAGNOSTIC_CONTEXT_CAP
            > line_end - line_start) {
            snippet_start = line_end - line_start
                - DXIL_DIAGNOSTIC_CONTEXT_CAP;
        }
    }
    snippet_len = line_end - line_start - snippet_start;
    if (snippet_len > DXIL_DIAGNOSTIC_CONTEXT_CAP) {
        snippet_len = DXIL_DIAGNOSTIC_CONTEXT_CAP;
    }
    memcpy(diagnostic->context, source + line_start + snippet_start,
        snippet_len);
    /* CAP counts source bytes; the record reserves one extra terminator byte. */
    diagnostic->context[snippet_len] = '\0';
    diagnostic->context_len = snippet_len;
    diagnostic->line = line;
    diagnostic->column = column;
}

void dxil_diagnostic_set(DxilDiagnostic *diagnostic,
    DxilDiagnosticStage stage, DxilDiagKind kind, const char *source,
    size_t source_len, size_t offset) {
    int written = 0;
    if (diagnostic == NULL) {
        return;
    }
    diagnostic->stage = stage;
    diagnostic->has_kind = true;
    diagnostic->kind = kind;
    record_context(diagnostic, source, source_len, offset);
    if (diagnostic->line != 0 && diagnostic->context_len != 0) {
        written = snprintf(diagnostic->message,
            sizeof(diagnostic->message), "%s: %s at %u:%u near `%.*s`",
            dxil_diagnostic_stage_name(stage),
            dxil_diagnostic_kind_name(kind), diagnostic->line,
            diagnostic->column, (int)diagnostic->context_len,
            diagnostic->context);
    } else {
        written = snprintf(diagnostic->message,
            sizeof(diagnostic->message), "%s: %s",
            dxil_diagnostic_stage_name(stage),
            dxil_diagnostic_kind_name(kind));
    }
    if (written < 0) {
        diagnostic->message[0] = '\0';
        diagnostic->message_len = 0;
        return;
    }
    if ((size_t)written >= sizeof(diagnostic->message)) {
        diagnostic->message_len = sizeof(diagnostic->message) - 1;
    } else {
        diagnostic->message_len = (size_t)written;
    }
}

void dxil_diagnostic_set_detail(DxilDiagnostic *diagnostic,
    DxilDiagnosticStage stage, DxilDiagKind kind, const char *detail) {
    int written = 0;
    if (diagnostic == NULL) {
        return;
    }
    diagnostic->stage = stage;
    diagnostic->has_kind = true;
    diagnostic->kind = kind;
    diagnostic->line = 0;
    diagnostic->column = 0;
    diagnostic->context[0] = '\0';
    diagnostic->context_len = 0;
    written = snprintf(diagnostic->message, sizeof(diagnostic->message),
        "%s: %s: %s", dxil_diagnostic_stage_name(stage),
        dxil_diagnostic_kind_name(kind),
        detail == NULL ? "" : detail);
    if (written < 0) {
        diagnostic->message[0] = '\0';
        diagnostic->message_len = 0;
        return;
    }
    if ((size_t)written >= sizeof(diagnostic->message)) {
        diagnostic->message_len = sizeof(diagnostic->message) - 1;
    } else {
        diagnostic->message_len = (size_t)written;
    }
}
