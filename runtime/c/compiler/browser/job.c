/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "job.h"
#include "../src/compile_internal.h"
#include "../src/rewrite.h"
#include <stdlib.h>
#include <string.h>

static char *append(char *output, const char *text) {
    const size_t size = strlen(text);
    memcpy(output, text, size);
    return output + size;
}

static char *unsigned_decimal(char *output, size_t value) {
    char reverse[3u * sizeof(size_t)];
    size_t count = 0u;
    do { reverse[count++] = (char)('0' + value % 10u); value /= 10u; } while (value != 0u);
    while (count != 0u) *output++ = reverse[--count];
    return output;
}

static char *quoted(char *output, const char *text, size_t length) {
    static const char hex[] = "0123456789abcdef";
    *output++ = '"';
    for (size_t i = 0u; i < length; ++i) {
        const unsigned char byte = (unsigned char)text[i];
        if (byte == '"' || byte == '\\') { *output++ = '\\'; *output++ = (char)byte; }
        else if (byte < 32u) {
            output = append(output, "\\u00");
            *output++ = hex[byte >> 4u];
            *output++ = hex[byte & 15u];
        } else *output++ = (char)byte;
    }
    *output++ = '"';
    return output;
}

static char *result_json(const DoeCompilerDiagnostic *diagnostic, const char *wgsl,
                         size_t length, size_t rewrites, size_t *output_length) {
    const bool success = diagnostic->status == DOE_COMPILER_OK;
    const size_t text_size = success ? length : strlen(diagnostic->message);
    if (text_size > (SIZE_MAX - 512u) / 6u) return NULL;
    char *result = malloc(text_size * 6u + 512u);
    if (result == NULL) return NULL;
    char *out = append(result, "{\"schemaVersion\":1,\"ok\":");
    if (success) {
        out = append(out, "true,\"wgsl\":");
        out = quoted(out, wgsl, length);
        out = append(out, ",\"rewrites\":");
        out = unsigned_decimal(out, rewrites);
        out = append(out, ",\"diagnostics\":[]}");
    } else {
        out = append(out, "false,\"diagnostic\":{\"stage\":");
        const char *stage = diagnostic->status == DOE_COMPILER_PARSE_ERROR ||
            diagnostic->status == DOE_COMPILER_UNSUPPORTED ? "parser" : "sema";
        out = quoted(out, stage, strlen(stage));
        out = append(out, ",\"message\":");
        out = quoted(out, diagnostic->message, text_size);
        out = append(out, ",\"byteOffset\":");
        out = unsigned_decimal(out, diagnostic->offset);
        out = append(out, "}}");
    }
    *out = '\0';
    *output_length = (size_t)(out - result);
    return result;
}

/* Count with the canonical lexer before sizing the caller-owned pools. Comments
 * and whitespace consume source bytes without inflating every semantic pool. */
static bool token_budget(Source *source, size_t *count) {
    CompilerLimits limits = {.tokens = (uint32_t)source->length};
    Token *tokens = malloc((source->length + 1u) * sizeof(*tokens));
    if (tokens == NULL) return source_error(source, DOE_COMPILER_INPUT_ERROR, 0u, "browser token allocation failed");
    CompilerWorkspace temporary = {.tokens = tokens};
    source->limits = &limits;
    const bool valid = lex_source(source, &temporary);
    *count = temporary.token_count + 1u;
    free(tokens);
    source->limits = NULL;
    return valid;
}

static DoeCompilerStageCapacities capacities(size_t tokens, size_t length) {
    return (DoeCompilerStageCapacities){
        .module = {
            .typed = {
                .source_bytes = length, .tokens = tokens, .statements = tokens,
                .expressions = tokens, .types = tokens, .members = tokens,
                .resources = tokens / 8u + 1u, .access_steps = tokens * 2u,
                .constants = tokens, .constant_edges = tokens * 4u,
                .ir_instructions = tokens * 4u, .ir_operands = tokens * 2u,
                .target_declarations = 1u, .traversal_items = tokens + 256u,
                .words = 0u, .expression_depth = 64u, .control_depth = 32u,
                .type_syntax_depth = 64u
            },
            .entries = tokens / 13u + 1u, .entry_inputs = tokens,
            .entry_references = tokens * 8u
        },
        .interface_attributes = tokens, .stage_interfaces = tokens
    };
}

void compiler_wgsl_job(const char *bytes, size_t length, bool enabled, WgslJobResult *result) {
    *result = (WgslJobResult){0};
    Source source = {.source = bytes, .length = length, .diagnostic = &result->diagnostic};
    void *memory = NULL;
    WgslEdit *edits = NULL;
    char *rewritten = NULL;
    size_t tokens = 0u, count = 0u, rewritten_length = length, base = 0u, size = 0u;
    if (bytes == NULL || length > DOE_COMPILER_MAX_SOURCE_BYTES) {
        (void)source_error(&source, DOE_COMPILER_INPUT_ERROR, 0u, "WGSL source exceeds 65536 bytes or is NULL");
        goto finish;
    }
    if (!token_budget(&source, &tokens)) goto finish;
    const DoeCompilerStageCapacities caps = capacities(tokens, length);
    CompilerLimits limits;
    if (!stage_layout(&caps, &limits, &base, &size) || size > 48u * 1024u * 1024u) {
        (void)source_error(&source, DOE_COMPILER_INPUT_ERROR, 0u, "browser compiler workspace exceeds its memory budget");
        goto finish;
    }
    memory = malloc(size);
    edits = enabled ? malloc(tokens * sizeof(*edits)) : NULL;
    if (memory == NULL || (enabled && edits == NULL)) {
        (void)source_error(&source, DOE_COMPILER_INPUT_ERROR, 0u, "browser compiler workspace allocation failed");
        goto finish;
    }
    CompilerWorkspace *compiler = memory;
    compiler_job_bind(compiler, memory, &limits);
    stage_bind(&source, memory, &caps, base);
    NumericEnvironment environment;
    const bool analyzed = compiler_analyze(&source, compiler, &environment);
    const bool planned = analyzed && (!enabled ||
        compiler_rewrite_plan(&source, edits, tokens, &count, &rewritten_length));
    const bool restored = numeric_environment_end(&source, &environment);
    if (!planned || !restored) goto finish;
    rewritten = malloc(rewritten_length + 1u);
    if (rewritten == NULL) {
        (void)source_error(&source, DOE_COMPILER_INPUT_ERROR, 0u, "WGSL output allocation failed");
        goto finish;
    }
    if (enabled) compiler_rewrite_publish(&source, edits, count, rewritten);
    else memcpy(rewritten, bytes, rewritten_length);
    rewritten[rewritten_length] = '\0';
    result->wgsl = rewritten;
    result->length = rewritten_length;
    result->rewrites = count;
finish:;
    free(edits);
    free(memory);
}

char *compiler_browser_job(const char *bytes, size_t length, bool enabled, size_t *output_length) {
    *output_length = 0u;
    WgslJobResult result;
    compiler_wgsl_job(bytes, length, enabled, &result);
    char *json = result_json(&result.diagnostic, result.wgsl, result.length, result.rewrites, output_length);
    free(result.wgsl);
    return json;
}
