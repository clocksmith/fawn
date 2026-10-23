/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_TEXT_INTERNAL_H
#define DOE_COMPILER_TEXT_INTERNAL_H
#include "compile_internal.h"
#include "../include/doe_compiler_text.h"
#include "../targets/metal/metal.h"

typedef struct {
    DoeMetalResult code;
    size_t bindings, entries;
} TextTargetResult;
bool compiler_text_metal(Source *, uint32_t selected, DoeTargetScratch,
    DoeCompilerTextBindingV10 *, DoeCompilerTextEntryV10 *, TextTargetResult *);
#endif
