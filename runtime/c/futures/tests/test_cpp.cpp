/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "doe_futures.h"
#include <cassert>
#include <cstdlib>

int main()
{
    doe_future_storage_requirements requirements{};
    assert(doe_future_queue_requirements(DOE_FUTURES_ABI_VERSION, &requirements) ==
           DOE_FUTURE_OK);
    void *storage = std::malloc(requirements.size);
    assert(storage != nullptr);
    doe_future_queue *queue = nullptr;
    assert(doe_future_queue_init(DOE_FUTURES_ABI_VERSION, storage,
           requirements.size, &queue) == DOE_FUTURE_OK);
    uint64_t id = 0;
    assert(doe_future_new_id(queue, &id) == DOE_FUTURE_OK);
    doe_future_wait_info info{id, 0};
    assert(doe_future_wait_any(queue, &info, 1, 0) == DOE_FUTURE_OK);
    assert(info.completed == 1);
    assert(doe_future_queue_deinit(queue) == DOE_FUTURE_OK);
    std::free(storage);
}
