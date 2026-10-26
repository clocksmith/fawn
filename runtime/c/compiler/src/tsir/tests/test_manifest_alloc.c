/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir.h"
#include "manifest_alloc.h"
#undef malloc
#undef calloc
#undef realloc
#undef free

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test-only bookkeeping uses fixed static storage so allocation injection does
 * not allocate, and the live set detects leaks, duplicate frees and lost
 * realloc ownership on each real generator acquisition failure. */
static void *live[4096];
static size_t live_count;
static size_t allocation_count;
static size_t fail_at;

static void remember(void *pointer) {
    if (pointer == NULL) {
        return;
    }
    assert(live_count < sizeof(live) / sizeof(live[0]));
    live[live_count++] = pointer;
}

static size_t allocation_index(void *pointer) {
    for (size_t i = 0; i < live_count; i++) {
        if (live[i] == pointer) {
            return i;
        }
    }
    fputs("manifest allocator: freeing or resizing unowned pointer\n", stderr);
    abort();
}

void *oracle_test_malloc(size_t size) {
    if (++allocation_count == fail_at) {
        return NULL;
    }
    void *pointer = malloc(size);
    remember(pointer);
    return pointer;
}

void *oracle_test_calloc(size_t count, size_t size) {
    if (++allocation_count == fail_at) {
        errno = ENOMEM;
        return NULL;
    }
    void *pointer = calloc(count, size);
    remember(pointer);
    return pointer;
}

void *oracle_test_realloc(void *pointer, size_t size) {
    if (++allocation_count == fail_at) {
        return NULL;
    }
    bool new_allocation = pointer == NULL;
    size_t index = new_allocation ? 0 : allocation_index(pointer);
    void *replacement = realloc(pointer, size);
    if (replacement != NULL) {
        if (new_allocation) {
            remember(replacement);
        } else {
            live[index] = replacement;
        }
    }
    return replacement;
}

void oracle_test_free(void *pointer) {
    if (pointer != NULL) {
        size_t index = allocation_index(pointer);
        live[index] = live[--live_count];
    }
    free(pointer);
}

static void prepare_output(TsirBuffer *output) {
    fail_at = 0;
    assert(live_count == 0);
    tsir_buf_init(output);
    tsir_buf_puts(output, "caller-prefix");
    assert(output->err == TSIR_OK && live_count == 1);
    allocation_count = 0;
}

int main(void) {
    TsirBootstrapManifestError error;
    TsirBuffer output;
    prepare_output(&output);
    TsirStatus status = tsir_bootstrap_manifest_generate(".", &output, &error);
    assert(status == TSIR_OK && output.len > strlen("caller-prefix"));
    size_t total = allocation_count;
    tsir_buf_free(&output);
    assert(live_count == 0);
    for (size_t i = 1; i <= total; i++) {
        prepare_output(&output);
        fail_at = i;
        status = tsir_bootstrap_manifest_generate(".", &output, &error);
        if (status != TSIR_ERR_NOMEM || live_count != 1 ||
            strcmp(output.data, "caller-prefix") != 0 ||
            output.len != strlen("caller-prefix")) {
            fprintf(stderr, "manifest allocation %zu/%zu: status %s live %zu "
                "stage %s length %zu\n", i, total, tsir_status_name(status),
                live_count, error.stage == NULL ? "none" : error.stage,
                output.len);
            abort();
        }
        tsir_buf_free(&output);
        assert(live_count == 0);
    }
    /* The source adapter's first acquisition fails before its normal cleanup
     * path. Model a real allocator's errno change, not just a NULL result. */
    static const char source[] = "@compute @workgroup_size(1) fn main() {}";
    TsirSemantic semantic;
    DoeCompilerDiagnostic diagnostic;
    allocation_count = 0;
    fail_at = 1;
    errno = EDOM;
    status = tsir_source_lower(source, sizeof(source) - 1u, "test",
        &semantic, &diagnostic);
    assert(status == TSIR_ERR_NOMEM);
    assert(errno == EDOM);
    assert(allocation_count == 1 && live_count == 0);
    assert(semantic.functions == NULL && semantic.function_count == 0);
    printf("bootstrap manifest allocation failures: %zu checks passed\n", total);
    puts("TSIR source first-allocation errno preservation: passed");
    return 0;
}
