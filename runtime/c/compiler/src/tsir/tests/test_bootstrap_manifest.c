/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir.h"

#include <stdio.h>
#include <string.h>

/* Failure publication is checked against populated caller storage, rather than
 * only checking an initially empty buffer. */
static int expect_preserved(TsirStatus expected, const char *root,
    const char *kernel, const char *source, size_t length) {
    TsirBuffer output;
    TsirBootstrapManifestError error;
    TsirStatus status;
    int failed = 0;
    tsir_buf_init(&output);
    tsir_buf_puts(&output, "caller-owned-prefix");
    status = tsir_bootstrap_manifest_source(root, kernel, source, length,
        &output, &error);
    if (status != expected || output.len != strlen("caller-owned-prefix") ||
        strcmp(output.data, "caller-owned-prefix") != 0 || error.stage == NULL) {
        fprintf(stderr, "manifest failed publication: got %s wanted %s\n",
            tsir_status_name(status), tsir_status_name(expected));
        failed = 1;
    }
    tsir_buf_free(&output);
    return failed;
}

int main(int argc, char **argv) {
    if (argc == 2) {
        TsirBuffer result;
        TsirBootstrapManifestError diagnostic;
        tsir_buf_init(&result);
        TsirStatus status = tsir_bootstrap_manifest_generate(argv[1], &result, &diagnostic);
        int failed = status != TSIR_OK;
        if (!failed && fwrite(result.data, 1u, result.len, stdout) != result.len) failed = 1;
        tsir_buf_free(&result);
        return failed;
    }
    static const char invalid[] = "@compute fn main( {";
    TsirBuffer output;
    TsirBootstrapManifestError error;
    TsirStatus status;
    int failures = 0;
    failures += expect_preserved(TSIR_ERR_INVALID, NULL, "gather", "", 0);
    failures += expect_preserved(TSIR_ERR_INVALID, ".", NULL, "", 0);
    failures += expect_preserved(TSIR_ERR_INVALID, ".", "gather", NULL, 0);
    failures += expect_preserved(TSIR_ERR_UNSUPPORTED, ".", "unrecognized",
        "", 0);
    failures += expect_preserved(TSIR_ERR_OVERFLOW, ".", "gather", "",
        (size_t)TSIR_BOOTSTRAP_MAX_WGSL_BYTES + 1u);
    /* Specific parser status may vary with source front-end diagnostics, but
     * malformed input must fail before emitting a plausible manifest. */
    tsir_buf_init(&output);
    tsir_buf_puts(&output, "prefix");
    status = tsir_bootstrap_manifest_source(".", "gather", invalid,
        sizeof(invalid) - 1u, &output, &error);
    if (status == TSIR_OK || strcmp(output.data, "prefix") != 0 ||
        error.diagnostic.message[0] == '\0') {
        fputs("malformed WGSL was not rejected with preserved output\n", stderr);
        failures++;
    }
    tsir_buf_free(&output);
    tsir_buf_init(&output);
    tsir_buf_puts(&output, "prefix");
    status = tsir_bootstrap_manifest_generate("/does-not-exist/fawn", &output,
        &error);
    if (status == TSIR_OK || strcmp(output.data, "prefix") != 0 ||
        error.system_error == 0 || error.input == NULL) {
        fputs("missing source was not rejected with preserved output\n", stderr);
        failures++;
    }
    tsir_buf_free(&output);
    tsir_buf_init(&output);
    tsir_buf_puts(&output, "prefix");
    output.err = TSIR_ERR_RANGE;
    status = tsir_bootstrap_manifest_source(".", "gather", "", 0,
        &output, &error);
    if (status != TSIR_ERR_RANGE || strcmp(output.data, "prefix") != 0) {
        fputs("source lowering replaced the first output error\n", stderr);
        failures++;
    }
    status = tsir_bootstrap_manifest_generate(".", &output, &error);
    if (status != TSIR_ERR_RANGE || strcmp(output.data, "prefix") != 0) {
        fputs("generation replaced the first output error\n", stderr);
        failures++;
    }
    tsir_buf_free(&output);
    tsir_buf_init(&output);
    tsir_buf_puts(&output, "prefix");
    status = tsir_bootstrap_manifest_generate("invalid-\xff-root", &output, &error);
    if (status != TSIR_ERR_INVALID || strcmp(output.data, "prefix") != 0 ||
        error.system_error == 0 || strcmp(error.stage, "WGSL input read") != 0) {
        fputs("invalid UTF-8 path did not retain WGSL read failure and output\n", stderr);
        failures++;
    }
    tsir_buf_free(&output);
    printf("bootstrap_manifest: 10 checks, %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
