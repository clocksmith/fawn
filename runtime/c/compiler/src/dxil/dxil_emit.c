/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_emit.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "dxil_files.h"
#include "dxil_hlsl.h"
#include "dxil_native.h"
#include "dxil_validate.h"

#define DXIL_EMIT_CACHE_DIR ".zig-cache/doe_wgsl_dxil"
#define DXIL_EMIT_MAX_HLSL (256u * 1024u)
#define DXIL_EMIT_MAX_DXIL (256u * 1024u)

DxilStatus dxil_lower(const DxilIrModule *module,
    DxilLoweredModule *lowered) {
    const DxilIrEntryPoint *entry = NULL;
    const DxilIrFunction *function = NULL;
    if (module == NULL || lowered == NULL || dxil_ir_validate(module) != DXIL_OK) {
        return DXIL_INVALID_IR;
    }
    if (module->entry_point_count == 0) {
        return DXIL_INVALID_IR;
    }
    entry = &module->entry_points[0];
    if (entry->function >= module->function_count) {
        return DXIL_INVALID_IR;
    }
    function = &module->functions[entry->function];
    lowered->stage = entry->stage == DXIL_IR_STAGE_COMPUTE
        ? DXIL_STAGE_COMPUTE
        : entry->stage == DXIL_IR_STAGE_VERTEX
            ? DXIL_STAGE_VERTEX
            : DXIL_STAGE_FRAGMENT;
    lowered->name = function->name;
    lowered->workgroup_size[0] = entry->workgroup_size[0];
    lowered->workgroup_size[1] = entry->workgroup_size[1];
    lowered->workgroup_size[2] = entry->workgroup_size[2];
    lowered->shader_model_major = 6;
    lowered->shader_model_minor = 0;
    lowered->native16 = false;
    return DXIL_OK;
}

static DxilStatus validate_gate(const uint8_t *bytes, size_t len,
    const char *producer, DxilDiagnostic *diagnostic) {
    DxilValidationResult result;
    char detail[512];
    dxil_validate(bytes, len, &result);
    if (result.valid) {
        return DXIL_OK;
    }
    snprintf(detail, sizeof(detail),
        "%s produced a structurally invalid DXIL container: %s",
        producer,
        result.error_message == NULL ? "unknown validation failure"
            : result.error_message);
    dxil_diagnostic_set_detail(diagnostic, DXIL_DIAG_STAGE_DXIL_EMIT,
        DXIL_DIAG_INVALID_IR, detail);
    return DXIL_INVALID_IR;
}

DxilStatus dxil_emit_with_diagnostic(const DxilIrModule *module,
    uint8_t *out, size_t out_len, size_t *written,
    DxilDiagnostic *diagnostic) {
    DxilStatus status = DXIL_OK;
    size_t len = 0;
    uint8_t staged[DXIL_EMIT_MAX_DXIL];
    dxil_diagnostic_clear(diagnostic);
    if (out == NULL || written == NULL) {
        return DXIL_INVALID_IR;
    }
    status = dxil_native_emit(module, staged, sizeof(staged), &len);
    if (status != DXIL_OK) {
        return status;
    }
    if (len > out_len) return DXIL_OUTPUT_TOO_LARGE;
    status = validate_gate(staged, len, "native emitter", diagnostic);
    if (status != DXIL_OK) {
        return status;
    }
    memcpy(out, staged, len);
    *written = len;
    return DXIL_OK;
}

DxilStatus dxil_emit(const DxilIrModule *module, uint8_t *out,
    size_t out_len, size_t *written, DxilDiagnostic *diagnostic) {
    return dxil_emit_with_diagnostic(module, out, out_len, written,
        diagnostic);
}

static DxilStatus check_toolchain(const DxilToolchainConfig *config,
    DxilDiagnostic *diagnostic) {
    if (config->executable == NULL || config->executable[0] == '\0') {
        dxil_diagnostic_set_detail(diagnostic,
            DXIL_DIAG_STAGE_DXIL_EMIT, DXIL_DIAG_INVALID_IR,
            "DXC executable path is empty; pass ToolchainConfig{ "
            ".executable = ... } or set DOE_WGSL_DXC to a DXC path "
            "or PATH");
        return DXIL_SHADER_TOOLCHAIN_UNAVAILABLE;
    }
    return DXIL_OK;
}

static void file_error(DxilDiagnostic *diagnostic, const char *operation,
    const char *path, int error) {
    char detail[1200];
    snprintf(detail, sizeof(detail), "failed to %s `%s`: %s", operation,
        path, strerror(error));
    dxil_diagnostic_set_detail(diagnostic, DXIL_DIAG_STAGE_DXIL_EMIT,
        DXIL_DIAG_INVALID_IR, detail);
}

static DxilStatus read_toolchain_output(const char *path,
    const DxilToolchainConfig *config, uint8_t *bytes, size_t capacity,
    size_t *length, DxilDiagnostic *diagnostic) {
    FILE *file = dxil_files_read(path);
    size_t count = 0u;
    int extra = EOF;
    int saved_error = 0;
    if (file == NULL) {
        if (errno == ENOENT) {
            char detail[1200];
            snprintf(detail, sizeof(detail),
                "DXC reported success via %s `%s` but did not write `%s`",
                dxil_discovery_label(config->discovery), config->executable,
                path);
            dxil_diagnostic_set_detail(diagnostic, DXIL_DIAG_STAGE_DXIL_EMIT,
                DXIL_DIAG_INVALID_IR, detail);
        } else {
            file_error(diagnostic, "read DXIL output", path, errno);
        }
        return DXIL_INVALID_IR;
    }
    errno = 0;
    count = fread(bytes, 1u, capacity, file);
    if (ferror(file)) saved_error = errno == 0 ? EIO : errno;
    if (saved_error == 0 && count == capacity) {
        errno = 0;
        extra = fgetc(file);
        if (ferror(file)) saved_error = errno == 0 ? EIO : errno;
    }
    errno = 0;
    if (fclose(file) != 0 && saved_error == 0) {
        saved_error = errno == 0 ? EIO : errno;
    }
    if (saved_error != 0) {
        file_error(diagnostic, "read DXIL output", path, saved_error);
        return DXIL_INVALID_IR;
    }
    if (extra != EOF) {
        char detail[1200];
        snprintf(detail, sizeof(detail),
            "DXIL output `%s` exceeded the %u-byte contract", path,
            DXIL_EMIT_MAX_DXIL);
        dxil_diagnostic_set_detail(diagnostic, DXIL_DIAG_STAGE_DXIL_EMIT,
            DXIL_DIAG_INVALID_IR, detail);
        return DXIL_OUTPUT_TOO_LARGE;
    }
    *length = count;
    return DXIL_OK;
}

DxilStatus dxil_emit_hlsl_toolchain(const DxilLoweredModule *lowered,
    const char *hlsl, size_t hlsl_len, uint8_t *out, size_t out_len,
    const DxilToolchainConfig *config, const char *cache_dir, size_t *written,
    DxilDiagnostic *diagnostic) {
    uint8_t dxil_buf[DXIL_EMIT_MAX_DXIL];
    char profile[DXIL_DXC_PROFILE_CAP];
    char errbuf[2048];
    char work_dir[DXIL_FILES_PATH_CAP];
    char hlsl_path[DXIL_FILES_PATH_CAP];
    char dxil_path[DXIL_FILES_PATH_CAP];
    DxilStatus status = DXIL_OK;
    size_t dxil_len = 0u;
    const char *dir = NULL;
    FILE *file = NULL;
    bool directory_created = false;
    dxil_diagnostic_clear(diagnostic);
    if (lowered == NULL || lowered->name == NULL || hlsl == NULL ||
        out == NULL || written == NULL || config == NULL) {
        return DXIL_INVALID_IR;
    }
    status = check_toolchain(config, diagnostic);
    if (status != DXIL_OK) return status;
    dir = cache_dir == NULL ? DXIL_EMIT_CACHE_DIR : cache_dir;
    if (dxil_files_mkdir(dir) != 0 && errno != EEXIST) {
        file_error(diagnostic, "create DXIL cache directory", dir, errno);
        return DXIL_INVALID_IR;
    }
    /* Owning a directory, rather than only the input name, prevents a stale
     * output from being mistaken for this invocation's result or removed. */
    for (unsigned attempt = 0u; attempt < 1000u; ++attempt) {
        int count = snprintf(work_dir, sizeof(work_dir),
            "%s/doe_wgsl_dxil_%lu_%u", dir, dxil_files_process_id(), attempt);
        if (count < 0 || (size_t)count >= sizeof(work_dir)) {
            file_error(diagnostic, "name DXIL temporary directory", dir,
                ENAMETOOLONG);
            return DXIL_INVALID_IR;
        }
        int input_count = snprintf(hlsl_path, sizeof(hlsl_path),
            "%s/input.hlsl", work_dir);
        int output_count = snprintf(dxil_path, sizeof(dxil_path),
            "%s/output.dxil", work_dir);
        if (input_count < 0 || (size_t)input_count >= sizeof(hlsl_path) ||
            output_count < 0 || (size_t)output_count >= sizeof(dxil_path)) {
            file_error(diagnostic, "name DXIL temporary file", dir,
                ENAMETOOLONG);
            return DXIL_INVALID_IR;
        }
        if (dxil_files_mkdir(work_dir) == 0) {
            directory_created = true;
            break;
        }
        if (errno != EEXIST) {
            file_error(diagnostic, "create DXIL temporary directory", work_dir,
                errno);
            return DXIL_INVALID_IR;
        }
    }
    if (!directory_created) {
        file_error(diagnostic, "create DXIL temporary directory", dir, EEXIST);
        return DXIL_INVALID_IR;
    }
    file = dxil_files_create(hlsl_path);
    if (file == NULL) {
        file_error(diagnostic, "create temporary HLSL input", hlsl_path, errno);
        status = DXIL_INVALID_IR;
        goto cleanup;
    }
    {
        int saved_error = 0;
        errno = 0;
        if (fwrite(hlsl, 1u, hlsl_len, file) != hlsl_len) {
            saved_error = errno == 0 ? EIO : errno;
        }
        errno = 0;
        if (fclose(file) != 0 && saved_error == 0) {
            saved_error = errno == 0 ? EIO : errno;
        }
        if (saved_error != 0) {
            file_error(diagnostic, "write temporary HLSL input", hlsl_path,
                saved_error);
            status = DXIL_INVALID_IR;
            goto cleanup;
        }
    }
    status = dxil_toolchain_profile(lowered->stage,
        lowered->shader_model_major, lowered->shader_model_minor,
        profile, sizeof(profile));
    if (status != DXIL_OK) goto cleanup;
    status = dxil_dxc_run_options(config->executable, profile, lowered->name,
        dxil_path, hlsl_path, config->discovery, lowered->native16, errbuf,
        sizeof(errbuf));
    if (status != DXIL_OK) {
        dxil_diagnostic_set_detail(diagnostic, DXIL_DIAG_STAGE_DXIL_EMIT,
            DXIL_DIAG_INVALID_IR, errbuf);
        goto cleanup;
    }
    status = read_toolchain_output(dxil_path, config, dxil_buf,
        sizeof(dxil_buf), &dxil_len, diagnostic);
    if (status != DXIL_OK) goto cleanup;
    if (dxil_len > out_len) {
        status = DXIL_OUTPUT_TOO_LARGE;
        goto cleanup;
    }
    status = validate_gate(dxil_buf, dxil_len, "DXC", diagnostic);
cleanup:
    /* Preserve the first failure. Cleanup failures on a successful compilation
     * are still failures and cannot publish output. Only our directory's two
     * owned names are removed; unrelated cache entries are never traversed. */
    if (dxil_files_remove(hlsl_path) != 0 && errno != ENOENT && status == DXIL_OK) {
        file_error(diagnostic, "remove temporary HLSL input", hlsl_path, errno);
        status = DXIL_INVALID_IR;
    }
    if (dxil_files_remove(dxil_path) != 0 && errno != ENOENT && status == DXIL_OK) {
        file_error(diagnostic, "remove temporary DXIL output", dxil_path, errno);
        status = DXIL_INVALID_IR;
    }
    if (dxil_files_rmdir(work_dir) != 0 && status == DXIL_OK) {
        file_error(diagnostic, "remove DXIL temporary directory", work_dir, errno);
        status = DXIL_INVALID_IR;
    }
    if (status == DXIL_OK) {
        memcpy(out, dxil_buf, dxil_len);
        *written = dxil_len;
    }
    return status;
}

static DxilStatus emit_toolchain_impl(const DxilIrModule *module,
    uint8_t *out, size_t out_len, const DxilToolchainConfig *config,
    const char *cache_dir, size_t *written, DxilDiagnostic *diagnostic) {
    DxilLoweredModule lowered;
    char hlsl[DXIL_EMIT_MAX_HLSL];
    size_t hlsl_len = 0u;
    dxil_diagnostic_clear(diagnostic);
    if (out == NULL || written == NULL || config == NULL) return DXIL_INVALID_IR;
    DxilStatus status = check_toolchain(config, diagnostic);
    if (status != DXIL_OK) return status;
    status = dxil_lower(module, &lowered);
    if (status != DXIL_OK) return status;
    status = dxil_hlsl_emit(module, hlsl, sizeof(hlsl), &hlsl_len);
    if (status != DXIL_OK) return status;
    return dxil_emit_hlsl_toolchain(&lowered, hlsl, hlsl_len, out, out_len,
        config, cache_dir, written, diagnostic);
}

DxilStatus dxil_emit_toolchain_with_diagnostic(
    const DxilIrModule *module, uint8_t *out, size_t out_len,
    const DxilToolchainConfig *config, const char *cache_dir,
    size_t *written, DxilDiagnostic *diagnostic) {
    return emit_toolchain_impl(module, out, out_len, config,
        cache_dir, written, diagnostic);
}

DxilStatus dxil_emit_toolchain(const DxilIrModule *module, uint8_t *out,
    size_t out_len, const DxilToolchainConfig *config,
    const char *cache_dir, size_t *written,
    DxilDiagnostic *diagnostic) {
    return emit_toolchain_impl(module, out, out_len, config,
        cache_dir, written, diagnostic);
}
