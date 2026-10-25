/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32
DXGI_FORMAT doe_dx_texture_resource_format(doe_dx_format format)
{
    switch (format) {
    case DOE_DX_RGBA8_UNORM: case DOE_DX_RGBA8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DOE_DX_BGRA8_UNORM: case DOE_DX_BGRA8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DOE_DX_BC1_UNORM: case DOE_DX_BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_TYPELESS;
    case DOE_DX_BC2_UNORM: case DOE_DX_BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_TYPELESS;
    case DOE_DX_BC3_UNORM: case DOE_DX_BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_TYPELESS;
    case DOE_DX_BC7_UNORM: case DOE_DX_BC7_UNORM_SRGB: return DXGI_FORMAT_BC7_TYPELESS;
    case DOE_DX_D16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
    case DOE_DX_D32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
    case DOE_DX_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24G8_TYPELESS;
    case DOE_DX_D32_FLOAT_S8_UINT: return DXGI_FORMAT_R32G8X24_TYPELESS;
    default: return doe_dx_texture_format(format);
    }
}
static DXGI_FORMAT sampled_format(const doe_dx_native_texture *texture,const doe_dx_texture_view_desc *view)
{
    switch (texture->format) {
    case DOE_DX_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
    case DOE_DX_D32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case DOE_DX_D24_UNORM_S8_UINT: return view->aspect==2 ? DXGI_FORMAT_X24_TYPELESS_G8_UINT : DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DOE_DX_D32_FLOAT_S8_UINT: return view->aspect==2 ? DXGI_FORMAT_X32_TYPELESS_G8X24_UINT : DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default: return doe_dx_texture_format(view->format);
    }
}
void doe_dx_texture_srv(const doe_dx_native_texture *texture,const doe_dx_texture_view_desc *span,D3D12_SHADER_RESOURCE_VIEW_DESC *out)
{
    *out=(D3D12_SHADER_RESOURCE_VIEW_DESC){0};
    out->Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (!span->dimension) {
        out->Format=doe_dx_texture_format(texture->format);
        out->ViewDimension=texture->description.SampleDesc.Count>1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
        if (texture->description.SampleDesc.Count<=1) out->Texture2D.MipLevels=1;
        return;
    }
    out->Format=sampled_format(texture,span);
    if (texture->description.SampleDesc.Count>1) { out->ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DMS; return; }
    switch (span->dimension) {
    case 1:
        out->ViewDimension=D3D12_SRV_DIMENSION_TEXTURE1D;
        out->Texture1D.MostDetailedMip=span->base_mip; out->Texture1D.MipLevels=span->mip_count; break;
    case 2: case 3:
        /* A one-layer 2D view at a nonzero layer needs the native array SRV;
         * selected HLSL 2D lowering uses array index0 consistently. */
        out->ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        out->Texture2DArray.MostDetailedMip=span->base_mip; out->Texture2DArray.MipLevels=span->mip_count;
        out->Texture2DArray.FirstArraySlice=span->base_layer; out->Texture2DArray.ArraySize=span->layer_count;
        out->Texture2DArray.PlaneSlice=span->aspect==2 ? 1u : 0u; break;
    case 4: case 5:
        out->ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        out->TextureCubeArray.MostDetailedMip=span->base_mip; out->TextureCubeArray.MipLevels=span->mip_count;
        out->TextureCubeArray.First2DArrayFace=span->base_layer; out->TextureCubeArray.NumCubes=span->layer_count/6; break;
    case 6:
        out->ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D;
        out->Texture3D.MostDetailedMip=span->base_mip; out->Texture3D.MipLevels=span->mip_count; break;
    }
}
void doe_dx_texture_uav(const doe_dx_native_texture *texture,const doe_dx_texture_view_desc *span,D3D12_UNORDERED_ACCESS_VIEW_DESC *out)
{
    *out=(D3D12_UNORDERED_ACCESS_VIEW_DESC){0};
    out->Format=doe_dx_texture_format(span->dimension ? span->format : texture->format);
    switch (span->dimension) {
    case 1:
        out->ViewDimension=D3D12_UAV_DIMENSION_TEXTURE1D; out->Texture1D.MipSlice=span->base_mip; break;
    case 0:
        out->ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D; break;
    case 2: case 3:
        out->ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        out->Texture2DArray.MipSlice=span->base_mip; out->Texture2DArray.FirstArraySlice=span->base_layer;
        out->Texture2DArray.ArraySize=span->layer_count; break;
    case 6:
        out->ViewDimension=D3D12_UAV_DIMENSION_TEXTURE3D; out->Texture3D.MipSlice=span->base_mip;
        out->Texture3D.WSize=UINT32_MAX; break;
    }
}
#endif
