/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#include "windows_cache.h"
#ifdef _WIN32

doe_dx_status doe_dx_hresult(HRESULT result, const char *operation, doe_dx_error *error) {
    doe_dx_status status = DOE_DX_NATIVE_FAILURE;
    if (SUCCEEDED(result)) status = DOE_DX_OK;
    else if (result == E_OUTOFMEMORY) status = DOE_DX_OUT_OF_MEMORY;
    else if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
        result == DXGI_ERROR_DEVICE_HUNG) status = DOE_DX_DEVICE_LOST;
    return doe_dx_fail(status, (int32_t)result, operation, error);
}

void doe_dx_native_close(doe_dx_native_context *context) {
    if (!context) return;
    doe_dx_cache_close(context);
    if (context->draw_signature) ID3D12CommandSignature_Release(context->draw_signature);
    if (context->draw_indexed_signature) ID3D12CommandSignature_Release(context->draw_indexed_signature);
    if (context->dispatch_signature) ID3D12CommandSignature_Release(context->dispatch_signature);
    if (context->fence) ID3D12Fence_Release(context->fence);
    if (context->queue) ID3D12CommandQueue_Release(context->queue);
    if (context->device) ID3D12Device_Release(context->device);
    free(context);
}

doe_dx_status doe_dx_native_open(const doe_dx_options *options,
    doe_dx_native_context **out, doe_dx_caps *caps, doe_dx_error *error) {
    doe_dx_native_context *context = calloc(1, sizeof(*context));
    if (!context) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.context.allocate", error);
    IDXGIFactory4 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    const char *operation = "CreateDXGIFactory1";
    HRESULT result = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
    if (FAILED(result)) goto fail;
    doe_dx_caps found = {0};
    doe_dx_status selected = doe_dx_open_adapter(factory, options, &adapter, &found, error);
    if (selected != DOE_DX_OK) {
        IDXGIFactory4_Release(factory);
        doe_dx_native_close(context);
        return selected;
    }
    operation = "D3D12CreateDevice";
    result = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
        &IID_ID3D12Device, (void **)&context->device);
    if (FAILED(result)) goto fail;
    LUID created_identity;
    context->device->lpVtbl->GetAdapterLuid(context->device, &created_identity);
    if (created_identity.LowPart != found.luid_low || created_identity.HighPart != found.luid_high) {
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory4_Release(factory);
        doe_dx_native_close(context);
        return doe_dx_fail(DOE_DX_DEVICE_MISMATCH, 0, "ID3D12Device.GetAdapterLuid", error);
    }
    D3D12_COMMAND_QUEUE_DESC queue = {0};
    queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    operation = "ID3D12Device.CreateCommandQueue";
    result = ID3D12Device_CreateCommandQueue(context->device, &queue,
        &IID_ID3D12CommandQueue, (void **)&context->queue);
    if (FAILED(result)) goto fail;
    operation = "ID3D12Device.CreateFence";
    result = ID3D12Device_CreateFence(context->device, 0, D3D12_FENCE_FLAG_NONE,
        &IID_ID3D12Fence, (void **)&context->fence);
    if (FAILED(result)) goto fail;
    doe_dx_status caps_status = doe_dx_query_caps(context, &found, error);
    if (caps_status==DOE_DX_OK)
        caps_status=doe_dx_native_timestamp_frequency(context,&found.timestamp_frequency,error);
    if (caps_status != DOE_DX_OK) {
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory4_Release(factory);
        doe_dx_native_close(context);
        return caps_status;
    }
    doe_dx_cache_adapter(context,adapter);
    IDXGIAdapter1_Release(adapter);
    IDXGIFactory4_Release(factory);
    *caps = found;
    *out = context;
    return doe_dx_fail(DOE_DX_OK, 0, "native.context.open", error);
fail:
    if (adapter) IDXGIAdapter1_Release(adapter);
    if (factory) IDXGIFactory4_Release(factory);
    doe_dx_native_close(context);
    return doe_dx_hresult(result, operation, error);
}

doe_dx_status doe_dx_native_completed(doe_dx_native_context *context, uint64_t *out,
    doe_dx_error *error) {
    HRESULT removed = ID3D12Device_GetDeviceRemovedReason(context->device);
    if (FAILED(removed))
        return doe_dx_fail(DOE_DX_DEVICE_LOST, (int32_t)removed, "GetDeviceRemovedReason", error);
    uint64_t completed = ID3D12Fence_GetCompletedValue(context->fence);
    if (completed == UINT64_MAX)
        return doe_dx_fail(DOE_DX_DEVICE_LOST, (int32_t)DXGI_ERROR_DEVICE_REMOVED,
            "ID3D12Fence.GetCompletedValue", error);
    *out = completed;
    return doe_dx_fail(DOE_DX_OK, 0, "ID3D12Fence.GetCompletedValue", error);
}

doe_dx_status doe_dx_native_signal(doe_dx_native_context *context, uint64_t value,
    doe_dx_error *error) {
    HRESULT result = ID3D12CommandQueue_Signal(context->queue, context->fence, value);
    if (FAILED(result)) {
        HRESULT removed = ID3D12Device_GetDeviceRemovedReason(context->device);
        if (FAILED(removed))
            return doe_dx_fail(DOE_DX_DEVICE_LOST, (int32_t)removed, "GetDeviceRemovedReason", error);
    }
    return doe_dx_hresult(result, "ID3D12CommandQueue.Signal", error);
}

doe_dx_status doe_dx_native_wait(doe_dx_native_context *context, uint64_t value,
    uint32_t wait_ms, doe_dx_error *error) {
    /* Bounded native fence polling avoids an outstanding event-registration
     * lifetime after a timeout. No timeout or wait failure permits retirement.
     * This is not a spin loop, and does not claim performance qualification. */
    ULONGLONG start = GetTickCount64();
    for (;;) {
        uint64_t completed = 0;
        doe_dx_status status = doe_dx_native_completed(context, &completed, error);
        if (status != DOE_DX_OK) return status;
        if (completed >= value) return doe_dx_fail(DOE_DX_OK, 0, "fence.wait", error);
        if (GetTickCount64() - start >= wait_ms)
            return doe_dx_fail(DOE_DX_PENDING, 0, "fence.wait", error);
        Sleep(1);
    }
}
#endif
