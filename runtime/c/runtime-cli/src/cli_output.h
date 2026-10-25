/* SPDX-License-Identifier: MIT */
#ifndef DOE_C_CLI_OUTPUT_H
#define DOE_C_CLI_OUTPUT_H
#include "stream.h"
#include "trace.h"
#include <stdio.h>
/* Writes one retained normalized command record followed by a newline. */
bool cli_write_normalized(FILE *out, uint64_t seq, const doe_command *command);
/* Host summary never labels an absent execution as a successful native operation. */
bool cli_write_summary(FILE *out, uint64_t seq, const doe_command *command, const doe_quirk_decision *decision,
    const doe_trace_execution *execution, const char *backend);
#endif
