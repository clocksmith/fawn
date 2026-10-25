/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_BROWSER_JOB_H
#define DOE_COMPILER_BROWSER_JOB_H
#include <stdbool.h>
#include <stddef.h>
#include "../include/doe_compiler.h"
typedef struct {
    DoeCompilerDiagnostic diagnostic;
    char *wgsl;
    size_t length, rewrites;
} WgslJobResult;
void compiler_wgsl_job(const char *, size_t, bool, WgslJobResult *);
/* Allocation belongs to this delivery adapter, never to the compiler library.
 * The caller frees the JSON result. No source or workspace survives the job. */
char *compiler_browser_job(const char *, size_t, bool, size_t *);
#endif
