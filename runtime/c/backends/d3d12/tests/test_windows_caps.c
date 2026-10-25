/* SPDX-License-Identifier: MIT
 * Actual SDK CPU call controls only. Official vtable types, no device creation,
 * reconstructed ABI, shader execution or driver-capability evidence. */
#include "../src/windows_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct sdk_device {
    ID3D12Device iface;
    D3D_FEATURE_LEVEL level;
    D3D12_RESOURCE_BINDING_TIER binding_tier;
    UINT resource_bits, process_bits;
    D3D12_FEATURE failed_query;
    HRESULT shader_result;
    DXGI_FORMAT missing_format;
    bool no_depth_msaa;
    UINT allocation_fault, allocation_calls;
} sdk_device;

static HRESULT STDMETHODCALLTYPE check_feature(ID3D12Device *raw, D3D12_FEATURE feature,
    void *data, UINT size) {
    sdk_device *device = (sdk_device *)raw;
    if (feature == device->failed_query) return E_FAIL;
    switch (feature) {
    case D3D12_FEATURE_FEATURE_LEVELS: {
        assert(size == sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS));
        D3D12_FEATURE_DATA_FEATURE_LEVELS *levels = data;
        assert(levels->NumFeatureLevels == 4);
        levels->MaxSupportedFeatureLevel = device->level;
        break;
    }
    case D3D12_FEATURE_D3D12_OPTIONS: {
        assert(size == sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS));
        D3D12_FEATURE_DATA_D3D12_OPTIONS *options = data;
        options->ResourceBindingTier = device->binding_tier;
        options->ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_1;
        break;
    }
    case D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT: {
        assert(size == sizeof(D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT));
        D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT *address = data;
        address->MaxGPUVirtualAddressBitsPerResource = device->resource_bits;
        address->MaxGPUVirtualAddressBitsPerProcess = device->process_bits;
        break;
    }
    case D3D12_FEATURE_ARCHITECTURE: {
        assert(size == sizeof(D3D12_FEATURE_DATA_ARCHITECTURE));
        D3D12_FEATURE_DATA_ARCHITECTURE *architecture = data;
        assert(architecture->NodeIndex == 0);
        architecture->UMA = TRUE;
        break;
    }
    case D3D12_FEATURE_SHADER_MODEL: {
        assert(size == sizeof(D3D12_FEATURE_DATA_SHADER_MODEL));
        if (FAILED(device->shader_result)) return device->shader_result;
        D3D12_FEATURE_DATA_SHADER_MODEL *shader = data;
        assert(shader->HighestShaderModel == D3D_SHADER_MODEL_6_0 || shader->HighestShaderModel == D3D_SHADER_MODEL_6_2);
        shader->HighestShaderModel = D3D_SHADER_MODEL_6_0;
        break;
    }
    case D3D12_FEATURE_FORMAT_SUPPORT: {
        assert(size==sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT));
        D3D12_FEATURE_DATA_FORMAT_SUPPORT *format=data;
        format->Support1=format->Format==device->missing_format ? D3D12_FORMAT_SUPPORT1_NONE :
            (D3D12_FORMAT_SUPPORT1)(D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE|
            D3D12_FORMAT_SUPPORT1_SHADER_LOAD|D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON|
            D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL|D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET);
        break;
    }
    case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS: {
        assert(size==sizeof(D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS));
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS *samples=data;
        assert(samples->Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT && samples->SampleCount==4);
        samples->NumQualityLevels=device->no_depth_msaa ? 0u : 1u;
        break;
    }
    case D3D12_FEATURE_D3D12_OPTIONS1:
    case D3D12_FEATURE_D3D12_OPTIONS4: return E_INVALIDARG;
    default: assert(false); return E_INVALIDARG;
    }
    return S_OK;
}

static D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE allocation_info(ID3D12Device *raw,
    D3D12_RESOURCE_ALLOCATION_INFO *out, UINT visible, UINT count, const D3D12_RESOURCE_DESC *descriptions) {
    sdk_device *device = (sdk_device *)raw;
    ++device->allocation_calls;
    assert(visible == 0 && count == 1);
    assert(descriptions->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);
    assert(descriptions->Flags == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    assert(descriptions->Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR);
    *out = (D3D12_RESOURCE_ALLOCATION_INFO){descriptions->Width,D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT};
    switch (device->allocation_fault) {
    case 0: break;
    case 1: out->SizeInBytes = UINT64_MAX; break;
    case 2: out->SizeInBytes = descriptions->Width - 1; break;
    case 3: out->Alignment = 0; break;
    case 4: out->Alignment = 3; break;
    case 5: out->SizeInBytes += 1; break;
    case 6: out->SizeInBytes = UINT64_C(1) << 41; break;
    default: assert(false);
    }
    return out;
}
static CONST_VTBL ID3D12DeviceVtbl device_vtable = {
    .CheckFeatureSupport=check_feature, .GetResourceAllocationInfo=allocation_info
};
static sdk_device device_defaults(void) {
    sdk_device device = {0};
    device.iface.lpVtbl = &device_vtable;
    device.level = D3D_FEATURE_LEVEL_11_0;
    device.binding_tier = D3D12_RESOURCE_BINDING_TIER_1;
    device.resource_bits = device.process_bits = 40;
    device.failed_query = (D3D12_FEATURE)-1;
    return device;
}

static void caps_controls(void) {
    for (UINT tier = 1; tier <= 3; ++tier) {
        for (UINT newer = 0; newer < 2; ++newer) {
            sdk_device device = device_defaults();
            device.binding_tier = (D3D12_RESOURCE_BINDING_TIER)tier;
            device.level = newer ? D3D_FEATURE_LEVEL_11_1 : D3D_FEATURE_LEVEL_11_0;
            doe_dx_native_context context = {0};
            context.device = &device.iface;
            doe_dx_caps caps = {0};
            assert(doe_dx_query_caps(&context,&caps,NULL) == DOE_DX_OK);
            assert(caps.unified_memory && caps.feature_level == (uint32_t)device.level);
            assert(caps.resource_binding_tier == tier && caps.resource_heap_tier == 1);
            assert(caps.gpu_va_bits_per_resource == 40 && caps.gpu_va_bits_per_process == 40);
            assert(caps.shader_model == (uint32_t)D3D_SHADER_MODEL_6_0);
            assert(caps.texture_compression_bc && caps.depth32float_stencil8);
            assert(caps.limits.max_descriptors == 64 && caps.limits.max_static_samplers == 16);
            assert(caps.limits.max_cbvs == (tier == 3 ? 64u : 14u));
            assert(caps.limits.max_uavs == (tier == 1 && !newer ? 8u : 64u));
            assert(caps.limits.max_srvs == 64 && caps.limits.max_compute_threads == 1024);
            assert(caps.limits.max_compute_shared_bytes == 32768);
            assert(caps.limits.max_compute_size[0] == 1024 && caps.limits.max_compute_size[2] == 64);
            assert(caps.limits.max_dispatch_groups[1] == 65535);
            assert(caps.limits.uniform_offset_alignment == 256 && caps.limits.max_uniform_bytes == 65536);
            assert(caps.limits.max_buffer_size == UINT64_C(268435456));
            assert(caps.limits.max_buffer_allocation_bytes == caps.limits.max_buffer_size);
            assert(context.limits.max_buffer_size == caps.limits.max_buffer_size && device.allocation_calls == 1);
        }
    }
    sdk_device device = device_defaults();
    doe_dx_native_context context = {0};
    context.device = &device.iface;
    doe_dx_caps caps = {0};
    device.shader_result = E_INVALIDARG;
    device.process_bits = 27;
    assert(doe_dx_query_caps(&context,&caps,NULL) == DOE_DX_OK);
    assert(caps.shader_model == 0 && caps.limits.max_buffer_size == UINT64_C(134217728));
    const D3D12_FEATURE failures[] = {D3D12_FEATURE_FEATURE_LEVELS,D3D12_FEATURE_D3D12_OPTIONS,
        D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT,D3D12_FEATURE_ARCHITECTURE,D3D12_FEATURE_SHADER_MODEL,
        D3D12_FEATURE_FORMAT_SUPPORT,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS};
    for (size_t index = 0; index < sizeof(failures)/sizeof(failures[0]); ++index) {
        device = device_defaults();
        device.failed_query = failures[index];
        caps.vendor_id = 77;
        context.limits.max_buffer_size = 123;
        doe_dx_error error = {0};
        assert(doe_dx_query_caps(&context,&caps,&error) == DOE_DX_NATIVE_FAILURE);
        assert(caps.vendor_id == 77 && error.native_code == (int32_t)E_FAIL && error.operation);
        assert(!device.allocation_calls && context.limits.max_buffer_size == 123);
    }
    device=device_defaults(); device.missing_format=DXGI_FORMAT_BC6H_SF16;
    assert(doe_dx_query_caps(&context,&caps,NULL)==DOE_DX_OK);
    assert(!caps.texture_compression_bc && caps.depth32float_stencil8);
    device=device_defaults(); device.missing_format=DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
    assert(doe_dx_query_caps(&context,&caps,NULL)==DOE_DX_OK);
    assert(caps.texture_compression_bc && !caps.depth32float_stencil8);
    device=device_defaults(); device.no_depth_msaa=true;
    assert(doe_dx_query_caps(&context,&caps,NULL)==DOE_DX_OK);
    assert(!caps.depth32float_stencil8);
    for (UINT fault = 1; fault <= 6; ++fault) {
        device = device_defaults();
        device.allocation_fault = fault;
        caps.vendor_id = 77;
        assert(doe_dx_query_caps(&context,&caps,NULL) == DOE_DX_UNSUPPORTED);
        assert(caps.vendor_id == 77 && device.allocation_calls == 1);
    }
    device = device_defaults();
    device.resource_bits = 0;
    assert(doe_dx_query_caps(&context,&caps,NULL) == DOE_DX_UNSUPPORTED);
    assert(!device.allocation_calls);
}

typedef struct sdk_adapter {
    IDXGIAdapter1 iface;
    DXGI_ADAPTER_DESC1 description;
    ULONG references;
    HRESULT describe_result;
} sdk_adapter;
typedef struct sdk_factory {
    IDXGIFactory4 iface;
    sdk_adapter adapters[2];
    UINT count, enumerations, warp_calls, luid_calls;
    bool missing_luid;
} sdk_factory;
static ULONG STDMETHODCALLTYPE adapter_release(IDXGIAdapter1 *raw) {
    sdk_adapter *adapter = (sdk_adapter *)raw;
    assert(adapter->references);
    return --adapter->references;
}
static HRESULT STDMETHODCALLTYPE adapter_description(IDXGIAdapter1 *raw, DXGI_ADAPTER_DESC1 *out) {
    sdk_adapter *adapter = (sdk_adapter *)raw;
    if (FAILED(adapter->describe_result)) return adapter->describe_result;
    *out = adapter->description;
    return S_OK;
}
static HRESULT adapter_result(sdk_factory *factory, UINT ordinal, void **out) {
    if (ordinal >= factory->count) return DXGI_ERROR_NOT_FOUND;
    ++factory->adapters[ordinal].references;
    *out = &factory->adapters[ordinal].iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE enumerate(IDXGIFactory4 *raw, UINT ordinal, IDXGIAdapter1 **out) {
    sdk_factory *factory = (sdk_factory *)raw;
    ++factory->enumerations;
    return adapter_result(factory,ordinal,(void **)out);
}
static HRESULT STDMETHODCALLTYPE warp_adapter(IDXGIFactory4 *raw, REFIID iid, void **out) {
    sdk_factory *factory = (sdk_factory *)raw;
    (void)iid;
    ++factory->warp_calls;
    return adapter_result(factory,0,out);
}
static HRESULT STDMETHODCALLTYPE luid_adapter(IDXGIFactory4 *raw, LUID identity, REFIID iid, void **out) {
    sdk_factory *factory = (sdk_factory *)raw;
    (void)iid;
    assert(identity.LowPart == 19 && identity.HighPart == -3);
    ++factory->luid_calls;
    return factory->missing_luid ? DXGI_ERROR_NOT_FOUND : adapter_result(factory,1,out);
}
static CONST_VTBL IDXGIAdapter1Vtbl adapter_vtable = {.Release=adapter_release,.GetDesc1=adapter_description};
static CONST_VTBL IDXGIFactory4Vtbl factory_vtable = {
    .EnumAdapters1=enumerate,.EnumWarpAdapter=warp_adapter,.EnumAdapterByLuid=luid_adapter
};
static void factory_init(sdk_factory *factory) {
    memset(factory,0,sizeof(*factory));
    factory->iface.lpVtbl = &factory_vtable;
    factory->count = 2;
    for (UINT index = 0; index < 2; ++index) {
        factory->adapters[index].iface.lpVtbl = &adapter_vtable;
        factory->adapters[index].description.Description[0] = L'A';
        factory->adapters[index].description.AdapterLuid.LowPart = 19;
        factory->adapters[index].description.AdapterLuid.HighPart = -3;
    }
    factory->adapters[0].description.Flags = DXGI_ADAPTER_FLAG_SOFTWARE;
}
static void adapter_controls(void) {
    sdk_factory factory;
    factory_init(&factory);
    doe_dx_options options = {.adapter_selection=DOE_DX_SELECT_HARDWARE};
    IDXGIAdapter1 *adapter = NULL;
    doe_dx_caps caps = {0};
    assert(doe_dx_open_adapter(&factory.iface,&options,&adapter,&caps,NULL) == DOE_DX_OK);
    assert(caps.adapter_ordinal == 1 && !caps.software_adapter && factory.enumerations == 2);
    assert(!factory.warp_calls && !factory.luid_calls && !factory.adapters[0].references);
    IDXGIAdapter1_Release(adapter);
    assert(!factory.adapters[1].references);
    factory_init(&factory);
    factory.count = 1;
    caps.vendor_id = 77;
    adapter = NULL;
    assert(doe_dx_open_adapter(&factory.iface,&options,&adapter,&caps,NULL) == DOE_DX_UNAVAILABLE);
    assert(!adapter && caps.vendor_id == 77 && factory.enumerations == 2 && !factory.warp_calls);
    assert(!factory.adapters[0].references);
    factory_init(&factory);
    factory.adapters[0].describe_result = E_FAIL;
    doe_dx_error error = {0};
    assert(doe_dx_open_adapter(&factory.iface,&options,&adapter,&caps,&error) == DOE_DX_NATIVE_FAILURE);
    assert(error.native_code == (int32_t)E_FAIL && factory.enumerations == 1);
    assert(!factory.adapters[0].references && !factory.adapters[1].references);
    factory_init(&factory);
    options.adapter_selection = DOE_DX_SELECT_ORDINAL;
    assert(doe_dx_open_adapter(&factory.iface,&options,&adapter,&caps,NULL) == DOE_DX_UNSUPPORTED);
    assert(factory.enumerations == 1 && !factory.adapters[0].references);
    factory_init(&factory);
    options.adapter_selection = DOE_DX_SELECT_WARP;
    options.allow_software_adapter = true;
    assert(doe_dx_open_adapter(&factory.iface,&options,&adapter,&caps,NULL) == DOE_DX_OK);
    assert(caps.software_adapter && caps.adapter_ordinal == DOE_DX_NO_ADAPTER_ORDINAL);
    assert(factory.warp_calls == 1 && !factory.enumerations && !factory.luid_calls);
    IDXGIAdapter1_Release(adapter);
    options.allow_software_adapter = false;
    options.adapter_selection = DOE_DX_SELECT_LUID;
    options.adapter_identity = (doe_dx_adapter_identity){19,-3,false};
    for (UINT fault = 0; fault < 5; ++fault) {
        factory_init(&factory);
        if (fault == 1) ++factory.adapters[1].description.AdapterLuid.LowPart;
        if (fault == 2) ++factory.adapters[1].description.AdapterLuid.HighPart;
        if (fault == 3) factory.adapters[1].description.Flags = DXGI_ADAPTER_FLAG_SOFTWARE;
        if (fault == 4) factory.missing_luid = true;
        adapter = NULL;
        doe_dx_status status = doe_dx_open_adapter(&factory.iface,&options,&adapter,&caps,NULL);
        assert(status == (fault ? DOE_DX_DEVICE_MISMATCH : DOE_DX_OK));
        assert(factory.luid_calls == 1 && !factory.enumerations && !factory.warp_calls);
        if (!fault) IDXGIAdapter1_Release(adapter);
        else assert(!adapter);
        assert(!factory.adapters[1].references);
    }
}
typedef struct sdk_list {
    ID3D12GraphicsCommandList iface;
    D3D12_RESOURCE_BARRIER barriers[4];
    UINT count;
} sdk_list;
static void STDMETHODCALLTYPE observe_barriers(ID3D12GraphicsCommandList *raw, UINT count,
    const D3D12_RESOURCE_BARRIER *barriers) {
    sdk_list *list = (sdk_list *)raw;
    assert(count <= 4 - list->count);
    for (UINT index = 0; index < count; ++index) list->barriers[list->count++] = barriers[index];
}
static CONST_VTBL ID3D12GraphicsCommandListVtbl list_vtable = {.ResourceBarrier=observe_barriers};
static void alias_transitions(void) {
    sdk_list list = {0};
    list.iface.lpVtbl = &list_vtable;
    doe_dx_native_commands commands = {0};
    commands.list = &list.iface;
    ID3D12Resource first_resource = {0}, second_resource = {0};
    doe_dx_native_buffer first = {0}, second = {0};
    first.resource = &first_resource;
    second.resource = &second_resource;
    doe_dx_native_resources resources = {0};
    resources.buffer_count = 5;
    resources.buffers[0] = (doe_dx_native_binding){&first,0,256};
    resources.buffers[1] = (doe_dx_native_binding){&first,64,256};
    resources.buffers[2] = (doe_dx_native_binding){&first,256,256};
    resources.buffers[3] = (doe_dx_native_binding){&second,0,64};
    resources.buffers[4] = (doe_dx_native_binding){&second,64,64};
    doe_dx_native_pipeline pipeline = {0};
    pipeline.bindings[0] = (doe_dx_binding_layout){0,0,0,DOE_DX_READ_ONLY,DOE_DX_UNIFORM};
    pipeline.bindings[1] = (doe_dx_binding_layout){0,0,4,DOE_DX_READ_ONLY,DOE_DX_STRUCTURED};
    pipeline.bindings[2] = (doe_dx_binding_layout){1,0,0,DOE_DX_READ_ONLY,DOE_DX_UNIFORM};
    pipeline.bindings[3] = (doe_dx_binding_layout){0,0,4,DOE_DX_READ_WRITE,DOE_DX_STRUCTURED};
    pipeline.bindings[4] = (doe_dx_binding_layout){1,0,4,DOE_DX_READ_WRITE,DOE_DX_STRUCTURED};
    for (UINT graphics = 0; graphics < 2; ++graphics) {
        for (UINT restore = 0; restore < 2; ++restore) {
            list.count = 0;
            doe_dx_transition_resources(&commands,&pipeline,&resources,graphics != 0,restore != 0);
            assert(list.count == 2); /* Once per allocation, not five views. */
            D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            if (graphics) read |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            assert(list.barriers[0].Transition.pResource == &first_resource);
            assert(list.barriers[1].Transition.pResource == &second_resource);
            for (UINT index = 0; index < 2; ++index) {
                D3D12_RESOURCE_STATES active = index ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : read;
                assert(list.barriers[index].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION);
                assert(list.barriers[index].Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
                assert(list.barriers[index].Transition.StateBefore == (restore ? active : D3D12_RESOURCE_STATE_COPY_DEST));
                assert(list.barriers[index].Transition.StateAfter == (restore ? D3D12_RESOURCE_STATE_COPY_DEST : active));
            }
        }
    }
}

int main(void) {
    caps_controls();
    adapter_controls();
    alias_transitions();
    puts("D3D12 SDK CPU call controls passed: queried caps/failures, hardware class, exact LUID and alias transitions; no driver/device execution");
    return 0;
}
