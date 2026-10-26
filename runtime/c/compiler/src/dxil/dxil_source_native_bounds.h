/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_SOURCE_NATIVE_BOUNDS_H
#define DOE_DXIL_SOURCE_NATIVE_BOUNDS_H
#include "dxil_source_native_internal.h"

/* Project the baseline native clamp graph using canonical typed extents and
 * known constants. Does not claim optional proof/dispatch-derived elision. */
uint32_t dxil_source_native_bound_index(DxilSourceBridge *, DxilSourceFunction *,
    uint32_t index_expression, uint32_t source_index);

#endif
