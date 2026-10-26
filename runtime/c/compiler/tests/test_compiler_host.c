/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "doe_compiler_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "host consumer failure at line %d: %s\n", \
        __LINE__, #condition); return 1; } } while (0)

static const char gather[] =
    "{\"kernel\":\"gather\",\"inputs\":{"
    "\"indices\":{\"elem\":\"u32\",\"shape\":[2],\"values\":[1,0]},"
    "\"table\":{\"elem\":\"f32\",\"shape\":[2,2],\"values\":[1,2,3,4]}}}";

static size_t occurrences(const char *text, const char *needle) {
    size_t count = 0;
    const size_t length = strlen(needle);
    while ((text = strstr(text, needle)) != NULL) {
        count++;
        text += length;
    }
    return count;
}

static char *load_source(const char *root, size_t *size) {
    char path[4096];
    FILE *file;
    char *bytes;
    size_t count;
    int length = snprintf(path, sizeof(path),
        "%s/runtime/zig/tests/tsir/bootstrap/gather.wgsl", root);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return NULL;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    bytes = malloc(65536);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    count = fread(bytes, 1, 65536, file);
    if (ferror(file) || count == 65536) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    if (fclose(file) != 0) {
        free(bytes);
        return NULL;
    }
    *size = count;
    return bytes;
}

int main(int argc, char **argv) {
    const uint32_t version = DOE_COMPILER_HOST_ABI_VERSION;
    DoeCompilerHostBuffer output = {NULL, 0};
    DoeCompilerHostDiagnostic diagnostic;
    DoeCompilerHostDiagnostic previous;
    DoeCompilerHostStatus status;
    char *source;
    size_t source_size = 0;
    CHECK(argc == 2);
    memset(&diagnostic, 0xa5, sizeof(diagnostic));
    previous = diagnostic;
    status = doe_compiler_host_tsir_oracle(version + 1u, "gather", gather,
        sizeof(gather) - 1, &output, sizeof(output), &diagnostic,
        sizeof(diagnostic));
    CHECK(status == DOE_COMPILER_HOST_ABI_MISMATCH);
    CHECK(output.data == NULL && output.size == 0);
    CHECK(memcmp(&diagnostic, &previous, sizeof(diagnostic)) == 0);
    CHECK(doe_compiler_host_tsir_oracle(version, "gather", gather,
        sizeof(gather) - 1, &output, sizeof(output) - 1, &diagnostic,
        sizeof(diagnostic)) == DOE_COMPILER_HOST_ABI_MISMATCH);
    CHECK(memcmp(&diagnostic, &previous, sizeof(diagnostic)) == 0);
    CHECK(doe_compiler_host_tsir_oracle(version, "gather", gather,
        sizeof(gather) - 1, &output, sizeof(output), NULL, 1) ==
        DOE_COMPILER_HOST_ABI_MISMATCH);
    status = doe_compiler_host_tsir_oracle(version, "gather", gather,
        sizeof(gather) - 1, &output, sizeof(output), &diagnostic,
        sizeof(diagnostic));
    CHECK(status == DOE_COMPILER_HOST_OK && output.data != NULL);
    CHECK(output.data[output.size] == 0);
    CHECK(strstr((char *)output.data,
        "fb5ae94f7e6aec39682d0a21c34df1d7914a3975e43d088ac8fec83df5b0fb8b") != NULL);
    {
        uint8_t *saved = output.data;
        size_t saved_size = output.size;
        CHECK(doe_compiler_host_tsir_oracle(version, "gather", gather,
            sizeof(gather) - 1, &output, sizeof(output), &diagnostic,
            sizeof(diagnostic)) == DOE_COMPILER_HOST_INVALID_ARGUMENT);
        CHECK(output.data == saved && output.size == saved_size);
    }
    doe_compiler_host_release(&output);
    doe_compiler_host_release(&output);
    doe_compiler_host_release(NULL);
    CHECK(output.data == NULL && output.size == 0);
    CHECK(doe_compiler_host_tsir_oracle(version, "gather", "{}", 2,
        &output, sizeof(output), NULL, 0) == DOE_COMPILER_HOST_OK);
    CHECK(strstr((char *)output.data, "not_implemented") != NULL);
    doe_compiler_host_release(&output);
    CHECK(doe_compiler_host_tsir_manifest(version, "/missing-fawn-inputs",
        &output, sizeof(output), &diagnostic, sizeof(diagnostic)) ==
        DOE_COMPILER_HOST_IO_ERROR);
    CHECK(output.data == NULL && output.size == 0 && diagnostic.domain == 1);
    CHECK(diagnostic.line == 0 && diagnostic.column == 0 && diagnostic.system_error != 0);
    CHECK(doe_compiler_host_tsir_manifest(version, argv[1], &output,
        sizeof(output), &diagnostic, sizeof(diagnostic)) == DOE_COMPILER_HOST_OK);
    CHECK(occurrences((char *)output.data, "\"backend\"") == 9);
    CHECK(strstr((char *)output.data, "wse3") == NULL);
    doe_compiler_host_release(&output);
    source = load_source(argv[1], &source_size);
    CHECK(source != NULL);
    status = doe_compiler_host_tsir_source(version, argv[1], "gather", source,
        source_size, &output, sizeof(output), &diagnostic, sizeof(diagnostic));
    free(source);
    CHECK(status == DOE_COMPILER_HOST_OK);
    CHECK(output.data[0] == '[' && output.data[output.size - 2] == ']');
    CHECK(occurrences((char *)output.data, "\"backend\"") == 3);
    doe_compiler_host_release(&output);
    CHECK(doe_compiler_host_tsir_source(version, argv[1], "gather",
        "fn malformed( {", 15, &output, sizeof(output), &diagnostic,
        sizeof(diagnostic)) != DOE_COMPILER_HOST_OK);
    CHECK(output.data == NULL && output.size == 0 && diagnostic.domain == 1);
    CHECK(diagnostic.line == 1 && diagnostic.column != 0);
    CHECK(doe_compiler_host_dxil(version, "", 0, NULL, "build/tmp", &output,
        sizeof(output), &diagnostic, sizeof(diagnostic)) ==
        DOE_COMPILER_HOST_INVALID_ARGUMENT);
    CHECK(output.data == NULL && output.size == 0);
    puts("PASS: allocating host C ABI, source/manifest/oracle, publication and release");
    return 0;
}
