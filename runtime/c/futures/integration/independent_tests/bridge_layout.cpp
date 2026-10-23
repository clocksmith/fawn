// SPDX-License-Identifier: MIT
#include "doe_futures_zig_bridge.h"
#include <cassert>
#include <cstddef>
#include <cstdio>

static void dispatch(doe_future_completion *entry, void *userdata)
{
    auto *count = static_cast<unsigned *>(userdata);
    ++*count;
    assert(doe_future_finish(entry) == DOE_FUTURE_OK);
    assert(doe_future_completion_deinit(entry) == DOE_FUTURE_OK);
}
int main()
{
    doe_future_queue queue;
    doe_future_completion completion;
    size_t q[DOE_FUTURES_QUEUE_LAYOUT_COUNT], n[DOE_FUTURES_COMPLETION_LAYOUT_COUNT];
    unsigned count = 0;
    uint64_t id = 0;
    doe_futures_zig_queue_layout(q);
    doe_futures_zig_completion_layout(n);
    assert(q[0] == sizeof(queue) && q[1] == alignof(decltype(queue)));
    assert(n[0] == sizeof(completion) && n[1] == alignof(decltype(completion)));
    assert(q[5] == offsetof(doe_future_queue, head));
    assert(q[6] == offsetof(doe_future_queue, active));
    assert(n[4] == offsetof(doe_future_completion, dispatch));
    assert(n[5] == offsetof(doe_future_completion, userdata));
    assert(doe_futures_zig_queue_init(&queue) == DOE_FUTURE_OK);
    assert(doe_futures_zig_completion_init(&completion, DOE_FUTURE_MODE_COMPAT_IMMEDIATE, dispatch, &count) == DOE_FUTURE_OK);
    assert(doe_future_new_id(&queue, &id) == DOE_FUTURE_OK);
    assert(doe_future_register(&queue, &completion, id) == DOE_FUTURE_OK);
    assert(doe_future_mark_ready(&completion) == DOE_FUTURE_OK);
    assert(count == 1 && !completion.initialized);
    assert(doe_future_queue_deinit(&queue) == DOE_FUTURE_OK);
    std::puts("PASS strict C++ real private storage layout and callback cleanup");
}
