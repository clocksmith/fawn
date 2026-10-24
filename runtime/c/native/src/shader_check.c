/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "doe_native_shader_check.h"
#include <stdio.h>
#include <string.h>

typedef struct check_diagnostic {
    char message[sizeof(((DoeCompilerDiagnostic *)0)->message)];
    const char *stage, *kind;
    uint32_t line, column;
} check_diagnostic;

/* The retained ABI reports a check's diagnostic through separate copy calls, so
 * the latest result belongs to the calling thread until its next check. */
static _Thread_local check_diagnostic last_check;

static doe_shader_target check_target(void)
{
#if defined(__APPLE__)
    return DOE_SHADER_METAL;
#else
    return DOE_SHADER_SPIRV;
#endif
}

/* Stage names match the retained provider's parser/sema split. */
static const char *compiler_stage(DoeCompilerStatus status)
{
    switch (status) {
    case DOE_COMPILER_PARSE_ERROR: return "parser";
    case DOE_COMPILER_SEMANTIC_ERROR: return "sema";
    case DOE_COMPILER_UNSUPPORTED: return "unsupported";
    case DOE_COMPILER_OUTPUT_TOO_SMALL: return "emit";
    default: return "native_check";
    }
}

static const char *failure_kind(const doe_shader_error *error)
{
    if (error->status == DOE_SHADER_OUT_OF_MEMORY) return "OutOfMemory";
    if (error->status == DOE_SHADER_OVERFLOW) return "CapacityExceeded";
    switch (error->compiler_status) {
    case DOE_COMPILER_PARSE_ERROR: return "ParseError";
    case DOE_COMPILER_SEMANTIC_ERROR: return "SemanticError";
    case DOE_COMPILER_UNSUPPORTED: return "Unsupported";
    case DOE_COMPILER_OUTPUT_TOO_SMALL: return "CapacityExceeded";
    default: return "InvalidInput";
    }
}

static void reject(const char *stage, const char *kind, const char *message)
{
    last_check = (check_diagnostic){.stage=stage, .kind=kind};
    snprintf(last_check.message, sizeof(last_check.message), "%s", message);
}

uint32_t doeNativeCheckShaderSource(const char *source, size_t size)
{
    last_check = (check_diagnostic){0};
    if (!source && size) {
        reject("native_check", "InvalidInput", "shader check failed: WGSL source pointer is null");
        return 0;
    }
    if (size > 65536) {
        reject("native_check", "CapacityExceeded", "WGSL source exceeds admitted capacity");
        return 0;
    }
    doe_shader_error error = {0};
    if (native_check_shader_module(check_target(), source, size, &error) == DOE_SHADER_OK) return 1;
    reject(compiler_stage(error.compiler_status), failure_kind(&error),
        error.diagnostic.message[0] ? error.diagnostic.message : "C shader validation failed");
    last_check.line = error.diagnostic.line;
    last_check.column = error.diagnostic.column;
    return 0;
}

static size_t copy_text(const char *text, char *out, size_t out_size)
{
    size_t length = text ? strlen(text) : 0;
    if (!out || !out_size) return length;
    size_t copied = length < out_size - 1 ? length : out_size - 1;
    if (copied) memcpy(out, text, copied);
    out[copied] = '\0';
    return length;
}

size_t doeNativeCopyLastErrorMessage(char *out, size_t out_size) { return copy_text(last_check.message, out, out_size); }
size_t doeNativeCopyLastErrorStage(char *out, size_t out_size) { return copy_text(last_check.stage, out, out_size); }
size_t doeNativeCopyLastErrorKind(char *out, size_t out_size) { return copy_text(last_check.kind, out, out_size); }
uint32_t doeNativeGetLastErrorLine(void) { return last_check.line; }
uint32_t doeNativeGetLastErrorColumn(void) { return last_check.column; }
