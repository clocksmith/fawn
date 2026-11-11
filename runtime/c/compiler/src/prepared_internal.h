/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_PREPARED_INTERNAL_H
#define DOE_COMPILER_PREPARED_INTERNAL_H
#include "override.h"
#include "compile_internal.h"
#include "../include/doe_compiler_prepared.h"

struct CompilerPrepared {
    uint64_t marker;
    size_t total_bytes, body_offset, body_bytes;
    DoeCompilerPrepareOptionsV20 options;
    DoeCompilerPreparedResultV20 result;
    Source source;
    CompilerMetadata metadata;
    SpecializationContext specialization;
    SamplingContext sampling;
    TranslationContext translation;
    uint32_t selected, flags;
    size_t closure_offset, closure_bytes, bindings_offset, entries_offset;
};
bool compiler_source_failure(DoeCompilerStatus);
void compiler_prepared_capture(CompilerPrepared *, const Source *, const CompilerMetadata *, uint32_t);
DoeCompilerStatus compiler_prepare_validation(CompilerPrepared *, void *, size_t, const char *, size_t);
DoeCompilerStatus compiler_publish_validation(CompilerPrepared *, const DoeCompilerDiagnosticOutputsV18 *, bool notices_only);
DoeCompilerStatus compiler_prepare_text(CompilerPrepared *, void *, size_t, const char *, size_t);
DoeCompilerStatus compiler_publish_text(CompilerPrepared *, const DoeCompilerTranslationOutputsV17 *, void *, size_t);
DoeCompilerStatus compiler_prepare_translation(CompilerPrepared *, void *, size_t, const char *, size_t);
#endif
