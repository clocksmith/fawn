/* SPDX-License-Identifier: MIT */
#ifndef DOE_C_CLI_OUTPUT_H
#define DOE_C_CLI_OUTPUT_H
#include "stream.h"
#include <stdio.h>
/* Writes one retained normalized command record followed by a newline. */
bool cli_write_normalized(FILE *out, uint64_t seq, const doe_command *command);
#endif
