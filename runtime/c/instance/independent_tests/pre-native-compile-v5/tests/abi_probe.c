/* SPDX-License-Identifier: MIT */
#include "doe_instance.h"
#include <stdio.h>
#ifdef __cplusplus
#define ALIGN(T) alignof(T)
#else
#define ALIGN(T) _Alignof(T)
#endif
#define TYPE(T) printf("type %s %zu %zu\n", #T, sizeof(T), (size_t)ALIGN(T))
#define FIELD(T,F) printf("field %s.%s %zu %zu\n", #T, #F, offsetof(T,F), sizeof(((T *)0)->F))
#define VALUE(V) printf("enum %s %u\n", #V, (unsigned)(V))
int main(void) {
    TYPE(doe_instance_status); TYPE(doe_instance_allocator); TYPE(doe_instance_config); TYPE(doe_instance_queue_lease);
    TYPE(doe_instance_allocate); TYPE(doe_instance_deallocate); TYPE(doe_instance_allocated); TYPE(doe_instance_external_count); TYPE(doe_instance_remove_label); TYPE(doe_instance_lease_hook);
    TYPE(doe_future_storage_requirements); TYPE(doe_future_wait_info); TYPE(WGPUFuture); TYPE(WGPUFutureWaitInfo);
    TYPE(WGPUSupportedInstanceFeatures); TYPE(WGPUInstanceLimits); TYPE(WGPUBool); TYPE(WGPUWaitStatus); TYPE(WGPUInstanceFeatureName);
    FIELD(doe_instance_allocator,state); FIELD(doe_instance_allocator,vtable); FIELD(doe_instance_allocator,allocate); FIELD(doe_instance_allocator,deallocate);
    FIELD(doe_instance_config,abi_version); FIELD(doe_instance_config,object_allocator); FIELD(doe_instance_config,wait_allocator); FIELD(doe_instance_config,hook_userdata); FIELD(doe_instance_config,allocated); FIELD(doe_instance_config,external_texture_count); FIELD(doe_instance_config,remove_label);
    FIELD(doe_instance_queue_lease,queue); FIELD(doe_instance_queue_lease,userdata); FIELD(doe_instance_queue_lease,retain); FIELD(doe_instance_queue_lease,release);
    FIELD(doe_future_storage_requirements,size); FIELD(doe_future_storage_requirements,alignment); FIELD(doe_future_wait_info,id); FIELD(doe_future_wait_info,completed);
    FIELD(WGPUFuture,id); FIELD(WGPUFutureWaitInfo,future); FIELD(WGPUFutureWaitInfo,completed);
    FIELD(WGPUSupportedInstanceFeatures,featureCount); FIELD(WGPUSupportedInstanceFeatures,features); FIELD(WGPUInstanceLimits,nextInChain); FIELD(WGPUInstanceLimits,timedWaitAnyMaxCount);
    VALUE(DOE_INSTANCE_ABI_VERSION); VALUE(DOE_INSTANCE_OK); VALUE(DOE_INSTANCE_INVALID_ARGUMENT); VALUE(DOE_INSTANCE_UNSUPPORTED_VERSION); VALUE(DOE_INSTANCE_OUT_OF_MEMORY); VALUE(DOE_INSTANCE_LAYOUT_OVERFLOW); VALUE(DOE_INSTANCE_QUEUE_INITIALIZATION);
    VALUE(WGPUWaitStatus_Success); VALUE(WGPUWaitStatus_TimedOut); VALUE(WGPUWaitStatus_Error);
    return 0;
}
