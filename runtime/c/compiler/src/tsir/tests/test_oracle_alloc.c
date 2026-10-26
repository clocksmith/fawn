/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_bootstrap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t fail_at;

/* Test-only ownership ledger, outside the production library. */
static void *live_allocations[512];
static size_t live_count;

static void record_allocation(void *pointer) {
    if (pointer != NULL) {
        assert(live_count < sizeof(live_allocations) / sizeof(live_allocations[0]));
        live_allocations[live_count++] = pointer;
    }
}

void *oracle_test_malloc(size_t size) {
    void *pointer = NULL;
    if (++allocation_count == fail_at) {
        return NULL;
    }
    pointer = malloc(size);
    record_allocation(pointer);
    return pointer;
}
void *oracle_test_calloc(size_t count, size_t size) {
    void *pointer = NULL;
    if (++allocation_count == fail_at) {
        return NULL;
    }
    pointer = calloc(count, size);
    record_allocation(pointer);
    return pointer;
}
void *oracle_test_realloc(void *pointer, size_t size) {
    size_t index = live_count;
    void *grown = NULL;
    if (++allocation_count == fail_at) {
        return NULL;
    }
    if (pointer != NULL) {
        for (index = 0; index < live_count; index++) {
            if (live_allocations[index] == pointer) {
                break;
            }
        }
        assert(index < live_count);
    }
    assert(size != 0);
    grown = realloc(pointer, size);
    if (grown != NULL) {
        if (index == live_count) {
            record_allocation(grown);
        } else {
            live_allocations[index] = grown;
        }
    }
    return grown;
}
void oracle_test_free(void *pointer) {
    if (pointer != NULL) {
        size_t index = 0;
        for (index = 0; index < live_count; index++) {
            if (live_allocations[index] == pointer) {
                break;
            }
        }
        assert(index < live_count);
        live_count--;
        live_allocations[index] = live_allocations[live_count];
    }
    free(pointer);
}

static size_t check_input(const char *kernel, const char *input) {
    allocation_count = 0;
    fail_at = 0;
    char *output = NULL;
    size_t length = 0;
    size_t total = 0;
    TsirStatus status = tsir_bootstrap_oracle_run(kernel, input,
        strlen(input), &output, &length);
    assert(status == TSIR_OK && output != NULL && length != 0);
    total = allocation_count;
    oracle_test_free(output);
    assert(live_count == 0);
    for (size_t i = 1; i <= total; i++) {
        allocation_count = 0;
        fail_at = i;
        output = (char *)1;
        length = 123;
        status = tsir_bootstrap_oracle_run(kernel, input,
            strlen(input), &output, &length);
        if (status != TSIR_ERR_NOMEM || output != NULL || length != 0 || live_count != 0) {
            fprintf(stderr, "allocation %zu/%zu: status %d length %zu\n",
                i, total, (int)status, length);
            abort();
        }
    }
    return total;
}

int main(void) {
    const char *gather = "{\"kernel\":\"gather\",\"inputs\":{"
        "\"indices\":{\"elem\":\"u32\",\"shape\":[1],\"values\":[0]},"
        "\"table\":{\"elem\":\"f32\",\"shape\":[1,1],\"values\":[2]}}}";
    const char *gemv = "{\"kernel\":\"fused_gemv\",\"inputs\":{"
        "\"W\":{\"elem\":\"f32\",\"shape\":[1,1],\"values\":[2]},"
        "\"x\":{\"elem\":\"f32\",\"shape\":[1],\"values\":[3]}}}";
    const char *rms = "{\"kernel\":\"rms_norm\",\"inputs\":{"
        "\"input\":{\"elem\":\"f32\",\"shape\":[1],\"values\":[1]},"
        "\"weight\":{\"elem\":\"f32\",\"shape\":[1],\"values\":[2]},"
        "\"u\":{\"elem\":\"u32\",\"shape\":[2],\"values\":[1,0]}}}";
    size_t total = check_input("gather", gather);
    total += check_input("fused_gemv", gemv);
    total += check_input("rms_norm", rms);
    printf("bootstrap oracle allocation failures: %zu checks passed\n", total);
    return 0;
}
