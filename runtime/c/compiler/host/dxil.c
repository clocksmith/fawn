/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include "../src/dxil/dxil_translate.h"

#include <stdlib.h>

static DoeCompilerHostStatus translate_status(DxilTranslateStatus status) {
    switch (status) {
        case DXIL_DIAG_OUT_OF_MEMORY: return DOE_COMPILER_HOST_OUT_OF_MEMORY;
        case DXIL_DIAG_OUTPUT_TOO_LARGE: return DOE_COMPILER_HOST_LIMIT_EXCEEDED;
        case DXIL_DIAG_SHADER_TOOLCHAIN_UNAVAILABLE:
            return DOE_COMPILER_HOST_TOOLCHAIN_UNAVAILABLE;
        case DXIL_DIAG_UNSUPPORTED_BUILTIN:
        case DXIL_DIAG_UNSUPPORTED_CONSTRUCT:
        case DXIL_DIAG_UNSUPPORTED_PATTERN:
        case DXIL_DIAG_UNSUPPORTED_WGSL: return DOE_COMPILER_HOST_UNSUPPORTED;
        case DXIL_DIAG_INVALID_WGSL:
        case DXIL_DIAG_INVALID_IR:
        case DXIL_DIAG_DUPLICATE_SYMBOL:
        case DXIL_DIAG_INVALID_ATTRIBUTE:
        case DXIL_DIAG_INVALID_TYPE:
        case DXIL_DIAG_UNEXPECTED_TOKEN:
        case DXIL_DIAG_TYPE_MISMATCH:
        case DXIL_DIAG_UNKNOWN_IDENTIFIER:
        case DXIL_DIAG_UNKNOWN_TYPE: return DOE_COMPILER_HOST_COMPILE_FAILED;
    }
    return DOE_COMPILER_HOST_COMPILE_FAILED;
}

DoeCompilerHostStatus doe_compiler_host_dxil(uint32_t abi_version,
    const char *source, size_t source_size, const char *dxc_executable,
    const char *cache_directory, DoeCompilerHostBuffer *output,
    size_t output_size, DoeCompilerHostDiagnostic *diagnostic,
    size_t diagnostic_size) {
    DoeCompilerHostStatus status = doe_host_preflight(abi_version, output,
        output_size, diagnostic, diagnostic_size);
    DxilDiagnostic detail;
    DxilTranslateStatus error = DXIL_DIAG_INVALID_IR;
    DxilToolchainConfig config;
    uint8_t *bytes;
    size_t written = 0;
    if (status != DOE_COMPILER_HOST_OK) {
        return status;
    }
    if (source == NULL || dxc_executable == NULL || *dxc_executable == '\0' ||
        cache_directory == NULL || *cache_directory == '\0') {
        return DOE_COMPILER_HOST_INVALID_ARGUMENT;
    }
    bytes = malloc(DXIL_TRANSLATE_MAX_OUTPUT);
    if (bytes == NULL) {
        return DOE_COMPILER_HOST_OUT_OF_MEMORY;
    }
    config.executable = dxc_executable;
    config.discovery = DXIL_DISCOVERY_EXPLICIT_CONFIG;
    dxil_diagnostic_clear(&detail);
    if (!dxil_translate_toolchain_with_diagnostic(source, source_size,
            bytes, DXIL_TRANSLATE_MAX_OUTPUT, &config, cache_directory,
            &written, &detail, &error)) {
        free(bytes);
        doe_host_diagnostic(diagnostic, 2, (uint32_t)error,
            dxil_diagnostic_stage_name(detail.stage), detail.message);
        if (diagnostic != NULL) {
            diagnostic->line = detail.line;
            diagnostic->column = detail.column;
        }
        return translate_status(error);
    }
    output->data = bytes;
    output->size = written;
    return DOE_COMPILER_HOST_OK;
}
