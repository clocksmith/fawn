/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "doe_futures.h"
void evaluator_c_layout(size_t out[8]);
void evaluator_c_layout(size_t out[8]) {
    out[0] = sizeof(doe_future_wait_info);
    out[1] = _Alignof(doe_future_wait_info);
    out[2] = offsetof(doe_future_wait_info, id);
    out[3] = offsetof(doe_future_wait_info, completed);
    out[4] = sizeof(doe_future_storage_requirements);
    out[5] = _Alignof(doe_future_storage_requirements);
    out[6] = offsetof(doe_future_storage_requirements, size);
    out[7] = offsetof(doe_future_storage_requirements, alignment);
}
