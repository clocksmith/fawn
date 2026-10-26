/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_SOURCE_H
#define DOE_TSIR_SOURCE_H
#include "tsir_frontend.h"
#include "../../include/doe_compiler.h"
/* Validate through the compiler's canonical parser and semantic owner, adapt
 * its typed syntax to the TSIR frontend, and deep-own the returned semantic.
 * The source and version are borrowed only during this call. On failure out is
 * zeroed; diagnostic carries the first source failure. Free success through
 * tsir_frontend_free_semantic. Source input is bounded to 1 MiB. */
TsirStatus tsir_source_lower(const char *source, size_t length,
    const char *frontend_version, TsirSemantic *out,
    DoeCompilerDiagnostic *diagnostic);
#endif
