/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_emit.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    if (module == NULL || lowered == NULL) {
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
    dxil_diagnostic_clear(diagnostic);
    if (out == NULL || written == NULL) {
        return DXIL_INVALID_IR;
    }
    status = dxil_native_emit(module, out, out_len, &len);
    if (status != DXIL_OK) {
        return status;
    }
    status = validate_gate(out, len, "native emitter", diagnostic);
    if (status != DXIL_OK) {
        return status;
    }
    *written = len;
    return DXIL_OK;
}

DxilStatus dxil_emit(const DxilIrModule *module, uint8_t *out,
    size_t out_len, size_t *written, DxilDiagnostic *diagnostic) {
    return dxil_emit_with_diagnostic(module, out, out_len, written,
        diagnostic);
}

static DxilStatus emit_toolchain_impl(const DxilIrModule *module,
    uint8_t *out, size_t out_len, const DxilToolchainConfig *config,
    const char *cache_dir, size_t *written,
    DxilDiagnostic *diagnostic) {
    DxilLoweredModule lowered;
    char hlsl_buf[DXIL_EMIT_MAX_HLSL];
    uint8_t dxil_buf[DXIL_EMIT_MAX_DXIL];
    char profile[DXIL_DXC_PROFILE_CAP];
    char errbuf[2048];
    char hlsl_path[1024];
    char dxil_path[1024];
    DxilStatus status = DXIL_OK;
    size_t hlsl_len = 0;
    size_t dxil_len = 0;
    const char *dir = NULL;
    int attempt = 0;
    int hlsl_fd = -1;
    bool files_created = false;
    dxil_diagnostic_clear(diagnostic);
    if (out == NULL || written == NULL || config == NULL) {
        return DXIL_INVALID_IR;
    }
    if (config->executable == NULL || config->executable[0] == '\0') {
        dxil_diagnostic_set_detail(diagnostic,
            DXIL_DIAG_STAGE_DXIL_EMIT, DXIL_DIAG_INVALID_IR,
            "DXC executable path is empty; pass ToolchainConfig{ "
            ".executable = ... } or set DOE_WGSL_DXC to a DXC path "
            "or PATH");
        return DXIL_SHADER_TOOLCHAIN_UNAVAILABLE;
    }
    status = dxil_lower(module, &lowered);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_hlsl_emit(module, hlsl_buf, sizeof(hlsl_buf),
        &hlsl_len);
    if (status != DXIL_OK) {
        return status;
    }
    dir = cache_dir == NULL ? DXIL_EMIT_CACHE_DIR : cache_dir;
    if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
        return DXIL_INVALID_IR;
    }
    for (attempt = 0; attempt < 1000; attempt++) {
        int written_path = snprintf(hlsl_path, sizeof(hlsl_path),
            "%s/doe_wgsl_dxil_%d_%d.hlsl", dir, (int)getpid(),
            attempt);
        int written_dxil = 0;
        if (written_path < 0
            || (size_t)written_path >= sizeof(hlsl_path)) {
            return DXIL_INVALID_IR;
        }
        written_dxil = snprintf(dxil_path, sizeof(dxil_path),
            "%s/doe_wgsl_dxil_%d_%d.dxil", dir, (int)getpid(),
            attempt);
        if (written_dxil < 0
            || (size_t)written_dxil >= sizeof(dxil_path)) {
            return DXIL_INVALID_IR;
        }
        hlsl_fd = open(hlsl_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (hlsl_fd >= 0) {
            break;
        }
        if (errno != EEXIST) {
            return DXIL_INVALID_IR;
        }
    }
    if (hlsl_fd < 0) {
        return DXIL_INVALID_IR;
    }
    files_created = true;
    {
        size_t offset = 0;
        while (offset < hlsl_len) {
            ssize_t chunk = write(hlsl_fd, hlsl_buf + offset,
                hlsl_len - offset);
            if (chunk <= 0) {
                char detail[1100];
                snprintf(detail, sizeof(detail),
                    "failed to write temporary HLSL input `%s`: %s",
                    hlsl_path, strerror(errno));
                dxil_diagnostic_set_detail(diagnostic,
                    DXIL_DIAG_STAGE_DXIL_EMIT, DXIL_DIAG_INVALID_IR,
                    detail);
                close(hlsl_fd);
                unlink(hlsl_path);
                unlink(dxil_path);
                return DXIL_INVALID_IR;
            }
            offset += (size_t)chunk;
        }
    }
    close(hlsl_fd);
    status = dxil_toolchain_profile(lowered.stage,
        lowered.shader_model_major, lowered.shader_model_minor,
        profile, sizeof(profile));
    if (status != DXIL_OK) {
        goto cleanup;
    }
    status = dxil_dxc_run(config->executable, profile, lowered.name,
        dxil_path, hlsl_path, config->discovery, errbuf,
        sizeof(errbuf));
    if (status != DXIL_OK) {
        dxil_diagnostic_set_detail(diagnostic,
            DXIL_DIAG_STAGE_DXIL_EMIT, DXIL_DIAG_INVALID_IR, errbuf);
        goto cleanup;
    }
    {
        struct stat st;
        FILE *file = NULL;
        size_t read_total = 0;
        if (stat(dxil_path, &st) != 0) {
            char detail[1100];
            if (errno == ENOENT) {
                snprintf(detail, sizeof(detail),
                    "DXC reported success via %s `%s` but did not "
                    "write `%s`",
                    dxil_discovery_label(config->discovery),
                    config->executable, dxil_path);
            } else {
                snprintf(detail, sizeof(detail),
                    "failed to read DXIL output `%s`: %s", dxil_path,
                    strerror(errno));
            }
            dxil_diagnostic_set_detail(diagnostic,
                DXIL_DIAG_STAGE_DXIL_EMIT, DXIL_DIAG_INVALID_IR,
                detail);
            status = DXIL_INVALID_IR;
            goto cleanup;
        }
        if (st.st_size < 0
            || (uint64_t)st.st_size > DXIL_EMIT_MAX_DXIL) {
            char detail[1100];
            snprintf(detail, sizeof(detail),
                "DXIL output `%s` exceeded the %u-byte contract",
                dxil_path, DXIL_EMIT_MAX_DXIL);
            dxil_diagnostic_set_detail(diagnostic,
                DXIL_DIAG_STAGE_DXIL_EMIT, DXIL_DIAG_INVALID_IR,
                detail);
            status = DXIL_OUTPUT_TOO_LARGE;
            goto cleanup;
        }
        file = fopen(dxil_path, "rb");
        if (file == NULL) {
            char detail[1100];
            snprintf(detail, sizeof(detail),
                "failed to read DXIL output `%s`: %s", dxil_path,
                strerror(errno));
            dxil_diagnostic_set_detail(diagnostic,
                DXIL_DIAG_STAGE_DXIL_EMIT, DXIL_DIAG_INVALID_IR,
                detail);
            status = DXIL_INVALID_IR;
            goto cleanup;
        }
        read_total = fread(dxil_buf, 1, sizeof(dxil_buf), file);
        if (ferror(file)) {
            char detail[1100];
            snprintf(detail, sizeof(detail),
                "failed to read DXIL output `%s`: %s", dxil_path,
                strerror(errno));
            dxil_diagnostic_set_detail(diagnostic,
                DXIL_DIAG_STAGE_DXIL_EMIT, DXIL_DIAG_INVALID_IR,
                detail);
            fclose(file);
            status = DXIL_INVALID_IR;
            goto cleanup;
        }
        fclose(file);
        dxil_len = read_total;
        if (dxil_len > out_len) {
            status = DXIL_OUTPUT_TOO_LARGE;
            goto cleanup;
        }
        status = validate_gate(dxil_buf, dxil_len, "DXC", diagnostic);
        if (status != DXIL_OK) {
            goto cleanup;
        }
        memcpy(out, dxil_buf, dxil_len);
        *written = dxil_len;
        status = DXIL_OK;
    }
cleanup:
    if (files_created) {
        unlink(hlsl_path);
        unlink(dxil_path);
    }
    return status;
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
