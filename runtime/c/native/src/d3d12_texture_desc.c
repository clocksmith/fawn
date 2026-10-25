/* SPDX-License-Identifier: MIT */
#include "d3d12_texture_desc.h"

bool native_d3d12_format(WGPUTextureFormat source,doe_dx_format *out)
{
    doe_dx_format result;
    switch (source) {
#define DOE_DX_FORMAT(name,public,sdk,bytes,block,kind) case WGPUTextureFormat_##public: result=DOE_DX_##name; break;
#include "../../backends/d3d12/include/doe_d3d12_formats.inc"
#undef DOE_DX_FORMAT
    case WGPUTextureFormat_Depth24Plus: result=DOE_DX_D32_FLOAT; break;
    case WGPUTextureFormat_Stencil8: result=DOE_DX_D24_UNORM_S8_UINT; break;
    default: return false;
    }
    *out=result;
    return true;
}

bool native_d3d12_texture_descriptor(const doe_resource_texture_desc *source,doe_dx_texture_desc *out)
{
    if (!source || !out || (source->dimension!=WGPUTextureDimension_1D && source->dimension!=WGPUTextureDimension_2D && source->dimension!=WGPUTextureDimension_3D) ||
        !source->width || !source->height || source->width > DOE_DX_MAX_TEXTURE_DIMENSION_2D || source->height > DOE_DX_MAX_TEXTURE_DIMENSION_2D ||
        !source->depth_or_layers || !source->mip_levels || (source->samples != 1 && source->samples != 4) || !source->usage ||
        (source->view_format_count && !source->view_formats)) return false;
    if (source->samples>1 && (!(source->usage & WGPUTextureUsage_RenderAttachment) ||
        (source->usage & WGPUTextureUsage_StorageBinding))) return false;
    for (size_t i=0;i<source->view_format_count;++i) {
        doe_dx_format mapped;
        if (!native_d3d12_format(source->view_formats[i],&mapped)) return false;
    }
    doe_dx_texture_desc result = {.width=source->width,.height=source->height,.usage=DOE_DX_TEXTURE_COPY_DST};
    if (!native_d3d12_format(source->format,&result.format)) return false;
    if (result.format==DOE_DX_D32_FLOAT || result.format==DOE_DX_D16_UNORM ||
        result.format==DOE_DX_D24_UNORM_S8_UINT || result.format==DOE_DX_D32_FLOAT_S8_UINT) {
        if (source->usage & WGPUTextureUsage_StorageBinding) return false;
        result.usage|=DOE_DX_TEXTURE_DEPTH_ATTACHMENT;
    }
    if (source->usage & ~(WGPUTextureUsage_CopySrc|WGPUTextureUsage_CopyDst|
        WGPUTextureUsage_TextureBinding|WGPUTextureUsage_StorageBinding|WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TransientAttachment)) return false;
    if ((source->usage & WGPUTextureUsage_RenderAttachment) && !(result.usage & DOE_DX_TEXTURE_DEPTH_ATTACHMENT)) {
        result.usage|=DOE_DX_TEXTURE_RENDER_ATTACHMENT;
    }
    if (source->usage & WGPUTextureUsage_CopySrc) result.usage |= DOE_DX_TEXTURE_COPY_SRC;
    if (source->usage & WGPUTextureUsage_TextureBinding) result.usage |= DOE_DX_TEXTURE_SAMPLED;
    if (source->usage & WGPUTextureUsage_StorageBinding) result.usage |= DOE_DX_TEXTURE_STORAGE;
    *out = result;
    return true;
}

bool native_d3d12_texture_region(const doe_resource_texture_copy *copy,doe_dx_texture_region *out)
{
    if (!copy || !out || copy->mip || copy->origin[2] || copy->depth_or_layers != 1 ||
        copy->aspect != WGPUTextureAspect_All || !copy->width || !copy->height) return false;
    *out = (doe_dx_texture_region){copy->origin[0],copy->origin[1],copy->width,copy->height};
    return true;
}

bool native_d3d12_view_descriptor(const doe_resource_view_desc *source,const doe_resource_texture_desc *texture,
    doe_dx_texture_view_desc *out)
{
    if (!source || !texture || !out) return false;
    doe_dx_texture_view_desc result={.base_mip=source->base_mip,.mip_count=source->mip_count,
        .base_layer=source->base_layer,.layer_count=source->layer_count};
    switch (source->aspect) {
    case WGPUTextureAspect_All: break;
    case WGPUTextureAspect_DepthOnly: result.aspect=1; break;
    case WGPUTextureAspect_StencilOnly: result.aspect=2; break;
    default: return false;
    }
    if (!native_d3d12_format(result.aspect ? texture->format : source->format,&result.format)) return false;
    switch (source->dimension) {
    case WGPUTextureViewDimension_1D: result.dimension=1; break;
    case WGPUTextureViewDimension_2D: result.dimension=2; break;
    case WGPUTextureViewDimension_2DArray: result.dimension=3; break;
    case WGPUTextureViewDimension_Cube: result.dimension=4; break;
    case WGPUTextureViewDimension_CubeArray: result.dimension=5; break;
    case WGPUTextureViewDimension_3D: result.dimension=6; break;
    default: return false;
    }
    *out=result;
    return true;
}
