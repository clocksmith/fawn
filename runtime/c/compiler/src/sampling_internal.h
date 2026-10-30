/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_SAMPLING_INTERNAL_H
#define DOE_COMPILER_SAMPLING_INTERNAL_H
#include "internal.h"
#include "../include/doe_compiler_translation.h"
#include "../include/doe_compiler_hlsl_profile.h"
typedef struct {
    DoeCompilerTranslationEntryV17 *staging;
    size_t capacity, count;
    const DoeCompilerTranslationOutputsV17 *outputs;
    DoeCompilerHlslProfileV19 *hlsl_profile; /* Optional private V19 staging, not a caller output. */
} TranslationContext;
typedef struct {
    DoeCompilerSamplingPairV16 *staging;
    size_t capacity, count;
    const DoeCompilerSamplingOutputsV16 *outputs;
    uint64_t work;
    TranslationContext *translation;
} SamplingContext;
bool sampling_layout(uint32_t, const DoeCompilerSamplingCapacitiesV16 *,
    size_t *, DoeCompilerWorkspaceRequirements *);
SamplingContext *sampling_context(Source *);
bool sampling_prepare(Source *);
bool translation_prepare(Source *);
bool translation_layout(uint32_t, const DoeCompilerTranslationCapacitiesV17 *, size_t *, DoeCompilerWorkspaceRequirements *);
#endif
