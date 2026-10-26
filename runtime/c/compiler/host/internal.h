/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_HOST_INTERNAL_H
#define DOE_COMPILER_HOST_INTERNAL_H

#include "../include/doe_compiler_host.h"

DoeCompilerHostStatus doe_host_preflight(uint32_t version,
    DoeCompilerHostBuffer *output, size_t output_size,
    DoeCompilerHostDiagnostic *diagnostic, size_t diagnostic_size);
void doe_host_diagnostic(DoeCompilerHostDiagnostic *diagnostic,
    uint32_t domain, uint32_t code, const char *stage, const char *message);

#endif
