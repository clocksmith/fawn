/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_ERROR_H
#define DOE_DXIL_ERROR_H

/* Shared DXIL status set. Mirrors the Zig EmitError set from dxil_spec.zig
 * plus the ShaderToolchainUnavailable error from emit_dxil.zig. The first
 * error encountered is the reported one: functions return immediately on
 * failure and never overwrite a pending status. */
typedef enum DxilStatus {
    DXIL_OK = 0,
    DXIL_OUT_OF_MEMORY = 1,
    DXIL_OUTPUT_TOO_LARGE = 2,
    DXIL_UNSUPPORTED_CONSTRUCT = 3,
    DXIL_INVALID_IR = 4,
    DXIL_UNSUPPORTED_BUILTIN = 5,
    DXIL_SHADER_TOOLCHAIN_UNAVAILABLE = 6
} DxilStatus;

/* Stable, human-readable name for a status. Never returns NULL. */
const char *dxil_status_name(DxilStatus status);

#endif /* DOE_DXIL_ERROR_H */
