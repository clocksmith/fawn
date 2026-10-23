/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Compiled with tiny build-only endpoints; no mutation of private state. */
#include "doe_futures.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    doe_future_storage_requirements req;
    doe_future_queue *queue;
    uint64_t id = 0, first = 0;
    assert(doe_future_queue_requirements(1, &req) == DOE_FUTURE_OK);
    void *storage = malloc(req.size);
    assert(storage != NULL);
    for (unsigned instance = 1; instance <= 2; ++instance) {
        assert(doe_future_queue_init(1, storage, req.size, &queue) == DOE_FUTURE_OK);
        for (unsigned sequence = 1; sequence <= 2; ++sequence) {
            assert(doe_future_new_id(queue, &id) == DOE_FUTURE_OK);
            assert(id == ((uint64_t)instance << 32 | sequence));
            if (first == 0) first = id;
        }
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
            id = 17;
            assert(doe_future_new_id(queue, &id) == DOE_FUTURE_EXHAUSTED && id == 17);
        }
        assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    }
    assert(doe_future_queue_init(1, storage, req.size, &queue) == DOE_FUTURE_OK);
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        id = 17;
        assert(doe_future_new_id(queue, &id) == DOE_FUTURE_EXHAUSTED && id == 17);
    }
    doe_future_wait_info stale = {first, 0};
    assert(doe_future_wait_any(queue, &stale, 1, 0) == DOE_FUTURE_UNKNOWN_ID);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    free(storage);
    for (unsigned sequence = 0; sequence <= 1; ++sequence) {
        assert(doe_future_legacy_id(&id) == DOE_FUTURE_OK);
        assert(id == UINT64_C(0x8000000000000000) + sequence);
    }
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        id = 17;
        assert(doe_future_legacy_id(&id) == DOE_FUTURE_EXHAUSTED && id == 17);
    }
    puts("Developer build-only namespace exhaustion passed.");
    return 0;
}
