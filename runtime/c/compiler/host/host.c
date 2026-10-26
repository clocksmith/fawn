/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DoeCompilerHostStatus doe_host_preflight(uint32_t version,
    DoeCompilerHostBuffer *output, size_t output_size,
    DoeCompilerHostDiagnostic *diagnostic, size_t diagnostic_size) {
    if (version != DOE_COMPILER_HOST_ABI_VERSION ||
        output_size != sizeof(*output) ||
        (diagnostic != NULL && diagnostic_size != sizeof(*diagnostic)) ||
        (diagnostic == NULL && diagnostic_size != 0)) {
        return DOE_COMPILER_HOST_ABI_MISMATCH;
    }
    if (output == NULL) {
        return DOE_COMPILER_HOST_INVALID_ARGUMENT;
    }
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
    }
    if (output->data != NULL || output->size != 0) {
        doe_host_diagnostic(diagnostic, 0,
            (uint32_t)DOE_COMPILER_HOST_INVALID_ARGUMENT, "host preflight",
            "output must be an empty result; release the previous result first");
        return DOE_COMPILER_HOST_INVALID_ARGUMENT;
    }
    return DOE_COMPILER_HOST_OK;
}

void doe_host_diagnostic(DoeCompilerHostDiagnostic *diagnostic,
    uint32_t domain, uint32_t code, const char *stage, const char *message) {
    if (diagnostic == NULL) {
        return;
    }
    diagnostic->domain = domain;
    diagnostic->code = code;
    snprintf(diagnostic->stage, sizeof(diagnostic->stage), "%s",
        stage == NULL ? "" : stage);
    snprintf(diagnostic->message, sizeof(diagnostic->message), "%s",
        message == NULL ? "" : message);
}

void doe_compiler_host_release(DoeCompilerHostBuffer *output) {
    if (output != NULL) {
        free(output->data);
        output->data = NULL;
        output->size = 0;
    }
}
