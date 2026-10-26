/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_error.h"

const char *dxil_status_name(DxilStatus status) {
    switch (status) {
        case DXIL_OK: return "ok";
        case DXIL_OUT_OF_MEMORY: return "OutOfMemory";
        case DXIL_OUTPUT_TOO_LARGE: return "OutputTooLarge";
        case DXIL_UNSUPPORTED_CONSTRUCT: return "UnsupportedConstruct";
        case DXIL_INVALID_IR: return "InvalidIr";
        case DXIL_UNSUPPORTED_BUILTIN: return "UnsupportedBuiltin";
        case DXIL_SHADER_TOOLCHAIN_UNAVAILABLE: return "ShaderToolchainUnavailable";
    }
    return "unknown";
}
