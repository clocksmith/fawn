// SPDX-License-Identifier: MIT
/* Explicit WARP selection and optional architecture query; borrowed/owned COM handles. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "d3d12_bridge.h"

/* Return one owned device reference, or NULL. Never select hardware for fallback. */
D3D12Handle d3d12_bridge_create_warp_device(void)
{
    IDXGIFactory4 *factory = NULL;
    HRESULT status = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
    if (FAILED(status)) return NULL;
    IDXGIAdapter *adapter = NULL;
    status = IDXGIFactory4_EnumWarpAdapter(factory, &IID_IDXGIAdapter, (void **)&adapter);
    IDXGIFactory4_Release(factory);
    if (FAILED(status)) return NULL;
    ID3D12Device *device = NULL;
    status = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0,
                              &IID_ID3D12Device, (void **)&device);
    IDXGIAdapter_Release(adapter);
    return FAILED(status) ? NULL : (D3D12Handle)device;
}

int d3d12_bridge_device_uma(D3D12Handle device, uint32_t *out)
{
    if (device == NULL || out == NULL) return -1;
    D3D12_FEATURE_DATA_ARCHITECTURE architecture = {0};
    HRESULT status = ID3D12Device_CheckFeatureSupport((ID3D12Device *)device,
        D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));
    if (FAILED(status)) return -1;
    *out = architecture.UMA != 0;
    return 0;
}
