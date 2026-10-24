/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_TEXT_INTERNAL_H
#define DOE_COMPILER_TEXT_INTERNAL_H
#include "compile_internal.h"
#include "../include/doe_compiler_text.h"
#include "../include/doe_compiler_hlsl.h"
#include "../include/doe_compiler_metal.h"
#include "../targets/metal/metal.h"
#include "../targets/hlsl/hlsl.h"

typedef struct {
    size_t required_bytes;
    size_t bindings, entries;
} TextTargetResult;
bool compiler_text_workspace(uint32_t, const DoeCompilerTextCapacities *, DoeCompilerWorkspaceRequirements *);
bool compiler_text_metal(Source *, uint32_t selected, DoeTargetScratch,
    DoeCompilerTextBindingV10 *, DoeCompilerTextEntryV10 *, TextTargetResult *);
bool compiler_text_metal_entry(Source *, uint32_t, DoeTargetScratch,
    DoeCompilerMetalBindingV12 *, DoeCompilerMetalEntryV12 *, TextTargetResult *);
bool compiler_text_hlsl(Source *, uint32_t selected, DoeTargetScratch,
    DoeCompilerHlslBindingV11 *, DoeCompilerHlslEntryV11 *, TextTargetResult *);
#endif
