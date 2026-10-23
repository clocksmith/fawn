/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

bool source_error(Source *source, DoeCompilerStatus status, size_t offset, const char *message) {
    DoeCompilerDiagnostic *diagnostic = source->diagnostic;
    diagnostic->status = status;
    diagnostic->offset = offset;
    diagnostic->line = 1u;
    diagnostic->column = 1u;
    /* CRLF counts as one newline; lone CR and LF each count as one. */
    for (size_t i = 0; i < offset && i < source->length; ++i) {
        if (source->source[i] == '\r') {
            ++diagnostic->line;
            diagnostic->column = 1u;
        } else if (source->source[i] == '\n') {
            if (i == 0u || source->source[i - 1u] != '\r') ++diagnostic->line;
            diagnostic->column = 1u;
        } else ++diagnostic->column;
    }
    size_t message_length = strlen(message);
    if (message_length >= sizeof(diagnostic->message)) message_length = sizeof(diagnostic->message) - 1u;
    memcpy(diagnostic->message, message, message_length);
    diagnostic->message[message_length] = '\0';
    return false;
}

DoeCompilerStatus doe_compiler_compile(
    uint32_t abi_version, const char *source, size_t length,
    uint32_t *words, size_t capacity,
    DoeCompilerResult *result, size_t result_size) {
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (abi_version != DOE_COMPILER_ABI_VERSION || result_size != sizeof(*result))
        return DOE_COMPILER_ABI_MISMATCH;
    memset(result, 0, sizeof(*result));
    Source context = { .source = source, .length = length, .diagnostic = &result->diagnostic };
    if (source == NULL && length != 0u) {
        context.length = 0u;
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "NULL source requires zero length");
    } else if (length > DOE_COMPILER_MAX_SOURCE_BYTES) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "source exceeds 65536 bytes");
    } else if (words == NULL && capacity != 0u) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "NULL output requires zero capacity");
    } else if (length == 0u) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "source is empty");
    } else {
        const char *nul = memchr(source, '\0', length);
        if (nul != NULL) {
            (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, (size_t)(nul - source), "embedded NUL byte in source");
        } else {
            TokenList tokens;
            Syntax syntax;
            IrProgram program;
            Spirv output;
            if (lex_source(&context, &tokens) && parse_tokens(&context, &tokens, &syntax) &&
                validate_syntax(&context, &syntax, &program) && emit_spirv(&context, &program, &output)) {
                if (capacity < output.count) {
                    (void)source_error(&context, DOE_COMPILER_OUTPUT_TOO_SMALL, length,
                                       "output word capacity is too small; provide DOE_COMPILER_MAX_WORDS");
                } else {
                    /* This is the only publication point. No caller pointers are retained. */
                    memcpy(words, output.words, output.count * sizeof(*words));
                    reflect_program(&program, &result->reflection);
                    result->word_count = output.count;
                    result->diagnostic.status = DOE_COMPILER_OK;
                    return DOE_COMPILER_OK;
                }
            }
        }
    }
    return result->diagnostic.status;
}
