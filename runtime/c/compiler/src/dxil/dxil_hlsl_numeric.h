/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_HLSL_NUMERIC_H
#define DOE_DXIL_HLSL_NUMERIC_H
#include "dxil_hlsl_internal.h"
/* Serialize the retained typed numeric expressions. Unknown/noninteger-dot
 * calls leave handled false for the ordinary builtin owner. */
DxilStatus dxil_hlsl_emit_numeric_builtin(DxilHlslEmitter *, const DxilIrFunction *,
    DxilIrTypeId, const char *, DxilIrRange, bool *handled);
DxilStatus dxil_hlsl_emit_numeric_helpers(DxilHlslEmitter *);
#endif
