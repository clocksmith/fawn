/* SPDX-License-Identifier: MIT */
#include "d3d12_texture_desc.h"

bool native_d3d12_texture_descriptor(const doe_resource_texture_desc *source,doe_dx_texture_desc *out)
{
    if (!source || !out || source->dimension != WGPUTextureDimension_2D ||
        !source->width || !source->height || source->width > DOE_DX_MAX_TEXTURE_DIMENSION_2D || source->height > DOE_DX_MAX_TEXTURE_DIMENSION_2D ||
        source->depth_or_layers != 1 || source->mip_levels != 1 || source->samples != 1 || !source->usage ||
        (source->view_format_count && !source->view_formats)) return false;
    for (size_t i = 0; i < source->view_format_count; ++i)
        if (source->view_formats[i] != source->format) return false;
    doe_dx_texture_desc result = {.width=source->width,.height=source->height,.usage=DOE_DX_TEXTURE_COPY_DST};
    switch (source->format) {
    case WGPUTextureFormat_RGBA8Unorm: result.format=DOE_DX_RGBA8_UNORM; break;
    case WGPUTextureFormat_RGBA8Uint: result.format=DOE_DX_RGBA8_UINT; break;
    case WGPUTextureFormat_R32Float: result.format=DOE_DX_R32_FLOAT; break;
    case WGPUTextureFormat_R32Uint: result.format=DOE_DX_R32_UINT; break;
    case WGPUTextureFormat_BGRA8Unorm: result.format=DOE_DX_BGRA8_UNORM; break;
    default: return false;
    }
    if (source->usage & ~(WGPUTextureUsage_CopySrc|WGPUTextureUsage_CopyDst|
        WGPUTextureUsage_TextureBinding|WGPUTextureUsage_StorageBinding)) return false;
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
