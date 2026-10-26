/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_PROCESS_H
#define DOE_DXIL_PROCESS_H

#include "dxil_toolchain.h"

/* Shared discovery diagnostic; no process state or caller storage is retained. */
void dxil_dxc_missing_message(DxilDiscovery discovery,
    const char *executable, char *out, size_t out_cap);

#endif
