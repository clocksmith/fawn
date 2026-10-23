/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "support.h"
int main(void) {
    doe_future_queue *q[3] = {new_queue(), new_queue(), new_queue()};
    for (unsigned i = 0; i < 2; ++i) {
        for (uint64_t sequence = 1; sequence <= 3; ++sequence) CHECK(next_id(q[i]) == (((uint64_t)i + 1) << 32 | sequence));
        for (unsigned retry = 0; retry < 3; ++retry) {
            uint64_t out = 77; CHECK(doe_future_new_id(q[i], &out) == DOE_FUTURE_EXHAUSTED && out == 77);
        }
    }
    for (unsigned retry = 0; retry < 3; ++retry) {
        uint64_t out = 88; CHECK(doe_future_new_id(q[2], &out) == DOE_FUTURE_EXHAUSTED && out == 88);
    }
    for (uint64_t i = 0; i < 3; ++i) {
        uint64_t out = 0; OK(doe_future_legacy_id(&out)); CHECK(out == UINT64_C(0x8000000000000000) + i);
    }
    for (unsigned retry = 0; retry < 3; ++retry) {
        uint64_t out = 99; CHECK(doe_future_legacy_id(&out) == DOE_FUTURE_EXHAUSTED && out == 99);
    }
    for (unsigned i = 0; i < 3; ++i) drop_queue(q[i]);
    q[0] = new_queue(); uint64_t out = 111;
    CHECK(doe_future_new_id(q[0], &out) == DOE_FUTURE_EXHAUSTED && out == 111);
    drop_queue(q[0]); report("test_only_tiny_namespace_exhaustion_saturates_nonrecycling");
    return 0;
}
