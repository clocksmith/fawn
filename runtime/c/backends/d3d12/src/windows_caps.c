/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

static uint32_t bounded(uint32_t native, uint32_t owner) {
    return native < owner ? native : owner;
}

static doe_dx_status query(ID3D12Device *device, D3D12_FEATURE feature,
    void *data, UINT size, const char *operation, doe_dx_error *error) {
    return doe_dx_hresult(ID3D12Device_CheckFeatureSupport(device, feature, data, size), operation, error);
}

static doe_dx_status buffer_bound(doe_dx_native_context *context, doe_dx_caps *caps, doe_dx_error *error) {
    uint32_t bits = bounded(caps->gpu_va_bits_per_resource, caps->gpu_va_bits_per_process);
    if (!caps->gpu_va_bits_per_resource || caps->gpu_va_bits_per_resource > 64 ||
        !caps->gpu_va_bits_per_process || caps->gpu_va_bits_per_process > 64)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "caps.gpu_va_bits", error);
    uint64_t address_space = bits == 64 ? UINT64_MAX : UINT64_C(1) << bits;
    /* Deliberate implementation ceiling, not a queried residency budget or a
     * claim that every resource up to the VA ceiling can be allocated. */
    uint64_t ceiling = UINT64_C(256) * 1024u * 1024u;
    if (ceiling > address_space) ceiling = address_space;
    if (ceiling > SIZE_MAX) ceiling = SIZE_MAX;
    D3D12_RESOURCE_DESC description = {0};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = ceiling;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_RESOURCE_ALLOCATION_INFO allocation = {0};
    context->device->lpVtbl->GetResourceAllocationInfo(context->device, &allocation, 0, 1, &description);
    if (allocation.SizeInBytes == UINT64_MAX || allocation.SizeInBytes < ceiling ||
        allocation.SizeInBytes > address_space || !allocation.Alignment ||
        (allocation.Alignment & (allocation.Alignment - 1u)) ||
        allocation.SizeInBytes % allocation.Alignment)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "ID3D12Device.GetResourceAllocationInfo.caps", error);
    caps->limits.max_buffer_size = ceiling;
    caps->limits.max_buffer_allocation_bytes = allocation.SizeInBytes;
    caps->limits.buffer_allocation_alignment = allocation.Alignment;
    return DOE_DX_OK;
}

static doe_dx_status format_support(ID3D12Device *device,DXGI_FORMAT format,UINT required,UINT required2,
    bool *supported,doe_dx_error *error)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT row={0}; row.Format=format;
    HRESULT hr=ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_FORMAT_SUPPORT,&row,sizeof(row));
    if (hr==E_INVALIDARG) { *supported=false; return DOE_DX_OK; }
    if (FAILED(hr)) return doe_dx_hresult(hr,"CheckFeatureSupport.FORMAT_SUPPORT.caps",error);
    *supported=((UINT)row.Support1&required)==required && ((UINT)row.Support2&required2)==required2;
    return DOE_DX_OK;
}
static doe_dx_status optional_formats(ID3D12Device *device,doe_dx_caps *caps,doe_dx_error *error)
{
    static const DXGI_FORMAT bc[]={DXGI_FORMAT_BC1_UNORM,DXGI_FORMAT_BC1_UNORM_SRGB,
        DXGI_FORMAT_BC2_UNORM,DXGI_FORMAT_BC2_UNORM_SRGB,DXGI_FORMAT_BC3_UNORM,DXGI_FORMAT_BC3_UNORM_SRGB,
        DXGI_FORMAT_BC4_UNORM,DXGI_FORMAT_BC4_SNORM,DXGI_FORMAT_BC5_UNORM,DXGI_FORMAT_BC5_SNORM,
        DXGI_FORMAT_BC6H_UF16,DXGI_FORMAT_BC6H_SF16,DXGI_FORMAT_BC7_UNORM,DXGI_FORMAT_BC7_UNORM_SRGB};
    caps->texture_compression_bc=true;
    caps->texture_compression_bc_sliced_3d=true;
    for (size_t i=0;i<sizeof(bc)/sizeof(bc[0]);++i) {
        bool supported=false;
        doe_dx_status status=format_support(device,bc[i],
            D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE,0,&supported,error);
        if (status!=DOE_DX_OK) return status;
        caps->texture_compression_bc=caps->texture_compression_bc&&supported;
        status=format_support(device,bc[i],D3D12_FORMAT_SUPPORT1_TEXTURE3D|
            D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE,0,&supported,error);
        if (status!=DOE_DX_OK) return status;
        caps->texture_compression_bc_sliced_3d=caps->texture_compression_bc_sliced_3d&&supported;
    }
    caps->texture_compression_bc_sliced_3d&=caps->texture_compression_bc;
    const DXGI_FORMAT formats[]={DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
        DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,DXGI_FORMAT_X32_TYPELESS_G8X24_UINT};
    const UINT required[]={D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL|
            D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET,
        D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_SHADER_LOAD|
            D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE|D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON,
        D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_SHADER_LOAD};
    caps->depth32float_stencil8=true;
    for (size_t i=0;i<3;++i) {
        bool supported=false;
        doe_dx_status status=format_support(device,formats[i],required[i],0,&supported,error);
        if (status!=DOE_DX_OK) return status;
        caps->depth32float_stencil8=caps->depth32float_stencil8&&supported;
    }
    if (caps->depth32float_stencil8) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS samples={0};
        samples.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT; samples.SampleCount=4;
        HRESULT hr=ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&samples,sizeof(samples));
        if (hr==E_INVALIDARG) caps->depth32float_stencil8=false;
        else if (FAILED(hr)) return doe_dx_hresult(hr,"CheckFeatureSupport.MULTISAMPLE_QUALITY_LEVELS.caps",error);
        else caps->depth32float_stencil8=samples.NumQualityLevels!=0;
    }
    return DOE_DX_OK;
}

static doe_dx_status format_family(ID3D12Device *device,const DXGI_FORMAT *formats,size_t count,
    UINT support1,UINT support2,bool *out,doe_dx_error *error)
{
    *out=true;
    for (size_t i=0;i<count;++i) {
        bool supported=false;
        doe_dx_status status=format_support(device,formats[i],support1,support2,&supported,error);
        if (status!=DOE_DX_OK) return status;
        *out=*out&&supported;
    }
    return DOE_DX_OK;
}
static doe_dx_status optional_render_formats(ID3D12Device *device,bool *supported,doe_dx_error *error)
{
    const DXGI_FORMAT formats[]={DXGI_FORMAT_R8_SNORM,DXGI_FORMAT_R8G8_SNORM,DXGI_FORMAT_R8G8B8A8_SNORM,
        DXGI_FORMAT_R16_UNORM,DXGI_FORMAT_R16_SNORM,DXGI_FORMAT_R16G16_UNORM,DXGI_FORMAT_R16G16_SNORM,
        DXGI_FORMAT_R16G16B16A16_UNORM,DXGI_FORMAT_R16G16B16A16_SNORM};
    *supported=true;
    for (size_t i=0;i<sizeof(formats)/sizeof(formats[0]);++i) {
        bool valid=false;
        doe_dx_status status=format_support(device,formats[i],D3D12_FORMAT_SUPPORT1_TEXTURE2D|
            D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE|D3D12_FORMAT_SUPPORT1_RENDER_TARGET|
            D3D12_FORMAT_SUPPORT1_BLENDABLE|D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET|
            D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE,0,&valid,error);
        if (status!=DOE_DX_OK) return status;
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality={0};
        quality.Format=formats[i]; quality.SampleCount=4;
        HRESULT hr=ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&quality,sizeof(quality));
        if (hr!=E_INVALIDARG && FAILED(hr)) return doe_dx_hresult(hr,"CheckFeatureSupport.tier1.sample4",error);
        *supported=*supported&&valid&&SUCCEEDED(hr)&&quality.NumQualityLevels!=0;
    }
    return DOE_DX_OK;
}
static doe_dx_status optional_storage_formats(ID3D12Device *device,doe_dx_caps *caps,doe_dx_error *error)
{
    /* Retained tier contracts; every constituent is independently queried.
     * FORMAT_SUPPORT1_SHADER_SAMPLE includes filtering, unlike SHADER_LOAD.
     * https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_format_support1 */
    const DXGI_FORMAT tier1[]={DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R8_SNORM,DXGI_FORMAT_R8_UINT,DXGI_FORMAT_R8_SINT,
        DXGI_FORMAT_R16_UNORM,DXGI_FORMAT_R16_SNORM,DXGI_FORMAT_R16_UINT,DXGI_FORMAT_R16_SINT,DXGI_FORMAT_R16_FLOAT,
        DXGI_FORMAT_R8G8_UNORM,DXGI_FORMAT_R8G8_SNORM,DXGI_FORMAT_R8G8_UINT,DXGI_FORMAT_R8G8_SINT,
        DXGI_FORMAT_R16G16_UNORM,DXGI_FORMAT_R16G16_SNORM,DXGI_FORMAT_R16G16_UINT,DXGI_FORMAT_R16G16_SINT,DXGI_FORMAT_R16G16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_UNORM,DXGI_FORMAT_R16G16B16A16_SNORM,
        DXGI_FORMAT_R10G10B10A2_UINT,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R11G11B10_FLOAT};
    const DXGI_FORMAT tier2[]={DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R8_UINT,DXGI_FORMAT_R8_SINT,
        DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UINT,DXGI_FORMAT_R8G8B8A8_SINT,
        DXGI_FORMAT_R16_UINT,DXGI_FORMAT_R16_SINT,DXGI_FORMAT_R16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_UINT,DXGI_FORMAT_R16G16B16A16_SINT,DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R32G32B32A32_UINT,DXGI_FORMAT_R32G32B32A32_SINT,DXGI_FORMAT_R32G32B32A32_FLOAT};
    const DXGI_FORMAT floats[]={DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT};
    UINT storage=D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_SHADER_LOAD|
        D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
    UINT read_write=D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE|D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
    doe_dx_status status=format_family(device,tier1,sizeof(tier1)/sizeof(tier1[0]),storage,
        D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE,&caps->texture_formats_tier1,error);
    if (status!=DOE_DX_OK) return status;
    status=format_family(device,tier2,sizeof(tier2)/sizeof(tier2[0]),storage,
        read_write,&caps->texture_formats_tier2,error);
    if (status!=DOE_DX_OK) return status;
    status=format_support(device,DXGI_FORMAT_B8G8R8A8_UNORM,storage,D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE,&caps->bgra8unorm_storage,error);
    if (status!=DOE_DX_OK) return status;
    status=format_support(device,DXGI_FORMAT_R11G11B10_FLOAT,D3D12_FORMAT_SUPPORT1_TEXTURE2D|
        D3D12_FORMAT_SUPPORT1_RENDER_TARGET|D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET|
        D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE|D3D12_FORMAT_SUPPORT1_BLENDABLE,0,&caps->rg11b10ufloat_renderable,error);
    if (status!=DOE_DX_OK) return status;
    if (caps->rg11b10ufloat_renderable) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality={0};
        quality.Format=DXGI_FORMAT_R11G11B10_FLOAT; quality.SampleCount=4;
        HRESULT hr=ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&quality,sizeof(quality));
        if (hr==E_INVALIDARG) caps->rg11b10ufloat_renderable=false;
        else if (FAILED(hr)) return doe_dx_hresult(hr,"CheckFeatureSupport.RG11B10.samples",error);
        else caps->rg11b10ufloat_renderable=quality.NumQualityLevels!=0;
    }
    bool render=false;
    status=optional_render_formats(device,&render,error);
    if (status!=DOE_DX_OK) return status;
    caps->texture_formats_tier1&=render&&caps->rg11b10ufloat_renderable;
    caps->texture_formats_tier2&=caps->texture_formats_tier1&&caps->rg11b10ufloat_renderable;
    status=format_family(device,floats,3,D3D12_FORMAT_SUPPORT1_TEXTURE2D|
        D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE,0,&caps->float32_filterable,error);
    if (status!=DOE_DX_OK) return status;
    return format_family(device,floats,3,D3D12_FORMAT_SUPPORT1_TEXTURE2D|
        D3D12_FORMAT_SUPPORT1_RENDER_TARGET|D3D12_FORMAT_SUPPORT1_BLENDABLE,0,&caps->float32_blendable,error);
}

doe_dx_status doe_dx_native_format_caps(doe_dx_native_context *context,doe_dx_format format,
    doe_dx_format_caps *out,doe_dx_error *error)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support={0}; support.Format=doe_dx_texture_format(format);
    HRESULT hr=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support));
    if (hr==E_INVALIDARG) { *out=(doe_dx_format_caps){0}; return DOE_DX_OK; }
    if (FAILED(hr)) return doe_dx_hresult(hr,"CheckFeatureSupport.format_caps",error);
    UINT bits=(UINT)support.Support1,bits2=(UINT)support.Support2;
    doe_dx_format_caps result={
        .texture_1d=(bits&D3D12_FORMAT_SUPPORT1_TEXTURE1D)!=0,
        .texture_2d=(bits&D3D12_FORMAT_SUPPORT1_TEXTURE2D)!=0,
        .texture_3d=(bits&D3D12_FORMAT_SUPPORT1_TEXTURE3D)!=0,
        .texture_cube=(bits&D3D12_FORMAT_SUPPORT1_TEXTURECUBE)!=0,
        .shader_load=(bits&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)!=0,
        .filterable=(bits&D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)!=0,
        .render_target=(bits&D3D12_FORMAT_SUPPORT1_RENDER_TARGET)!=0,
        .blendable=(bits&D3D12_FORMAT_SUPPORT1_BLENDABLE)!=0,
        .depth_stencil=(bits&D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL)!=0,
        .storage_store=(bits&D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW)!=0 && (bits2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)!=0,
        .storage_load=(bits&D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW)!=0 && (bits2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)!=0,
        .resolve=(bits&D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE)!=0,
        .comparison=(bits&D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON)!=0,
        .multisample_load=(bits&D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD)!=0};
    if (bits&D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality={0}; quality.Format=support.Format; quality.SampleCount=4;
        hr=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&quality,sizeof(quality));
        if (hr!=E_INVALIDARG && FAILED(hr)) return doe_dx_hresult(hr,"CheckFeatureSupport.format_caps.sample4",error);
        result.sample4=SUCCEEDED(hr)&&quality.NumQualityLevels!=0;
    }
    DXGI_FORMAT sampled=DXGI_FORMAT_UNKNOWN;
    switch (format) {
    case DOE_DX_D16_UNORM: sampled=DXGI_FORMAT_R16_UNORM; break;
    case DOE_DX_D32_FLOAT: sampled=DXGI_FORMAT_R32_FLOAT; break;
    case DOE_DX_D24_UNORM_S8_UINT: sampled=DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
    case DOE_DX_D32_FLOAT_S8_UINT: sampled=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
    default: break;
    }
    if (sampled!=DXGI_FORMAT_UNKNOWN) {
        support=(D3D12_FEATURE_DATA_FORMAT_SUPPORT){0}; support.Format=sampled;
        hr=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support));
        if (hr!=E_INVALIDARG && FAILED(hr)) return doe_dx_hresult(hr,"CheckFeatureSupport.format_caps.depth_srv",error);
        result.shader_load=SUCCEEDED(hr)&&((UINT)support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)!=0;
        result.filterable=SUCCEEDED(hr)&&((UINT)support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)!=0;
        result.comparison=SUCCEEDED(hr)&&((UINT)support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON)!=0;
        result.multisample_load=SUCCEEDED(hr)&&((UINT)support.Support1&D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD)!=0;
    }
    if (format==DOE_DX_D24_UNORM_S8_UINT || format==DOE_DX_D32_FLOAT_S8_UINT) {
        support=(D3D12_FEATURE_DATA_FORMAT_SUPPORT){0};
        support.Format=format==DOE_DX_D24_UNORM_S8_UINT ? DXGI_FORMAT_X24_TYPELESS_G8_UINT : DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
        hr=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support));
        if (hr!=E_INVALIDARG && FAILED(hr)) return doe_dx_hresult(hr,"CheckFeatureSupport.format_caps.stencil_srv",error);
        result.stencil_load=SUCCEEDED(hr)&&((UINT)support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)!=0;
        /* The combined format promises multisample loads for both aspects. */
        result.multisample_load=result.multisample_load && SUCCEEDED(hr) &&
            ((UINT)support.Support1&D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD)!=0;
    }
    *out=result;
    return DOE_DX_OK;
}

doe_dx_status doe_dx_query_caps(doe_dx_native_context *context, doe_dx_caps *out, doe_dx_error *error) {
    doe_dx_caps caps = *out;
    caps.wave_ops=false; caps.native_16bit_shader_ops=false;
    caps.wave_lane_count_min=0; caps.wave_lane_count_max=0;
    caps.texture_compression_bc=false; caps.depth32float_stencil8=false;
    const D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {0};
    levels.NumFeatureLevels = (UINT)(sizeof(requested) / sizeof(requested[0]));
    levels.pFeatureLevelsRequested = requested;
    doe_dx_status status = query(context->device, D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels),
        "CheckFeatureSupport.FEATURE_LEVELS", error);
    if (status != DOE_DX_OK) return status;
    if (levels.MaxSupportedFeatureLevel < D3D_FEATURE_LEVEL_11_0)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "caps.feature_level", error);
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    status = query(context->device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options),
        "CheckFeatureSupport.D3D12_OPTIONS", error);
    if (status != DOE_DX_OK) return status;
    if (options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_1 ||
        options.ResourceBindingTier > D3D12_RESOURCE_BINDING_TIER_3 ||
        options.ResourceHeapTier < D3D12_RESOURCE_HEAP_TIER_1 || options.ResourceHeapTier > D3D12_RESOURCE_HEAP_TIER_2)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "caps.resource_tiers", error);
    D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT addresses = {0};
    status = query(context->device, D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &addresses, sizeof(addresses),
        "CheckFeatureSupport.GPU_VIRTUAL_ADDRESS_SUPPORT", error);
    if (status != DOE_DX_OK) return status;
    D3D12_FEATURE_DATA_ARCHITECTURE architecture = {0};
    architecture.NodeIndex = 0;
    status = query(context->device, D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture),
        "CheckFeatureSupport.ARCHITECTURE", error);
    if (status != DOE_DX_OK) return status;
    caps.unified_memory = architecture.UMA != FALSE;
    caps.feature_level = (uint32_t)levels.MaxSupportedFeatureLevel;
    caps.resource_binding_tier = (uint32_t)options.ResourceBindingTier;
    caps.resource_heap_tier = (uint32_t)options.ResourceHeapTier;
    caps.gpu_va_bits_per_resource = addresses.MaxGPUVirtualAddressBitsPerResource;
    caps.gpu_va_bits_per_process = addresses.MaxGPUVirtualAddressBitsPerProcess;
    D3D12_FEATURE_DATA_SHADER_MODEL shader = {D3D_SHADER_MODEL_6_2};
    HRESULT shader_result = ID3D12Device_CheckFeatureSupport(context->device,
        D3D12_FEATURE_SHADER_MODEL, &shader, sizeof(shader));
    if (shader_result==E_INVALIDARG) {
        shader.HighestShaderModel=D3D_SHADER_MODEL_6_0;
        shader_result=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_SHADER_MODEL,&shader,sizeof(shader));
    }
    /* Older D3D12 runtimes predate this query. Preserve DXBC use, but expose
     * unknown rather than fabricating SM6 eligibility for a DXC consumer. */
    if (shader_result == E_INVALIDARG) caps.shader_model = 0;
    else if (FAILED(shader_result)) return doe_dx_hresult(shader_result, "CheckFeatureSupport.SHADER_MODEL", error);
    else caps.shader_model = (uint32_t)shader.HighestShaderModel;
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 waves={0};
    HRESULT optional=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_D3D12_OPTIONS1,&waves,sizeof(waves));
    if (optional!=E_INVALIDARG && FAILED(optional)) return doe_dx_hresult(optional,"CheckFeatureSupport.OPTIONS1",error);
    if (SUCCEEDED(optional) && waves.WaveOps) {
        if (!waves.WaveLaneCountMin || waves.WaveLaneCountMin>waves.WaveLaneCountMax)
            return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"caps.wave_lanes",error);
        caps.wave_ops=true; caps.wave_lane_count_min=waves.WaveLaneCountMin; caps.wave_lane_count_max=waves.WaveLaneCountMax;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 native16={0};
    optional=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_D3D12_OPTIONS4,&native16,sizeof(native16));
    if (optional!=E_INVALIDARG && FAILED(optional)) return doe_dx_hresult(optional,"CheckFeatureSupport.OPTIONS4",error);
    if (SUCCEEDED(optional)) caps.native_16bit_shader_ops=native16.Native16BitShaderOpsSupported!=FALSE;
    status=optional_formats(context->device,&caps,error);
    if (status!=DOE_DX_OK) return status;
    status=optional_storage_formats(context->device,&caps,error);
    if (status!=DOE_DX_OK) return status;
    doe_dx_limits *limits = &caps.limits;
    limits->max_descriptors = DOE_DX_MAX_BINDINGS;
    limits->max_static_samplers = DOE_DX_MAX_STATIC_SAMPLERS;
    limits->max_cbvs = options.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_3 ?
        DOE_DX_MAX_BINDINGS : bounded(D3D12_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, DOE_DX_MAX_BINDINGS);
    limits->max_srvs = options.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_1 ?
        bounded(D3D12_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, DOE_DX_MAX_BINDINGS) : DOE_DX_MAX_BINDINGS;
    limits->max_uavs = options.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_1 &&
        levels.MaxSupportedFeatureLevel < D3D_FEATURE_LEVEL_11_1 ?
        bounded(D3D12_PS_CS_UAV_REGISTER_COUNT, DOE_DX_MAX_BINDINGS) : bounded(D3D12_UAV_SLOT_COUNT, DOE_DX_MAX_BINDINGS);
    /* SDK guarantees for the admitted FL11_0+ profile, not optional query facts. */
    limits->max_compute_threads = D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP;
    limits->max_compute_size[0] = D3D12_CS_THREAD_GROUP_MAX_X;
    limits->max_compute_size[1] = D3D12_CS_THREAD_GROUP_MAX_Y;
    limits->max_compute_size[2] = D3D12_CS_THREAD_GROUP_MAX_Z;
    for (size_t axis = 0; axis < 3; ++axis)
        limits->max_dispatch_groups[axis] = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
    limits->max_compute_shared_bytes = D3D12_CS_TGSM_REGISTER_COUNT * (uint32_t)sizeof(DWORD);
    limits->uniform_offset_alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    limits->max_uniform_bytes = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16u;
    status = buffer_bound(context, &caps, error);
    if (status != DOE_DX_OK) return status;
    context->limits = caps.limits;
    *out = caps;
    return DOE_DX_OK;
}
#endif
