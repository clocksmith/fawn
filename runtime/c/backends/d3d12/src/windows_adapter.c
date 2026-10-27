/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

static HRESULT find_luid(IDXGIFactory4 *factory, uint32_t low, int32_t high,
    IDXGIAdapter1 **out) {
    const LUID identity = {.LowPart = low, .HighPart = high};
    return IDXGIFactory4_EnumAdapterByLuid(factory, identity, &IID_IDXGIAdapter1, (void **)out);
}

/* Discovery never creates a D3D12 device. Its copied identity is revalidated
 * against both DXGI and the created device by the existing context owner. */
doe_dx_status doe_dx_native_query_luid(uint32_t low, int32_t high,
    doe_dx_adapter_identity *out, doe_dx_error *error) {
    if (!out) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "adapter.query_luid_output", error);
    IDXGIFactory4 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    const char *operation = "CreateDXGIFactory1";
    HRESULT result = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
    if (FAILED(result)) {
        if (factory) IDXGIFactory4_Release(factory);
        return doe_dx_hresult(result, operation, error);
    }
    operation = "IDXGIFactory4.EnumAdapterByLuid";
    result = find_luid(factory, low, high, &adapter);
    if (SUCCEEDED(result)) {
        DXGI_ADAPTER_DESC1 description;
        operation = "IDXGIAdapter1.GetDesc1";
        result = IDXGIAdapter1_GetDesc1(adapter, &description);
        if (SUCCEEDED(result)) {
            doe_dx_adapter_identity identity = {description.AdapterLuid.LowPart,
                description.AdapterLuid.HighPart, (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0};
            IDXGIAdapter1_Release(adapter);
            IDXGIFactory4_Release(factory);
            if (identity.luid_low != low || identity.luid_high != high)
                return doe_dx_fail(DOE_DX_DEVICE_MISMATCH, 0, "adapter.query_luid_identity", error);
            *out = identity;
            return doe_dx_fail(DOE_DX_OK, 0, "adapter.query_luid", error);
        }
    }
    if (adapter) IDXGIAdapter1_Release(adapter);
    IDXGIFactory4_Release(factory);
    if (result == DXGI_ERROR_NOT_FOUND)
        return doe_dx_fail(DOE_DX_UNAVAILABLE, (int32_t)result, operation, error);
    return doe_dx_hresult(result, operation, error);
}

doe_dx_status doe_dx_open_adapter(IDXGIFactory4 *factory, const doe_dx_options *options,
    IDXGIAdapter1 **out, doe_dx_caps *caps, doe_dx_error *error) {
    IDXGIAdapter1 *adapter = NULL;
    uint32_t ordinal = options->adapter_ordinal;
    const char *operation = "adapter.selection";
    HRESULT result = E_INVALIDARG;
    switch (options->adapter_selection) {
    case DOE_DX_SELECT_ORDINAL:
    case DOE_DX_SELECT_HARDWARE:
        operation = "IDXGIFactory4.EnumAdapters1";
        break;
    case DOE_DX_SELECT_LUID: {
        operation = "IDXGIFactory4.EnumAdapterByLuid";
        result = find_luid(factory, options->adapter_identity.luid_low,
            options->adapter_identity.luid_high, &adapter);
        ordinal = DOE_DX_NO_ADAPTER_ORDINAL;
        break;
    }
    case DOE_DX_SELECT_WARP:
        operation = "IDXGIFactory4.EnumWarpAdapter";
        result = IDXGIFactory4_EnumWarpAdapter(factory, &IID_IDXGIAdapter1, (void **)&adapter);
        ordinal = DOE_DX_NO_ADAPTER_ORDINAL;
        break;
    }
    for (;;) {
        if (options->adapter_selection == DOE_DX_SELECT_ORDINAL || options->adapter_selection == DOE_DX_SELECT_HARDWARE) {
            operation = "IDXGIFactory4.EnumAdapters1";
            result = IDXGIFactory4_EnumAdapters1(factory, ordinal, &adapter);
        }
        if (FAILED(result)) {
            if (result == DXGI_ERROR_NOT_FOUND && options->adapter_selection == DOE_DX_SELECT_HARDWARE)
                return doe_dx_fail(DOE_DX_UNAVAILABLE, (int32_t)result, "adapter.no_hardware", error);
            if (result == DXGI_ERROR_NOT_FOUND && options->adapter_selection == DOE_DX_SELECT_LUID)
                return doe_dx_fail(DOE_DX_DEVICE_MISMATCH, (int32_t)result, operation, error);
            return doe_dx_hresult(result, operation, error);
        }
        DXGI_ADAPTER_DESC1 description;
        result = IDXGIAdapter1_GetDesc1(adapter, &description);
        if (FAILED(result)) {
            IDXGIAdapter1_Release(adapter);
            return doe_dx_hresult(result, "IDXGIAdapter1.GetDesc1", error);
        }
        bool software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        if (options->adapter_selection == DOE_DX_SELECT_HARDWARE && software) {
            IDXGIAdapter1_Release(adapter);
            adapter = NULL;
            if (ordinal == UINT32_MAX)
                return doe_dx_fail(DOE_DX_RANGE, 0, "adapter.enumeration_overflow", error);
            ++ordinal;
            continue;
        }
        doe_dx_caps found = {0};
        found.adapter_ordinal = ordinal;
        found.adapter_selection = options->adapter_selection;
        found.vendor_id = description.VendorId;
        found.device_id = description.DeviceId;
        found.software_adapter = software;
        found.luid_low = description.AdapterLuid.LowPart;
        found.luid_high = description.AdapterLuid.HighPart;
        doe_dx_status status = doe_dx_validate_adapter(options, &found, error);
        if (status == DOE_DX_OK && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            description.Description, -1, found.name, (int)sizeof(found.name), NULL, NULL))
            status = doe_dx_hresult(HRESULT_FROM_WIN32(GetLastError()), "WideCharToMultiByte", error);
        if (status != DOE_DX_OK) {
            IDXGIAdapter1_Release(adapter);
            return status;
        }
        *caps = found;
        *out = adapter;
        return doe_dx_fail(DOE_DX_OK, 0, "adapter.open", error);
    }
}
#endif
