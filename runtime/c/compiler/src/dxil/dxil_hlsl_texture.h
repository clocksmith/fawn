/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_HLSL_TEXTURE_H
#define DOE_DXIL_HLSL_TEXTURE_H

#include "dxil_hlsl_internal.h"

/* Typed target serialization only. Inputs borrow the existing IR; output and
 * first-error propagation belong to the shared HLSL writer. Unknown names are
 * not handled. Invalid operands and unsupported texture forms fail visibly. */
DxilStatus dxil_hlsl_emit_texture_builtin(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrTypeId result_type,
    const char *name, DxilIrRange args, bool *handled);
DxilStatus dxil_hlsl_emit_texture_helpers(DxilHlslEmitter *emitter);

#endif /* DOE_DXIL_HLSL_TEXTURE_H */
