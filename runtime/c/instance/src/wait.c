/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"

WGPUWaitStatus doe_instance_wait_any(doe_instance *instance, size_t count,
                                    WGPUFutureWaitInfo *infos, uint64_t timeout_ns)
{
    doe_instance_allocator allocator;
    doe_future_wait_info *copied;
    doe_future_status status;
    size_t size;
    if (count == 0) return WGPUWaitStatus_Success;
    if (instance == NULL) return WGPUWaitStatus_Error;
    if (count > SIZE_MAX / sizeof(*copied) || count > SIZE_MAX / sizeof(*infos))
        return WGPUWaitStatus_Error;
    doe_instance_retain(instance);
    allocator = instance->config.wait_allocator;
    size = count * sizeof(*copied);
    copied = allocator.allocate(allocator.state, allocator.vtable,
        size, _Alignof(doe_future_wait_info));
    if (copied == NULL) {
        doe_instance_release(instance);
        return WGPUWaitStatus_Error;
    }
    for (size_t i = 0; i < count; ++i) {
        copied[i].id = infos[i].future.id;
        copied[i].completed = infos[i].completed;
    }
    status = doe_future_wait_any(instance->queue, copied, count, timeout_ns);
    for (size_t i = 0; i < count; ++i) infos[i].completed = copied[i].completed;
    allocator.deallocate(allocator.state, allocator.vtable,
        copied, size, _Alignof(doe_future_wait_info));
    doe_instance_release(instance);
    if (status == DOE_FUTURE_OK) return WGPUWaitStatus_Success;
    if (status == DOE_FUTURE_TIMED_OUT) return WGPUWaitStatus_TimedOut;
    return WGPUWaitStatus_Error;
}

void doe_instance_process_events(doe_instance *instance)
{
    doe_future_status status;
    if (instance == NULL) return;
    doe_instance_retain(instance);
    status = doe_future_process_events(instance->queue);
    if (status != DOE_FUTURE_OK) di_fatal("process events", status);
    doe_instance_release(instance);
}
