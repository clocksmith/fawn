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

/* All APIs use this pipeline. The wrapper alone owns result layout/admission. */
static bool compile_pipeline(Source *context, CompilerWorkspace *workspace,
                             bool allow_uniforms, uint32_t *words, size_t capacity) {
    const char *source = context->source;
    const size_t length = context->length;
    if (source == NULL && length != 0u) {
        context->length = 0u;
        return source_error(context, DOE_COMPILER_INPUT_ERROR, 0u, "NULL source requires zero length");
    }
    if (length > DOE_COMPILER_MAX_SOURCE_BYTES)
        return source_error(context, DOE_COMPILER_INPUT_ERROR, 0u, "source exceeds 65536 bytes");
    if (words == NULL && capacity != 0u)
        return source_error(context, DOE_COMPILER_INPUT_ERROR, 0u, "NULL output requires zero capacity");
    if (length == 0u)
        return source_error(context, DOE_COMPILER_INPUT_ERROR, 0u, "source is empty");
    const char *nul = memchr(source, '\0', length);
    if (nul != NULL)
        return source_error(context, DOE_COMPILER_INPUT_ERROR, (size_t)(nul - source),
                            "embedded NUL byte in source");
    if (!lex_source(context, &workspace->phase.tokens) ||
        !parse_tokens(context, &workspace->phase.tokens, &workspace->syntax, allow_uniforms)) return false;
    /* Tokens are dead here. Validation starts the other union member and does
     * not inspect stale token bytes. Syntax owns declaration names and validated source spans for expressions. */
    IrProgram *program = &workspace->phase.lowered.program;
    Spirv *output = &workspace->phase.lowered.output;
    if (!validate_syntax(context, &workspace->syntax, program) ||
        !emit_spirv(context, program, output)) return false;
    if (capacity < output->count)
        return source_error(context, DOE_COMPILER_OUTPUT_TOO_SMALL, length,
                            "output word capacity is too small; provide DOE_COMPILER_MAX_WORDS");
    /* No later operation can fail: this is the only word publication point. */
    memcpy(words, output->words, output->count * sizeof(*words));
    return true;
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
    CompilerWorkspace workspace;
    if (compile_pipeline(&context, &workspace, false, words, capacity)) {
        reflect_program(&workspace.phase.lowered.program, &result->reflection);
        result->word_count = workspace.phase.lowered.output.count;
    }
    return result->diagnostic.status;
}

_Static_assert(_Alignof(CompilerWorkspace) <= _Alignof(max_align_t),
               "malloc must provide sufficient compiler workspace alignment");
DoeCompilerStatus doe_compiler_workspace_requirements(
    uint32_t abi_version, DoeCompilerWorkspaceRequirements *requirements,
    size_t requirements_size) {
    if (requirements == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (abi_version != DOE_COMPILER_ABI_VERSION_2 || requirements_size != sizeof(*requirements))
        return DOE_COMPILER_ABI_MISMATCH;
    requirements->size_bytes = sizeof(CompilerWorkspace);
    requirements->alignment_bytes = _Alignof(CompilerWorkspace);
    return DOE_COMPILER_OK;
}

DoeCompilerStatus doe_compiler_compile_v2(
    uint32_t abi_version, void *workspace, size_t workspace_size,
    const char *source, size_t length,
    uint32_t *words, size_t capacity,
    DoeCompilerResultV2 *result, size_t result_size) {
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (abi_version != DOE_COMPILER_ABI_VERSION_2 || result_size != sizeof(*result))
        return DOE_COMPILER_ABI_MISMATCH;
    memset(result, 0, sizeof(*result));
    Source context = { .source = source, .length = length, .diagnostic = &result->diagnostic };
    if (workspace == NULL || workspace_size < sizeof(CompilerWorkspace) ||
        (uintptr_t)workspace % _Alignof(CompilerWorkspace) != 0u) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u,
                           "workspace must satisfy queried size and alignment");
        return result->diagnostic.status;
    }
    CompilerWorkspace *job = workspace;
    if (compile_pipeline(&context, job, true, words, capacity)) {
        reflect_program_v2(&job->phase.lowered.program, &result->reflection);
        result->word_count = job->phase.lowered.output.count;
    }
    return result->diagnostic.status;
}
