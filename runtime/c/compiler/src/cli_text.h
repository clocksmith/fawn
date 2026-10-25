/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_CLI_TEXT_H
#define DOE_COMPILER_CLI_TEXT_H
#include "../include/doe_compiler_metal.h"
#include "../include/doe_compiler_hlsl.h"
#include <stdbool.h>

/* CLI allocation owner. On success the caller owns *code; failure publishes no
 * code and reports the public compiler diagnostic or a host allocation error. */
bool cli_compile_text(uint32_t target, const DoeCompilerStageCapacities *stages,
    const char *source, size_t length, unsigned char **code, size_t *code_length,
    DoeCompilerDiagnostic *diagnostic);
#endif
