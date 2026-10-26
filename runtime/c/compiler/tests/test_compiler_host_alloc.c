/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "doe_compiler_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void __real_free(void *pointer);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *pointer, size_t size);
void __wrap_free(void *pointer);

static size_t acquisitions;
static size_t fail_at;
static size_t live;

static int fail(void) {
    acquisitions++;
    return fail_at != 0 && acquisitions == fail_at;
}

void *__wrap_malloc(size_t size) {
    void *pointer = fail() ? NULL : __real_malloc(size);
    if (pointer != NULL) {
        live++;
    }
    return pointer;
}

void *__wrap_calloc(size_t count, size_t size) {
    void *pointer = fail() ? NULL : __real_calloc(count, size);
    if (pointer != NULL) {
        live++;
    }
    return pointer;
}

void *__wrap_realloc(void *pointer, size_t size) {
    void *result;
    const int was_null = pointer == NULL;
    if (fail()) {
        return NULL;
    }
    result = __real_realloc(pointer, size);
    if (was_null && result != NULL) {
        live++;
    } else if (!was_null && size == 0 && result == NULL) {
        live--;
    }
    return result;
}

void __wrap_free(void *pointer) {
    if (pointer != NULL) {
        if (live == 0) {
            fputs("host fault consumer: unbalanced free\n", stderr);
            abort();
        }
        live--;
    }
    __real_free(pointer);
}

static const char gather[] =
    "{\"kernel\":\"gather\",\"inputs\":{"
    "\"indices\":{\"elem\":\"u32\",\"shape\":[2],\"values\":[1,0]},"
    "\"table\":{\"elem\":\"f32\",\"shape\":[2,2],\"values\":[1,2,3,4]}}}";

static int oracle(size_t failure, size_t *count) {
    DoeCompilerHostBuffer output = {NULL, 0};
    DoeCompilerHostDiagnostic diagnostic;
    DoeCompilerHostStatus status;
    acquisitions = 0;
    fail_at = failure;
    status = doe_compiler_host_tsir_oracle(DOE_COMPILER_HOST_ABI_VERSION,
        "gather", gather, sizeof(gather) - 1, &output, sizeof(output),
        &diagnostic, sizeof(diagnostic));
    *count = acquisitions;
    fail_at = 0;
    if ((failure == 0 && status != DOE_COMPILER_HOST_OK) ||
        (failure != 0 && (status != DOE_COMPILER_HOST_OUT_OF_MEMORY ||
            output.data != NULL || output.size != 0))) {
        fprintf(stderr, "host fault consumer: failure=%zu status=%u\n",
            failure, (unsigned)status);
        doe_compiler_host_release(&output);
        return 1;
    }
    doe_compiler_host_release(&output);
    if (live != 0) {
        fprintf(stderr, "host fault consumer: failure=%zu live=%zu\n", failure, live);
        return 1;
    }
    return 0;
}

int main(void) {
    size_t count = 0;
    size_t ignored = 0;
    if (oracle(0, &count) != 0 || count == 0) {
        return 1;
    }
    for (size_t i = 1; i <= count; i++) {
        if (oracle(i, &ignored) != 0 || oracle(0, &ignored) != 0) {
            return 1;
        }
    }
    printf("PASS: host oracle %zu acquisition failures, empty publication and retry\n",
        count);
    return 0;
}
