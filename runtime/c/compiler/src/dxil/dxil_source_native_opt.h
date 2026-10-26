/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_SOURCE_NATIVE_OPT_H
#define DOE_DXIL_SOURCE_NATIVE_OPT_H
#include "dxil_source_native_internal.h"

/* Rewrite an already typed, fully projected function. Borrows all storage and
 * allocates nothing. Alias discovery follows immutable declarations only;
 * numeric evaluation remains owned by the shared scalar numeric helpers. */
bool dxil_source_native_optimize(DxilSourceBridge *, DxilSourceFunction *);
#endif
