/* SPDX-License-Identifier: MIT
 * Private, build-bound native storage bridge. Not a stable public ABI. */
#ifndef DOE_FUTURES_ZIG_BRIDGE_H
#define DOE_FUTURES_ZIG_BRIDGE_H
#include "../src/internal.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Size, alignment, then every field offset, in declaration order. */
#define DOE_FUTURES_QUEUE_LAYOUT_COUNT 9
#define DOE_FUTURES_COMPLETION_LAYOUT_COUNT 12
void doe_futures_zig_queue_layout(size_t out[DOE_FUTURES_QUEUE_LAYOUT_COUNT]);
void doe_futures_zig_completion_layout(size_t out[DOE_FUTURES_COMPLETION_LAYOUT_COUNT]);
doe_future_status doe_futures_zig_queue_init(doe_future_queue *queue);
doe_future_status doe_futures_zig_completion_init(doe_future_completion *completion,
    doe_future_mode mode, doe_future_dispatch dispatch, void *userdata);
#ifdef __cplusplus
}
#endif
#endif
