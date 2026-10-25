/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_CLI_SPECIALIZATION_H
#define DOE_COMPILER_CLI_SPECIALIZATION_H
#include "../include/doe_compiler_specialization.h"
#include <stdbool.h>

#define CLI_MAX_OVERRIDES 64u
typedef struct {
    DoeCompilerOverrideValueV13 values[CLI_MAX_OVERRIDES];
    size_t count;
} CliOverrides;

/* Keys borrow immutable argv spans through compilation. Parsing accepts finite
 * numbers only and checks duplicates across repeated --overrides arguments. */
bool cli_parse_overrides(const char *argument, CliOverrides *overrides);
/* CLI allocation owner; ABI14 retains every shader entry in source order.
 * Success transfers code ownership. Failure leaves code empty with diagnostic. */
bool cli_compile_specialized(const DoeCompilerStageCapacities *stages,
    const char *source, size_t length, const CliOverrides *overrides,
    unsigned char **code, size_t *code_length, DoeCompilerDiagnostic *diagnostic);
#endif
