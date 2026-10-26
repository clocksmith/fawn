/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    TsirBuffer output;
    TsirBootstrapManifestError error;
    TsirStatus status = TSIR_OK;
    tsir_buf_init(&output);
    status = tsir_bootstrap_manifest_generate(".", &output, &error);
    if (status != TSIR_OK) {
        fprintf(stderr, "TSIR bootstrap manifest: %s: %s: %s",
            error.input == NULL ? "input" : error.input,
            error.stage == NULL ? "operation" : error.stage,
            tsir_status_name(status));
        if (error.diagnostic.message[0] != '\0') {
            fprintf(stderr, ": %s", error.diagnostic.message);
        }
        if (error.system_error != 0) {
            fprintf(stderr, ": %s", strerror(error.system_error));
        }
        fputc('\n', stderr);
        tsir_buf_free(&output);
        return 1;
    }
    if (fwrite(output.data, 1u, output.len, stdout) != output.len ||
        fflush(stdout) != 0) {
        fputs("TSIR bootstrap manifest: output write failed\n", stderr);
        tsir_buf_free(&output);
        return 1;
    }
    tsir_buf_free(&output);
    return 0;
}
