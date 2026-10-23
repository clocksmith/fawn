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
    if (length > workspace->limits.source_bytes)
        return source_error(context, DOE_COMPILER_INPUT_ERROR, workspace->limits.extended ? workspace->limits.source_bytes : 0u, "source byte capacity exceeded");
    if (words == NULL && capacity != 0u)
        return source_error(context, DOE_COMPILER_INPUT_ERROR, 0u, "NULL output requires zero capacity");
    if (length == 0u)
        return source_error(context, DOE_COMPILER_INPUT_ERROR, 0u, "source is empty");
    const char *nul = memchr(source, '\0', length);
    if (nul != NULL)
        return source_error(context, DOE_COMPILER_INPUT_ERROR, (size_t)(nul - source),
                            "embedded NUL byte in source");
    if (!lex_source(context, &workspace->tokens) ||
        !parse_tokens(context, &workspace->tokens, &workspace->syntax, allow_uniforms)) return false;
    /* Tokens are dead here. Validation starts the other union member and does
     * not inspect stale token bytes. Syntax owns declaration names and validated source spans for expressions. */
    IrProgram *program = &workspace->program;
    Spirv *output = &workspace->output;
    if (!validate_syntax(context, &workspace->syntax, program, workspace->scope) ||
        !emit_spirv(context, program, output, workspace->target_values)) return false;
    if (capacity < output->count)
        return source_error(context, DOE_COMPILER_OUTPUT_TOO_SMALL, length,
                            "caller output word capacity is too small");
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
    LegacyWorkspace workspace;
    compiler_legacy_bind(&workspace, false);
    context.limits = &workspace.job.limits;
    if (compile_pipeline(&context, &workspace.job, false, words, capacity)) {
        reflect_program(&workspace.job.program, &result->reflection);
        result->word_count = workspace.job.output.count;
    }
    return result->diagnostic.status;
}

_Static_assert(_Alignof(LegacyWorkspace) <= _Alignof(max_align_t),
               "malloc must provide sufficient compiler workspace alignment");
DoeCompilerStatus doe_compiler_workspace_requirements(
    uint32_t abi_version, DoeCompilerWorkspaceRequirements *requirements,
    size_t requirements_size) {
    if (requirements == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (abi_version != DOE_COMPILER_ABI_VERSION_2 || requirements_size != sizeof(*requirements))
        return DOE_COMPILER_ABI_MISMATCH;
    requirements->size_bytes = sizeof(LegacyWorkspace);
    requirements->alignment_bytes = _Alignof(LegacyWorkspace);
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
    if (workspace == NULL || workspace_size < sizeof(LegacyWorkspace) ||
        (uintptr_t)workspace % _Alignof(LegacyWorkspace) != 0u) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u,
                           "workspace must satisfy queried size and alignment");
        return result->diagnostic.status;
    }
    LegacyWorkspace *storage = workspace;
    compiler_legacy_bind(storage, true);
    CompilerWorkspace *job = &storage->job;
    context.limits = &job->limits;
    if (compile_pipeline(&context, job, true, words, capacity)) {
        reflect_program_v2(&job->program, &result->reflection);
        result->word_count = job->output.count;
    }
    return result->diagnostic.status;
}

DoeCompilerStatus doe_compiler_job_requirements(
    uint32_t version, const DoeCompilerJobCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    if (requirements == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != DOE_COMPILER_JOB_ABI_VERSION || capacities_size != sizeof(*capacities) ||
        requirements_size != sizeof(*requirements)) return DOE_COMPILER_ABI_MISMATCH;
    CompilerLimits limits;
    size_t bytes;
    if (!compiler_job_layout(capacities, &limits, &bytes)) return DOE_COMPILER_INPUT_ERROR;
    requirements->size_bytes = bytes;
    requirements->alignment_bytes = _Alignof(max_align_t);
    return DOE_COMPILER_OK;
}
DoeCompilerStatus doe_compiler_compile_job(
    uint32_t version, const DoeCompilerJobCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    uint32_t *words, size_t capacity, DoeCompilerResultV2 *result, size_t result_size) {
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != DOE_COMPILER_JOB_ABI_VERSION || capacities_size != sizeof(*capacities) ||
        result_size != sizeof(*result)) return DOE_COMPILER_ABI_MISMATCH;
    memset(result, 0, sizeof(*result));
    Source context = { .source = source, .length = length, .diagnostic = &result->diagnostic };
    CompilerLimits limits;
    size_t bytes;
    if (!compiler_job_layout(capacities, &limits, &bytes)) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "invalid job capacities or layout overflow");
        return result->diagnostic.status;
    }
    if (workspace == NULL || workspace_size < bytes ||
        (uintptr_t)workspace % _Alignof(max_align_t) != 0u) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "workspace must satisfy job size and alignment");
        return result->diagnostic.status;
    }
    CompilerWorkspace *job = workspace;
    compiler_job_bind(job, workspace, &limits);
    context.limits = &job->limits;
    if (compile_pipeline(&context, job, true, words, capacity)) {
        reflect_program_v2(&job->program, &result->reflection);
        result->word_count = job->output.count;
    }
    return result->diagnostic.status;
}
