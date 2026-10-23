/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "doe_futures.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL consumer.cpp:%d: %s\n", __LINE__, #x); std::abort(); } } while (0)
extern "C" void evaluator_c_layout(size_t out[8]);
static_assert(std::is_standard_layout<doe_future_wait_info>::value, "C layout");
static_assert(DOE_FUTURE_MODE_COMPAT_IMMEDIATE == 0 && DOE_FUTURE_MODE_WAIT_ANY_ONLY == 1 && DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS == 2 && DOE_FUTURE_MODE_ALLOW_SPONTANEOUS == 3, "Pinned modes");
static_assert(DOE_FUTURE_OK == 0 && DOE_FUTURE_TIMED_OUT == 1 && DOE_FUTURE_INVALID_ARGUMENT == 2 && DOE_FUTURE_UNSUPPORTED_VERSION == 3 && DOE_FUTURE_INSUFFICIENT_STORAGE == 4 && DOE_FUTURE_MISALIGNED_STORAGE == 5 && DOE_FUTURE_UNKNOWN_ID == 6 && DOE_FUTURE_DUPLICATE == 7 && DOE_FUTURE_BUSY == 8 && DOE_FUTURE_EXHAUSTED == 9 && DOE_FUTURE_SYSTEM_ERROR == 10 && DOE_FUTURE_INVALID_STATE == 11 && DOE_FUTURE_TIME_OVERFLOW == 12 && DOE_FUTURE_CLEANUP_REQUIRED == 13, "Pinned status ABI");
static void *allocate(doe_future_storage_requirements r) {
    size_t alignment = r.alignment < alignof(std::max_align_t) ? alignof(std::max_align_t) : r.alignment;
    void *storage = std::aligned_alloc(alignment, (r.size + alignment - 1) / alignment * alignment);
    CHECK(storage != nullptr); return storage;
}
extern "C" void consume_callback(doe_future_completion *n, void *data) {
    auto *calls = static_cast<unsigned *>(data); ++*calls;
    CHECK(doe_future_finish(n) == DOE_FUTURE_OK);
}
int main() {
    const size_t expected[8] = {sizeof(doe_future_wait_info), alignof(doe_future_wait_info), offsetof(doe_future_wait_info, id), offsetof(doe_future_wait_info, completed), sizeof(doe_future_storage_requirements), alignof(doe_future_storage_requirements), offsetof(doe_future_storage_requirements, size), offsetof(doe_future_storage_requirements, alignment)};
    size_t actual[8] = {}; evaluator_c_layout(actual);
    for (unsigned i = 0; i < 8; ++i) CHECK(actual[i] == expected[i]);
    doe_future_storage_requirements qr{}, nr{};
    CHECK(doe_future_queue_requirements(DOE_FUTURES_ABI_VERSION, &qr) == DOE_FUTURE_OK);
    CHECK(doe_future_completion_requirements(DOE_FUTURES_ABI_VERSION, &nr) == DOE_FUTURE_OK);
    void *queue_storage = allocate(qr), *node_storage = allocate(nr);
    doe_future_queue *q = nullptr; doe_future_completion *n = nullptr; unsigned calls = 0;
    CHECK(doe_future_queue_init(DOE_FUTURES_ABI_VERSION, queue_storage, qr.size, &q) == DOE_FUTURE_OK);
    CHECK(doe_future_completion_init(DOE_FUTURES_ABI_VERSION, node_storage, nr.size, DOE_FUTURE_MODE_WAIT_ANY_ONLY, consume_callback, &calls, &n) == DOE_FUTURE_OK);
    uint64_t id = 0; CHECK(doe_future_new_id(q, &id) == DOE_FUTURE_OK);
    CHECK(doe_future_register(q, n, id) == DOE_FUTURE_OK);
    CHECK(doe_future_mark_ready(n) == DOE_FUTURE_OK);
    doe_future_wait_info info{id, 0}; CHECK(doe_future_wait_any(q, &info, 1, 0) == DOE_FUTURE_OK);
    CHECK(info.completed == 1 && calls == 1);
    CHECK(doe_future_completion_deinit(n) == DOE_FUTURE_OK);
    CHECK(doe_future_queue_deinit(q) == DOE_FUTURE_OK);
    std::free(node_storage); std::free(queue_storage);
    std::puts("PASS restricted_cpp17_static_library/c_linkage/public_layout");
    return 0;
}
