/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include "../src/tsir/tsir_bootstrap.h"
#include "../src/tsir/tsir_bootstrap_manifest.h"

#include <string.h>

static DoeCompilerHostStatus tsir_status(TsirStatus status,
    const TsirBootstrapManifestError *error,
    DoeCompilerHostDiagnostic *diagnostic) {
    if (status == TSIR_OK) {
        return DOE_COMPILER_HOST_OK;
    }
    doe_host_diagnostic(diagnostic, 1, (uint32_t)status,
        error == NULL ? "TSIR oracle" : error->stage,
        error != NULL && error->diagnostic.message[0] != '\0'
            ? error->diagnostic.message : tsir_status_name(status));
    if (error != NULL && diagnostic != NULL) {
        diagnostic->system_error = error->system_error;
        diagnostic->line = error->diagnostic.line;
        diagnostic->column = error->diagnostic.column;
    }
    switch (status) {
        case TSIR_OK: return DOE_COMPILER_HOST_OK;
        case TSIR_ERR_NOMEM: return DOE_COMPILER_HOST_OUT_OF_MEMORY;
        case TSIR_ERR_UNSUPPORTED: return DOE_COMPILER_HOST_UNSUPPORTED;
        case TSIR_ERR_OVERFLOW:
        case TSIR_ERR_RANGE: return DOE_COMPILER_HOST_LIMIT_EXCEEDED;
        case TSIR_ERR_REJECTED: return DOE_COMPILER_HOST_REJECTED;
        case TSIR_ERR_INVALID:
            return error != NULL && error->system_error != 0
                ? DOE_COMPILER_HOST_IO_ERROR : DOE_COMPILER_HOST_COMPILE_FAILED;
    }
    return DOE_COMPILER_HOST_COMPILE_FAILED;
}

static void publish(TsirBuffer *buffer, DoeCompilerHostBuffer *output) {
    output->data = (uint8_t *)buffer->data;
    output->size = buffer->len;
    buffer->data = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

DoeCompilerHostStatus doe_compiler_host_tsir_manifest(uint32_t abi_version,
    const char *repository_root, DoeCompilerHostBuffer *output,
    size_t output_size, DoeCompilerHostDiagnostic *diagnostic,
    size_t diagnostic_size) {
    TsirBuffer buffer;
    TsirBootstrapManifestError error = {0};
    DoeCompilerHostStatus status = doe_host_preflight(abi_version, output,
        output_size, diagnostic, diagnostic_size);
    if (status != DOE_COMPILER_HOST_OK) {
        return status;
    }
    if (repository_root == NULL) {
        return DOE_COMPILER_HOST_INVALID_ARGUMENT;
    }
    tsir_buf_init(&buffer);
    status = tsir_status(tsir_bootstrap_manifest_generate(repository_root,
        &buffer, &error), &error, diagnostic);
    if (status == DOE_COMPILER_HOST_OK) {
        publish(&buffer, output);
    }
    tsir_buf_free(&buffer);
    return status;
}

DoeCompilerHostStatus doe_compiler_host_tsir_source(uint32_t abi_version,
    const char *repository_root, const char *kernel, const char *source,
    size_t source_size, DoeCompilerHostBuffer *output, size_t output_size,
    DoeCompilerHostDiagnostic *diagnostic, size_t diagnostic_size) {
    TsirBuffer buffer;
    TsirBootstrapManifestError error = {0};
    TsirStatus result;
    DoeCompilerHostStatus status = doe_host_preflight(abi_version, output,
        output_size, diagnostic, diagnostic_size);
    if (status != DOE_COMPILER_HOST_OK) {
        return status;
    }
    if (repository_root == NULL || kernel == NULL || source == NULL) {
        return DOE_COMPILER_HOST_INVALID_ARGUMENT;
    }
    tsir_buf_init(&buffer);
    tsir_buf_puts(&buffer, "[");
    result = tsir_bootstrap_manifest_source(repository_root, kernel, source,
        source_size, &buffer, &error);
    if (result == TSIR_OK) {
        tsir_buf_puts(&buffer, "]\n");
        result = buffer.err;
    }
    status = tsir_status(result, &error, diagnostic);
    if (status == DOE_COMPILER_HOST_OK) {
        publish(&buffer, output);
    }
    tsir_buf_free(&buffer);
    return status;
}

DoeCompilerHostStatus doe_compiler_host_tsir_oracle(uint32_t abi_version,
    const char *kernel, const char *inputs_json, size_t inputs_size,
    DoeCompilerHostBuffer *output, size_t output_size,
    DoeCompilerHostDiagnostic *diagnostic, size_t diagnostic_size) {
    char *json = NULL;
    size_t size = 0;
    DoeCompilerHostStatus status = doe_host_preflight(abi_version, output,
        output_size, diagnostic, diagnostic_size);
    if (status != DOE_COMPILER_HOST_OK) {
        return status;
    }
    if (kernel == NULL || inputs_json == NULL) {
        return DOE_COMPILER_HOST_INVALID_ARGUMENT;
    }
    status = tsir_status(tsir_bootstrap_oracle_run(kernel, inputs_json,
        inputs_size, &json, &size), NULL, diagnostic);
    if (status == DOE_COMPILER_HOST_OK) {
        output->data = (uint8_t *)json;
        output->size = size;
    }
    return status;
}
