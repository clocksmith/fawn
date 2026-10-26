/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_SOURCE_NATIVE_PACKED_H
#define DOE_DXIL_SOURCE_NATIVE_PACKED_H
#include "dxil_source_native_internal.h"

uint32_t dxil_source_native_packed(DxilSourceBridge *, DxilSourceFunction *,
    NumericBuiltinKind, const DxilIrExpr *);
#endif
