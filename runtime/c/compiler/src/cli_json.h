/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_CLI_JSON_H
#define DOE_COMPILER_CLI_JSON_H
#include "../include/doe_compiler_v2.h"
#include <stdbool.h>
#define CLI_JSON_V1_CAPACITY 4096u
#define CLI_JSON_V2_CAPACITY 32768u
bool cli_json_v1(const DoeCompilerReflection *reflection, char *bytes, size_t capacity, size_t *length);
bool cli_json_v2(const DoeCompilerReflectionV2 *reflection, char *bytes, size_t capacity, size_t *length);
#endif
