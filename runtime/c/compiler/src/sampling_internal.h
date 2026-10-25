/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_SAMPLING_INTERNAL_H
#define DOE_COMPILER_SAMPLING_INTERNAL_H
#include "internal.h"
#include "../include/doe_compiler_sampling.h"
typedef struct {
    DoeCompilerSamplingPairV16 *staging;
    size_t capacity, count;
    const DoeCompilerSamplingOutputsV16 *outputs;
    uint64_t work;
} SamplingContext;
bool sampling_layout(uint32_t, const DoeCompilerSamplingCapacitiesV16 *,
    size_t *, DoeCompilerWorkspaceRequirements *);
SamplingContext *sampling_context(Source *);
bool sampling_prepare(Source *);
#endif
