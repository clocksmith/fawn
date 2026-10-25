/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "job.h"
#include <stdint.h>
#include <stdlib.h>

/* One Web Worker exclusively owns this Wasm instance and its current job.
 * These are adapter-owned instance objects, not native compiler globals. */
static char *input, *output;
static uint32_t input_size, output_size;

uint32_t source_limit(void);
uint32_t reserve_source(uint32_t);
uint32_t compile(uint32_t, uint32_t);
uint32_t output_length(void);
void release_job(void);

uint32_t source_limit(void) { return 65536u; }

uint32_t reserve_source(uint32_t length) {
    if (length == 0u || length > source_limit()) return 0u;
    free(input);
    input = malloc(length);
    input_size = input != NULL ? length : 0u;
    return (uint32_t)(uintptr_t)input;
}

uint32_t compile(uint32_t length, uint32_t passes_enabled) {
    free(output);
    output = NULL;
    output_size = 0u;
    if (input == NULL || length != input_size || passes_enabled > 1u) return 0u;
    size_t size = 0u;
    output = compiler_browser_job(input, length, passes_enabled != 0u, &size);
    output_size = (uint32_t)size;
    return (uint32_t)(uintptr_t)output;
}

uint32_t output_length(void) { return output_size; }

void release_job(void) {
    free(input);
    free(output);
    input = NULL;
    output = NULL;
    input_size = 0u;
    output_size = 0u;
}
