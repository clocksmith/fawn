/* SPDX-License-Identifier: MIT */
#include "doe_instance.h"
#include <assert.h>
#include <stdio.h>

#define RECORD(type) printf(#type " size=%zu align=%zu\n", sizeof(type), _Alignof(type))
#define FIELD(type, member) printf(#type "." #member " offset=%zu size=%zu\n", offsetof(type, member), sizeof(((type *)0)->member))
int main(void)
{
    RECORD(doe_instance_allocator);
    FIELD(doe_instance_allocator, state); FIELD(doe_instance_allocator, vtable);
    FIELD(doe_instance_allocator, allocate); FIELD(doe_instance_allocator, deallocate);
    RECORD(doe_instance_config);
    FIELD(doe_instance_config, abi_version); FIELD(doe_instance_config, object_allocator);
    FIELD(doe_instance_config, wait_allocator); FIELD(doe_instance_config, hook_userdata);
    FIELD(doe_instance_config, allocated); FIELD(doe_instance_config, external_texture_count);
    FIELD(doe_instance_config, remove_label);
    RECORD(doe_instance_queue_lease);
    FIELD(doe_instance_queue_lease, queue); FIELD(doe_instance_queue_lease, userdata);
    FIELD(doe_instance_queue_lease, retain); FIELD(doe_instance_queue_lease, release);
    RECORD(WGPUFuture); FIELD(WGPUFuture, id);
    RECORD(WGPUFutureWaitInfo); FIELD(WGPUFutureWaitInfo, future); FIELD(WGPUFutureWaitInfo, completed);
    RECORD(WGPUSupportedInstanceFeatures);
    FIELD(WGPUSupportedInstanceFeatures, featureCount); FIELD(WGPUSupportedInstanceFeatures, features);
    RECORD(WGPUInstanceLimits); FIELD(WGPUInstanceLimits, nextInChain); FIELD(WGPUInstanceLimits, timedWaitAnyMaxCount);
    RECORD(WGPUBool); RECORD(WGPUWaitStatus); RECORD(WGPUInstanceFeatureName); RECORD(doe_instance_status);
    RECORD(doe_instance_allocate); RECORD(doe_instance_deallocate); RECORD(doe_instance_allocated);
    RECORD(doe_instance_external_count); RECORD(doe_instance_remove_label); RECORD(doe_instance_lease_hook);
    assert(DOE_INSTANCE_OK == 0 && DOE_INSTANCE_INVALID_ARGUMENT == 1 && DOE_INSTANCE_UNSUPPORTED_VERSION == 2);
    assert(DOE_INSTANCE_OUT_OF_MEMORY == 3 && DOE_INSTANCE_LAYOUT_OVERFLOW == 4 && DOE_INSTANCE_QUEUE_INITIALIZATION == 5);
    assert(WGPUWaitStatus_Success == 1 && WGPUWaitStatus_TimedOut == 2 && WGPUWaitStatus_Error == 3);
    assert(doe_instance_features().featureCount == 2 && doe_instance_features().features != NULL);
    assert(doe_instance_limits().nextInChain == NULL && doe_instance_limits().timedWaitAnyMaxCount == 64);
    assert(doe_instance_wait_any(NULL, 0, NULL, UINT64_MAX) == WGPUWaitStatus_Success);
    puts("instance ABI C calls: passed");
    return 0;
}
