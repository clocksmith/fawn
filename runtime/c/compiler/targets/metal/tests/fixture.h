/* Shared developer-only source preparation and atomic publication checks. */
#ifndef DOE_METAL_TEST_FIXTURE_H
#define DOE_METAL_TEST_FIXTURE_H
/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../metal.h"
#include "../../../src/numeric.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DoeCompilerModuleCapacities capacities = {
    .typed = {.source_bytes = 16384u, .tokens = 2048u, .statements = 256u, .expressions = 1024u,
        .types = 64u, .members = 128u, .resources = 32u, .access_steps = 1024u,
        .constants = 256u, .constant_edges = 512u, .ir_instructions = 4096u,
        .ir_operands = 1024u, .target_declarations = 2048u, .traversal_items = 193u,
        .words = 32768u, .expression_depth = 32u, .control_depth = 16u, .type_syntax_depth = 32u},
    .entries = 8u, .entry_inputs = 32u, .entry_references = 256u
};
typedef struct {
    void *storage;
    CompilerWorkspace *module;
    Source source;
    DoeCompilerDiagnostic diagnostic;
} Fixture;
static Fixture *prepare(const char *text) {
    Fixture *f = calloc(1u, sizeof(*f));
    assert(f != NULL);
    CompilerLimits limits;
    size_t bytes = 0u;
    assert(compiler_module_layout(&capacities, &limits, &bytes));
    f->storage = malloc(bytes);
    assert(f->storage != NULL);
    f->module = f->storage;
    compiler_job_bind(f->module, f->storage, &limits);
    f->source = (Source){.source = text, .length = strlen(text), .diagnostic = &f->diagnostic,
        .limits = &f->module->limits, .compiler = f->module};
    bool okay = lex_source(&f->source, f->module) && parse_tokens(&f->source, f->module);
    if (okay) {
        for (uint32_t i = 0u; i < f->module->expression_count; ++i)
            f->module->expression_references[i] = (SourceReference){SOURCE_NONE, NO_ID};
        for (uint32_t i = 0u; i < f->module->statement_count; ++i)
            f->module->statement_references[i] = (SourceReference){SOURCE_NONE, NO_ID};
        NumericEnvironment environment;
        okay = numeric_environment_begin(&f->source, &environment);
        if (okay) {
            okay = validate_syntax(&f->source, f->module);
            okay = numeric_environment_end(&f->source, &environment) && okay;
        }
    }
    if (!okay) fprintf(stderr, "source failed at %zu: %s\n%s\n", f->diagnostic.offset, f->diagnostic.message, text);
    assert(okay);
    return f;
}
static void release(Fixture *f) {
    free(f->storage);
    free(f);
}
static char *emit(Fixture *f, uint32_t selected) {
    DoeMetalResult measure;
    errno = EDOM;
    const DoeCompilerStatus status = doe_metal_emit(&f->source, f->module, selected, NULL, 0u, &measure);
    if (status != DOE_COMPILER_OK) fprintf(stderr, "Metal rejected at %zu: %s\n", f->diagnostic.offset, f->diagnostic.message);
    assert(status == DOE_COMPILER_OK);
    assert(errno == EDOM);
    assert(measure.required_bytes != 0u && measure.written_bytes == 0u);
    char *text = malloc(measure.required_bytes + 1u);
    assert(text != NULL);
    memset(text, 0xa5, measure.required_bytes + 1u);
    DoeMetalResult result;
    assert(doe_metal_emit(&f->source, f->module, selected, text, measure.required_bytes - 1u, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.written_bytes == 0u && (unsigned char)text[0] == 0xa5u);
    assert(doe_metal_emit(&f->source, f->module, selected, text, measure.required_bytes, &result) == DOE_COMPILER_OK);
    assert(result.required_bytes == measure.required_bytes && result.written_bytes == measure.required_bytes);
    assert((unsigned char)text[measure.required_bytes] == 0xa5u);
    text[measure.required_bytes] = '\0';
    return text;
}
static void has(const char *text, const char *needle) {
    if (strstr(text, needle) == NULL) fprintf(stderr, "missing emission: %s\n", needle);
    assert(strstr(text, needle) != NULL);
}
#endif
