/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

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
        LUID identity;
        identity.LowPart = options->adapter_identity.luid_low;
        identity.HighPart = options->adapter_identity.luid_high;
        operation = "IDXGIFactory4.EnumAdapterByLuid";
        result = IDXGIFactory4_EnumAdapterByLuid(factory, identity, &IID_IDXGIAdapter1, (void **)&adapter);
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
