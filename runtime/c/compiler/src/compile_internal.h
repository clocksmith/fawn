/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_COMPILE_INTERNAL_H
#define DOE_COMPILER_COMPILE_INTERNAL_H
#include "numeric.h"
#include "stage.h"
#include "sampling_internal.h"

bool compiler_analyze(Source *, CompilerWorkspace *, NumericEnvironment *);
bool compiler_valid_span(const void *, size_t count, size_t width, size_t alignment, bool selected);
bool compiler_selection_shape(Source *, const DoeCompilerEntrySelectionV5 *);
bool compiler_select_entry(Source *, const DoeCompilerEntrySelectionV5 *, uint32_t *selected);
bool compiler_module_spans(Source *, uint32_t flags, const DoeCompilerModuleOutputs *);
bool compiler_module_records(const DoeCompilerModuleOutputs *);

typedef struct {
    DoeCompilerModuleOutputCounts counts;
    SamplingContext *sampling;
    size_t handles, interfaces, stage_names;
} CompilerMetadata;
bool compiler_metadata_prepare(Source *, DoeCompilerTypedOutputCounts *, uint32_t flags,
                               bool handles, CompilerMetadata *);
bool compiler_metadata_fits(const CompilerMetadata *, const DoeCompilerModuleOutputs *,
                           const DoeCompilerResourceOutputs *, const DoeCompilerStageOutputs *);
void compiler_metadata_publish(Source *, const CompilerMetadata *, uint32_t selected, uint32_t version,
    const DoeCompilerModuleOutputs *, DoeCompilerModuleResult *,
    const DoeCompilerResourceOutputs *, DoeCompilerResourceResult *,
    const DoeCompilerStageOutputs *, DoeCompilerStageResult *);
DoeCompilerStatus compiler_compile_module(const DoeCompilerModuleCapacities *,
    void *, size_t, const char *, size_t, const DoeCompilerEntrySelectionV5 *, uint32_t,
    const DoeCompilerModuleOutputs *, DoeCompilerModuleResult *, const DoeCompilerResourceOutputs *,
    DoeCompilerResourceResult *, const DoeCompilerStageCapacities *, const DoeCompilerStageOutputs *,
    DoeCompilerStageResult *, uint32_t, SpecializationContext *);
#endif
