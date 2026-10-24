/* SPDX-License-Identifier: MIT */
#include "doe_futures_zig_bridge.h"
#include <string.h>
void doe_futures_zig_queue_layout(size_t out[DOE_FUTURES_QUEUE_LAYOUT_COUNT])
{
    const size_t layout[] = {sizeof(doe_future_queue), _Alignof(doe_future_queue),
        offsetof(doe_future_queue, mutex), offsetof(doe_future_queue, changed),
        offsetof(doe_future_queue, head),
        offsetof(doe_future_queue, active), offsetof(doe_future_queue, instance_id),
        offsetof(doe_future_queue, next_sequence), offsetof(doe_future_queue, state)};
    memcpy(out, layout, sizeof(layout));
}
void doe_futures_zig_completion_layout(size_t out[DOE_FUTURES_COMPLETION_LAYOUT_COUNT])
{
    const size_t layout[] = {sizeof(doe_future_completion), _Alignof(doe_future_completion),
        offsetof(doe_future_completion, owner), offsetof(doe_future_completion, next),
        offsetof(doe_future_completion, dispatch), offsetof(doe_future_completion, userdata),
        offsetof(doe_future_completion, id), offsetof(doe_future_completion, delivery_thread),
        offsetof(doe_future_completion, mode), offsetof(doe_future_completion, ready),
        offsetof(doe_future_completion, delivering), offsetof(doe_future_completion, initialized)};
    memcpy(out, layout, sizeof(layout));
}
doe_future_status doe_futures_zig_queue_init(doe_future_queue *queue)
{
    doe_future_queue *out = NULL;
    return doe_future_queue_init(DOE_FUTURES_ABI_VERSION, queue, sizeof(*queue), &out);
}
doe_future_status doe_futures_zig_completion_init(doe_future_completion *completion,
    doe_future_mode mode, doe_future_dispatch dispatch, void *userdata)
{
    doe_future_completion *out = NULL;
    return doe_future_completion_init(DOE_FUTURES_ABI_VERSION, completion,
        sizeof(*completion), mode, dispatch, userdata, &out);
}
