/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "windows_internal.h"
#ifdef _WIN32
struct doe_dx_shared_wait {
    ID3D12Fence *fence;
    ID3D12Device *device;
    uint64_t value;
    struct doe_dx_shared_wait *next;
};
doe_dx_status doe_dx_commands_wait_shared_fence(doe_dx_commands *commands,doe_dx_shared_fence *source,
    uint64_t value,doe_dx_error *error)
{
    doe_dx_status status=doe_dx_recording(commands,error);
    if (status!=DOE_DX_OK) return status;
    if (!source || value==UINT64_MAX) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"shared.queue_wait.arguments",error);
    struct doe_dx_shared_wait *wait=calloc(1,sizeof(*wait));
    if (!wait) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"shared.queue_wait.allocate",error);
    wait->fence=doe_dx_shared_fence_native(source);
    HRESULT hr=ID3D12Fence_GetDevice(wait->fence,&IID_ID3D12Device,(void **)&wait->device);
    if (FAILED(hr)) { free(wait); return doe_dx_hresult(hr,"shared.queue_wait.device",error); }
    ID3D12Fence_AddRef(wait->fence); wait->value=value;
    struct doe_dx_shared_wait **tail=&commands->native->shared_waits;
    while (*tail) tail=&(*tail)->next;
    *tail=wait; return DOE_DX_OK;
}
doe_dx_status doe_dx_shared_waits_execute(doe_dx_native_context *context,doe_dx_native_commands *commands,
    doe_dx_error *error)
{
    for (struct doe_dx_shared_wait *wait=commands->shared_waits;wait;wait=wait->next) {
        HRESULT hr=ID3D12CommandQueue_Wait(context->queue,wait->fence,wait->value);
        if (FAILED(hr)) return doe_dx_hresult(hr,"ID3D12CommandQueue.Wait.shared_fence",error);
    }
    return DOE_DX_OK;
}
void doe_dx_shared_waits_close(doe_dx_native_commands *commands)
{
    while (commands->shared_waits) {
        struct doe_dx_shared_wait *wait=commands->shared_waits;
        commands->shared_waits=wait->next;
        ID3D12Fence_Release(wait->fence); ID3D12Device_Release(wait->device); free(wait);
    }
}
#endif
