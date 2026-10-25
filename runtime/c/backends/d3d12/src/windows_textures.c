/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

_Static_assert(DOE_DX_MAX_TEXTURE_DIMENSION_2D <= D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION,
    "texture implementation ceiling must fit the SDK guaranteed dimension");

DXGI_FORMAT doe_dx_texture_format(doe_dx_format format) {
    switch (format) {
#define DOE_DX_FORMAT(name,public,sdk,bytes,block,kind) case DOE_DX_##name: return DXGI_FORMAT_##sdk;
#include "../include/doe_d3d12_formats.inc"
#undef DOE_DX_FORMAT
    }
    return DXGI_FORMAT_UNKNOWN;
}

void doe_dx_native_texture_destroy(doe_dx_native_texture *texture) {
    if (!texture) return;
    if (texture->resource) ID3D12Resource_Release(texture->resource);
    free(texture->footprints);
    free(texture);
}

static doe_dx_status sampled_depth_support(doe_dx_native_context *context,doe_dx_format format,
    bool multisampled,doe_dx_error *error)
{
    DXGI_FORMAT views[2]={DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_UNKNOWN};
    switch (format) {
    case DOE_DX_D16_UNORM: views[0]=DXGI_FORMAT_R16_UNORM; break;
    case DOE_DX_D32_FLOAT: views[0]=DXGI_FORMAT_R32_FLOAT; break;
    case DOE_DX_D24_UNORM_S8_UINT:
        views[0]=DXGI_FORMAT_R24_UNORM_X8_TYPELESS; views[1]=DXGI_FORMAT_X24_TYPELESS_G8_UINT; break;
    case DOE_DX_D32_FLOAT_S8_UINT:
        views[0]=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; views[1]=DXGI_FORMAT_X32_TYPELESS_G8X24_UINT; break;
    default: return DOE_DX_INVALID_ARGUMENT;
    }
    for (size_t i=0;i<2 && views[i]!=DXGI_FORMAT_UNKNOWN;++i) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support={0}; support.Format=views[i];
        HRESULT result=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support));
        if (FAILED(result)) return doe_dx_hresult(result,"CheckFeatureSupport.depth_srv",error);
        UINT required=D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
        if (multisampled) required|=D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD;
        if (((UINT)support.Support1 & required)!=required)
            return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"texture.depth_srv_support",error);
    }
    return DOE_DX_OK;
}

doe_dx_status doe_dx_native_texture_create(doe_dx_native_context *context,
    const doe_dx_texture_shape *shape, doe_dx_native_texture **out, doe_dx_error *error) {
    const doe_dx_texture_desc *desc=&shape->image;
    uint32_t samples=shape->samples;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {0};
    support.Format = doe_dx_texture_format(desc->format);
    HRESULT result = ID3D12Device_CheckFeatureSupport(context->device,
        D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
    if (FAILED(result)) return doe_dx_hresult(result, "CheckFeatureSupport.format", error);
    doe_dx_format_info info;
    if (!doe_dx_format_properties(desc->format,&info)) return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"texture.format",error);
    bool exact_clear=(desc->usage & DOE_DX_TEXTURE_RENDER_ATTACHMENT) && doe_dx_format_exact_clear(desc->format);
    UINT required = shape->dimension==1 ? D3D12_FORMAT_SUPPORT1_TEXTURE1D :
        shape->dimension==3 ? D3D12_FORMAT_SUPPORT1_TEXTURE3D : D3D12_FORMAT_SUPPORT1_TEXTURE2D;
    if (exact_clear) required |= D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
    if ((desc->usage & DOE_DX_TEXTURE_SAMPLED) && !doe_dx_format_depth(desc->format))
        required |= D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
    if (samples==1 && (desc->usage & DOE_DX_TEXTURE_SAMPLED) &&
        info.kind==1 && desc->format!=DOE_DX_R32_FLOAT &&
        desc->format!=DOE_DX_RG32_FLOAT && desc->format!=DOE_DX_RGBA32_FLOAT)
        required |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
    if (desc->usage & DOE_DX_TEXTURE_STORAGE)
        required |= D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW | D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
    if (desc->usage & DOE_DX_TEXTURE_RENDER_ATTACHMENT) required |= D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
    if (desc->usage & DOE_DX_TEXTURE_DEPTH_ATTACHMENT) required |= D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
    if (samples>1) {
        if ((desc->usage & DOE_DX_TEXTURE_SAMPLED) && !doe_dx_format_depth(desc->format))
            required |= D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD;
        if (desc->usage & DOE_DX_TEXTURE_RENDER_ATTACHMENT)
            required |= D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET;
    }
    if (((UINT)support.Support1 & required) != required)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.native_format", error);
    if ((desc->usage & DOE_DX_TEXTURE_SAMPLED) && doe_dx_format_depth(desc->format)) {
        doe_dx_status supported=sampled_depth_support(context,desc->format,samples>1,error);
        if (supported!=DOE_DX_OK) return supported;
    }
    if ((desc->usage & DOE_DX_TEXTURE_STORAGE) || exact_clear) {
        UINT storage = D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
        /* Load capability follows actual binding access, not allocation usage. */
        if (((UINT)support.Support2 & storage) != storage)
            return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.native_storage", error);
    }
    if (samples>1) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality={0};
        quality.Format=support.Format; quality.SampleCount=samples;
        result=ID3D12Device_CheckFeatureSupport(context->device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
            &quality,sizeof(quality));
        if (FAILED(result)) return doe_dx_hresult(result,"CheckFeatureSupport.multisample",error);
        if (!quality.NumQualityLevels) return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"texture.native_samples",error);
    }
    doe_dx_native_texture *texture = calloc(1, sizeof(*texture));
    if (!texture) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.texture.allocate", error);
    texture->format=desc->format;
    texture->resolve_supported=((UINT)support.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE)!=0;
    D3D12_RESOURCE_DESC *description = &texture->description;
    description->Dimension = shape->dimension==1 ? D3D12_RESOURCE_DIMENSION_TEXTURE1D :
        shape->dimension==3 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description->Width = desc->width;
    description->Height = desc->height;
    description->DepthOrArraySize = (UINT16)shape->depth_or_layers;
    description->MipLevels = (UINT16)shape->mip_levels;
    description->Format = doe_dx_texture_resource_format(desc->format);
    description->SampleDesc.Count = samples;
    description->Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if ((desc->usage & DOE_DX_TEXTURE_STORAGE) || exact_clear)
        description->Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (desc->usage & DOE_DX_TEXTURE_RENDER_ATTACHMENT)
        description->Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (desc->usage & DOE_DX_TEXTURE_DEPTH_ATTACHMENT)
        description->Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
            ((desc->usage & DOE_DX_TEXTURE_SAMPLED) ? D3D12_RESOURCE_FLAG_NONE : D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
    D3D12_HEAP_PROPERTIES heap = {0};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    result = ID3D12Device_CreateCommittedResource(context->device, &heap, D3D12_HEAP_FLAG_NONE,
        description, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
        (void **)&texture->resource);
    if (FAILED(result)) {
        doe_dx_native_texture_destroy(texture);
        return doe_dx_hresult(result, "CreateCommittedResource.texture", error);
    }
    doe_dx_status footprint_status=doe_dx_texture_native_footprints(context,texture,error);
    if (footprint_status!=DOE_DX_OK) { doe_dx_native_texture_destroy(texture); return footprint_status; }
    if (samples>1 || doe_dx_format_stencil(desc->format) || shape->mip_levels!=1 || shape->depth_or_layers!=1) {
        *out=texture;
        return doe_dx_fail(DOE_DX_OK,0,"native.texture.create",error);
    }
    /* Check the admitted format/block layout against the SDK's real
     * footprint query; no backend-private approximation overrides native facts. */
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT rows = 0;
    UINT64 row_bytes = 0, total = 0;
    ID3D12Device_GetCopyableFootprints(context->device, description, 0, 1, 0,
        &footprint, &rows, &row_bytes, &total);
    doe_dx_texture_footprint expected;
    doe_dx_status normalized = doe_dx_texture_layout_2d(desc, (doe_dx_texture_layout){0}, &expected);
    if (normalized != DOE_DX_OK || rows != expected.rows || row_bytes != expected.row_bytes ||
        total != expected.span || footprint.Offset != 0 || footprint.Footprint.RowPitch != expected.row_pitch ||
        footprint.Footprint.Width < desc->width || footprint.Footprint.Width > (desc->width+info.block_width-1)/info.block_width*info.block_width ||
        footprint.Footprint.Height < desc->height || footprint.Footprint.Height > (desc->height+info.block_height-1)/info.block_height*info.block_height ||
        footprint.Footprint.Depth != 1 || footprint.Footprint.Format != description->Format) {
        doe_dx_native_texture_destroy(texture);
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.native_footprint", error);
    }
    *out = texture;
    return doe_dx_fail(DOE_DX_OK, 0, "native.texture.create", error);
}

void doe_dx_native_texture_transfer(doe_dx_native_commands *commands,
    doe_dx_native_buffer *buffer, doe_dx_native_texture *texture,
    const doe_dx_texture_footprint *footprint, bool upload) {
    D3D12_TEXTURE_COPY_LOCATION linear = {0};
    linear.pResource = buffer->resource;
    linear.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    linear.PlacedFootprint.Offset = footprint->offset;
    linear.PlacedFootprint.Footprint.Format = texture->description.Format;
    linear.PlacedFootprint.Footprint.Width = (UINT)texture->description.Width;
    linear.PlacedFootprint.Footprint.Height = texture->description.Height;
    linear.PlacedFootprint.Footprint.Depth = 1;
    linear.PlacedFootprint.Footprint.RowPitch = footprint->row_pitch;
    D3D12_TEXTURE_COPY_LOCATION image = {0};
    image.pResource = texture->resource;
    image.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    if (upload) {
        ID3D12GraphicsCommandList_CopyTextureRegion(commands->list, &image, 0, 0, 0, &linear, NULL);
    } else {
        doe_dx_transition(commands->list, texture->resource,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyTextureRegion(commands->list, &linear, 0, 0, 0, &image, NULL);
        doe_dx_transition(commands->list, texture->resource,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    }
}

void doe_dx_native_texture_copy(doe_dx_native_commands *commands,
    doe_dx_native_texture *source, doe_dx_native_texture *destination) {
    doe_dx_transition(commands->list, source->resource,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyResource(commands->list, destination->resource, source->resource);
    doe_dx_transition(commands->list, source->resource,
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
}
#endif
