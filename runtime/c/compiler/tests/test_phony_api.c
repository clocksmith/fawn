/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_text.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DoeCompilerTextCapacities capacities = {
    .stages = {.module = {.typed = {.source_bytes = 8192u, .tokens = 2048u,
        .statements = 64u, .expressions = 512u, .types = 64u, .members = 32u,
        .resources = 8u, .access_steps = 128u, .constants = 128u,
        .constant_edges = 256u, .ir_instructions = 1024u, .ir_operands = 1024u,
        .target_declarations = 0u, .traversal_items = 128u, .words = 0u,
        .expression_depth = 32u, .control_depth = 8u, .type_syntax_depth = 32u},
        .entries = 4u, .entry_inputs = 16u, .entry_references = 32u},
        .interface_attributes = 32u, .stage_interfaces = 32u}, .text_bytes = 0u
};
static const char source[] =
    "@group(0) @binding(1) var<storage,read> data:array<u32>;"
    "@group(0) @binding(2) var tex:texture_2d<f32>;"
    "@group(0) @binding(3) var smp:sampler;"
    "@group(0) @binding(4) var<storage,read> unused:array<u32>;"
    "fn helper(){_ = &data;}"
    "@fragment fn shade()->@location(0) vec4f{"
    "_ = tex;_ = smp;return vec4f();helper();}"
    "@compute @workgroup_size(1) fn empty(){}";

static DoeCompilerStatus measure(void *storage, size_t bytes, const char *text,
    const char *entry, DoeCompilerTextResult *result) {
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NAME, 0u,
        entry, strlen(entry)};
    return doe_compiler_compile_text(10u, DOE_COMPILER_TARGET_METAL,
        &capacities, sizeof(capacities), storage, bytes, text, strlen(text),
        &selection, sizeof(selection), DOE_COMPILER_TEXT_REFLECTION | DOE_COMPILER_TEXT_MEASURE,
        NULL, 0u, result, sizeof(*result));
}

int main(void) {
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_text_requirements(10u, DOE_COMPILER_TARGET_METAL,
        &capacities, sizeof(capacities), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *storage = malloc(requirements.size_bytes);
    assert(storage != NULL);
    DoeCompilerTextResult result;
    errno = EBUSY;
    assert(measure(storage, requirements.size_bytes, source, "shade", &result) == DOE_COMPILER_OK);
    assert(errno == EBUSY && result.required_bindings == 3u && result.required_entries == 1u);
    assert(result.stages.resources.module.required.resources == 4u);
    assert(result.published_text_bytes == 0u && result.published_bindings == 0u);
    assert(measure(storage, requirements.size_bytes, source, "empty", &result) == DOE_COMPILER_OK);
    assert(result.required_bindings == 0u && result.required_entries == 1u);
    assert(measure(storage, requirements.size_bytes,
        "@compute @workgroup_size(1) fn empty(){_ = missing;}", "empty", &result) == DOE_COMPILER_SEMANTIC_ERROR);
    assert(result.published_bindings == 0u && errno == EBUSY);
    assert(measure(storage, requirements.size_bytes, source, "shade", &result) == DOE_COMPILER_OK);
    assert(result.required_bindings == 3u);
    free(storage);

    uint32_t words[4096], saved[4096];
    DoeCompilerResult fixed;
    const char good[] = "@compute @workgroup_size(1) fn main(){_ = max(2u,3u);}";
    assert(doe_compiler_compile(1u, good, strlen(good), words, 4096u, &fixed, sizeof(fixed)) == DOE_COMPILER_OK);
    const size_t count = fixed.word_count;
    memset(words, 0xa5, sizeof(words)); memcpy(saved, words, sizeof(words));
    assert(doe_compiler_compile(1u, good, strlen(good), words, count - 1u,
        &fixed, sizeof(fixed)) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(fixed.word_count == 0u && memcmp(words, saved, sizeof(words)) == 0 && errno == EBUSY);
    const char bad[] = "@compute @workgroup_size(1) fn main(){_ += 1u;}";
    assert(doe_compiler_compile(1u, bad, strlen(bad), words, 4096u,
        &fixed, sizeof(fixed)) == DOE_COMPILER_PARSE_ERROR);
    assert(fixed.word_count == 0u && memcmp(words, saved, sizeof(words)) == 0 && errno == EBUSY);
    puts("Phony API: exact static entry closure, discarded handles/pointers, workspace reuse and atomic failure passed.");
    return 0;
}
