/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_HLSL_TEST_SUPPORT_H
#define DOE_HLSL_TEST_SUPPORT_H
#include "../hlsl.h"
typedef struct {
    void *storage;
    CompilerWorkspace *module;
    Source source;
    DoeCompilerDiagnostic diagnostic;
} Fixture;
Fixture *prepare(const char *);
void release(Fixture *);
char *emit(Fixture *, uint32_t selected);
void has(const char *, const char *);
void test_images(const char *);
#endif
