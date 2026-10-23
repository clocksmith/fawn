/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
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
    operation = "IDXGIFactory4.EnumAdapters1";
    result = IDXGIFactory4_EnumAdapters1(factory, options->adapter_ordinal, &adapter);
    if (FAILED(result)) goto fail;
    DXGI_ADAPTER_DESC1 description;
    operation = "IDXGIAdapter1.GetDesc1";
    result = IDXGIAdapter1_GetDesc1(adapter, &description);
    if (FAILED(result)) goto fail;
    bool software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    if (software && !options->allow_software_adapter) {
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory4_Release(factory);
        doe_dx_native_close(context);
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "adapter.software", error);
    }
    operation = "D3D12CreateDevice";
    result = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
        &IID_ID3D12Device, (void **)&context->device);
    if (FAILED(result)) goto fail;
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
    doe_dx_caps found = {0};
    found.adapter_ordinal = options->adapter_ordinal;
    found.vendor_id = description.VendorId;
    found.device_id = description.DeviceId;
    found.software_adapter = software;
    found.luid_low = description.AdapterLuid.LowPart;
    found.luid_high = description.AdapterLuid.HighPart;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, description.Description,
        -1, found.name, (int)sizeof(found.name), NULL, NULL)) {
        operation = "WideCharToMultiByte";
        result = HRESULT_FROM_WIN32(GetLastError());
        goto fail;
    }
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
