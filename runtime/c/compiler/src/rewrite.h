/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_REWRITE_H
#define DOE_COMPILER_REWRITE_H
#include "internal.h"

typedef struct {
    uint32_t start, operator_offset, end, replacement;
    bool division;
} WgslEdit;
/* Consumes validated expression types. Re-lexing reuses the dead IR phase;
 * callers must not emit a native target after planning source edits. */
bool compiler_rewrite_plan(Source *, WgslEdit *, size_t capacity,
                           size_t *count, size_t *output_length);
void compiler_rewrite_publish(const Source *, const WgslEdit *, size_t count, char *);
#endif
