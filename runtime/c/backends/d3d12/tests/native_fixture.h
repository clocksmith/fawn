/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_NATIVE_FIXTURE_H
#define DOE_DX_NATIVE_FIXTURE_H
#include "doe_d3d12_graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only fixture file IO/reporting. No native-call replacement or GPU simulation. */
static unsigned char *read_bytecode(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length <= 0 || length > 2 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    unsigned char *bytecode = malloc((size_t)length);
    if (!bytecode) {
        fclose(file);
        return NULL;
    }
    size_t received = fread(bytecode, 1, (size_t)length, file);
    int close_result = fclose(file);
    if (received != (size_t)length || close_result) {
        free(bytecode);
        return NULL;
    }
    *size = (size_t)length;
    return bytecode;
}

static void report(const char *step, doe_dx_status status, const doe_dx_error *error) {
    fprintf(stderr, "%s: status=%d native=%ld operation=%s\n", step, (int)status,
        (long)error->native_code, error->operation ? error->operation : "none");
}
#endif
