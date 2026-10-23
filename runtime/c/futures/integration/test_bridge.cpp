/* SPDX-License-Identifier: MIT */
#include "doe_futures_zig_bridge.h"
#include <cassert>
#include <cstddef>
int main()
{
    doe_future_queue queue;
    doe_future_completion completion;
    size_t q[DOE_FUTURES_QUEUE_LAYOUT_COUNT];
    size_t n[DOE_FUTURES_COMPLETION_LAYOUT_COUNT];
    doe_futures_zig_queue_layout(q);
    doe_futures_zig_completion_layout(n);
    assert(q[0] == sizeof(queue) && q[1] == alignof(decltype(queue)));
    assert(n[0] == sizeof(completion) && n[1] == alignof(decltype(completion)));
    assert(q[5] == offsetof(doe_future_queue, head));
    assert(n[5] == offsetof(doe_future_completion, userdata));
    assert(doe_futures_zig_queue_init(&queue) == DOE_FUTURE_OK);
    assert(doe_futures_zig_completion_init(&completion,
        DOE_FUTURE_MODE_WAIT_ANY_ONLY, nullptr, nullptr) == DOE_FUTURE_OK);
    assert(doe_future_completion_deinit(&completion) == DOE_FUTURE_OK);
    assert(doe_future_queue_deinit(&queue) == DOE_FUTURE_OK);
}
