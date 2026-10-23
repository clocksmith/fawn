/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "support.h"
int main(void) {
    doe_future_queue *q = new_queue(); uint64_t id = next_id(q); uint32_t completed;
    CHECK(poll(q, id, &completed) == DOE_FUTURE_OK && completed == 1);
    drop_queue(q); report("independent_c11_prebuilt_archive_consumer"); return 0;
}
