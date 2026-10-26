/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_translate.h"

#include "dxil_analyze.h"
#include "dxil_source.h"
#include <errno.h>
#include <stdlib.h>

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
    const int saved_errno = errno;
    DxilIrModule *module = malloc(sizeof(*module));
    char name_buf[16384];
    DxilDiagnostic emission;
    DxilTranslateStatus analyze_error = DXIL_DIAG_INVALID_IR;
    bool success = false;
    if (module == NULL) {
        dxil_diagnostic_set_detail(diagnostic, DXIL_DIAG_STAGE_IR_BUILDER,
            DXIL_DIAG_OUT_OF_MEMORY, "native DXIL module allocation failed");
        if (out_error != NULL) *out_error = DXIL_DIAG_OUT_OF_MEMORY;
        errno = saved_errno;
        return false;
    }
    if (!dxil_analyze(source, source_len, module, name_buf,
            sizeof(name_buf), diagnostic, &analyze_error)) {
        if (out_error != NULL) *out_error = analyze_error;
    } else {
        dxil_diagnostic_clear(&emission);
        const DxilStatus status = dxil_emit_with_diagnostic(module, out, out_len,
            written, &emission);
        success = status == DXIL_OK;
        if (!success) (void)fail_emit(status, &emission, diagnostic, out_error);
    }
    free(module);
    errno = saved_errno;
    return success;
}

bool dxil_translate_toolchain_with_diagnostic(const char *source,
    size_t source_len, uint8_t *out, size_t out_len,
    const DxilToolchainConfig *config, const char *cache_dir,
    size_t *written, DxilDiagnostic *diagnostic,
    DxilTranslateStatus *out_error) {
    DxilSourceHlsl compiled;
    DxilDiagnostic emission;
    if (!dxil_source_hlsl(source, source_len, &compiled, diagnostic, out_error))
        return false;
    dxil_diagnostic_clear(&emission);
    const DxilStatus status = dxil_emit_hlsl_toolchain(&compiled.entry,
        compiled.text, compiled.text_length, out, out_len, config, cache_dir,
        written, &emission);
    dxil_source_hlsl_free(&compiled);
    if (status != DXIL_OK) return fail_emit(status, &emission, diagnostic, out_error);
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
