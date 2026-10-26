/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_translate.h"

#include "dxil_analyze.h"

DxilTranslateStatus dxil_translate_map_emit_error(DxilStatus status) {
    switch (status) {
        case DXIL_OK:
            return DXIL_DIAG_INVALID_IR;
        case DXIL_OUT_OF_MEMORY:
            return DXIL_DIAG_OUT_OF_MEMORY;
        case DXIL_OUTPUT_TOO_LARGE:
            return DXIL_DIAG_OUTPUT_TOO_LARGE;
        case DXIL_UNSUPPORTED_CONSTRUCT:
            return DXIL_DIAG_UNSUPPORTED_CONSTRUCT;
        case DXIL_INVALID_IR:
            return DXIL_DIAG_INVALID_IR;
        case DXIL_UNSUPPORTED_BUILTIN:
            return DXIL_DIAG_UNSUPPORTED_BUILTIN;
        case DXIL_SHADER_TOOLCHAIN_UNAVAILABLE:
            return DXIL_DIAG_SHADER_TOOLCHAIN_UNAVAILABLE;
    }
    return DXIL_DIAG_INVALID_IR;
}

static bool fail_emit(DxilStatus emit_status,
    const DxilDiagnostic *emission, DxilDiagnostic *diagnostic,
    DxilTranslateStatus *out_error) {
    DxilTranslateStatus mapped =
        dxil_translate_map_emit_error(emit_status);
    if (emission != NULL && emission->message_len != 0) {
        dxil_diagnostic_set_detail(diagnostic,
            DXIL_DIAG_STAGE_DXIL_EMIT, mapped, emission->message);
    } else {
        dxil_diagnostic_set(diagnostic, DXIL_DIAG_STAGE_DXIL_EMIT,
            mapped, NULL, 0, 0);
    }
    if (out_error != NULL) {
        *out_error = mapped;
    }
    return false;
}

bool dxil_translate_with_diagnostic(const char *source,
    size_t source_len, uint8_t *out, size_t out_len, size_t *written,
    DxilDiagnostic *diagnostic, DxilTranslateStatus *out_error) {
    DxilIrModule module;
    char name_buf[16384];
    DxilDiagnostic emission;
    DxilTranslateStatus analyze_error = DXIL_DIAG_INVALID_IR;
    DxilStatus emit_status = DXIL_OK;
    if (!dxil_analyze(source, source_len, &module, name_buf,
            sizeof(name_buf), diagnostic, &analyze_error)) {
        if (out_error != NULL) {
            *out_error = analyze_error;
        }
        return false;
    }
    dxil_diagnostic_clear(&emission);
    emit_status = dxil_emit_with_diagnostic(&module, out, out_len,
        written, &emission);
    if (emit_status != DXIL_OK) {
        return fail_emit(emit_status, &emission, diagnostic,
            out_error);
    }
    return true;
}

bool dxil_translate_toolchain_with_diagnostic(const char *source,
    size_t source_len, uint8_t *out, size_t out_len,
    const DxilToolchainConfig *config, const char *cache_dir,
    size_t *written, DxilDiagnostic *diagnostic,
    DxilTranslateStatus *out_error) {
    DxilIrModule module;
    char name_buf[16384];
    DxilDiagnostic emission;
    DxilTranslateStatus analyze_error = DXIL_DIAG_INVALID_IR;
    DxilStatus emit_status = DXIL_OK;
    if (!dxil_analyze(source, source_len, &module, name_buf,
            sizeof(name_buf), diagnostic, &analyze_error)) {
        if (out_error != NULL) {
            *out_error = analyze_error;
        }
        return false;
    }
    dxil_diagnostic_clear(&emission);
    emit_status = dxil_emit_toolchain_with_diagnostic(&module, out,
        out_len, config, cache_dir, written, &emission);
    if (emit_status != DXIL_OK) {
        return fail_emit(emit_status, &emission, diagnostic,
            out_error);
    }
    return true;
}

bool dxil_translate(const char *source, size_t source_len, uint8_t *out,
    size_t out_len, size_t *written, DxilDiagnostic *diagnostic,
    DxilTranslateStatus *out_error) {
    return dxil_translate_with_diagnostic(source, source_len, out,
        out_len, written, diagnostic, out_error);
}

bool dxil_translate_toolchain(const char *source, size_t source_len,
    uint8_t *out, size_t out_len, const DxilToolchainConfig *config,
    const char *cache_dir, size_t *written,
    DxilDiagnostic *diagnostic, DxilTranslateStatus *out_error) {
    return dxil_translate_toolchain_with_diagnostic(source,
        source_len, out, out_len, config, cache_dir, written,
        diagnostic, out_error);
}
