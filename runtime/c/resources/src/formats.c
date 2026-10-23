/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_resource_status resource_format_block(WGPUTextureFormat format, uint32_t *bytes,
    uint32_t *width, uint32_t *height)
{
    uint32_t n;
    switch (format) {
    case WGPUTextureFormat_R8Unorm:
    case WGPUTextureFormat_R8Snorm:
    case WGPUTextureFormat_R8Uint:
    case WGPUTextureFormat_R8Sint:
    case WGPUTextureFormat_Stencil8:
        n = 1;
        break;
    case WGPUTextureFormat_R16Unorm:
    case WGPUTextureFormat_R16Snorm:
    case WGPUTextureFormat_R16Uint:
    case WGPUTextureFormat_R16Sint:
    case WGPUTextureFormat_R16Float:
    case WGPUTextureFormat_RG8Unorm:
    case WGPUTextureFormat_RG8Snorm:
    case WGPUTextureFormat_RG8Uint:
    case WGPUTextureFormat_RG8Sint:
    case WGPUTextureFormat_Depth16Unorm:
        n = 2;
        break;
    case WGPUTextureFormat_RGBA8Unorm:
    case WGPUTextureFormat_RGBA8UnormSrgb:
    case WGPUTextureFormat_RGBA8Snorm:
    case WGPUTextureFormat_RGBA8Uint:
    case WGPUTextureFormat_RGBA8Sint:
    case WGPUTextureFormat_BGRA8Unorm:
    case WGPUTextureFormat_BGRA8UnormSrgb:
    case WGPUTextureFormat_RGB10A2Uint:
    case WGPUTextureFormat_RGB10A2Unorm:
    case WGPUTextureFormat_RG11B10Ufloat:
    case WGPUTextureFormat_RGB9E5Ufloat:
    case WGPUTextureFormat_RG16Unorm:
    case WGPUTextureFormat_RG16Snorm:
    case WGPUTextureFormat_RG16Uint:
    case WGPUTextureFormat_RG16Sint:
    case WGPUTextureFormat_RG16Float:
    case WGPUTextureFormat_R32Uint:
    case WGPUTextureFormat_R32Sint:
    case WGPUTextureFormat_R32Float:
    case WGPUTextureFormat_Depth32Float:
    case WGPUTextureFormat_Depth24Plus:
    case WGPUTextureFormat_Depth24PlusStencil8:
        n = 4;
        break;
    case WGPUTextureFormat_RGBA16Unorm:
    case WGPUTextureFormat_RGBA16Snorm:
    case WGPUTextureFormat_RGBA16Uint:
    case WGPUTextureFormat_RGBA16Sint:
    case WGPUTextureFormat_RGBA16Float:
    case WGPUTextureFormat_RG32Uint:
    case WGPUTextureFormat_RG32Sint:
    case WGPUTextureFormat_RG32Float:
    case WGPUTextureFormat_Depth32FloatStencil8:
        n = 8;
        break;
    case WGPUTextureFormat_BC1RGBAUnorm:
    case WGPUTextureFormat_BC1RGBAUnormSrgb:
    case WGPUTextureFormat_BC4RUnorm:
    case WGPUTextureFormat_BC4RSnorm:
    case WGPUTextureFormat_ETC2RGB8Unorm:
    case WGPUTextureFormat_ETC2RGB8UnormSrgb:
    case WGPUTextureFormat_ETC2RGB8A1Unorm:
    case WGPUTextureFormat_ETC2RGB8A1UnormSrgb:
    case WGPUTextureFormat_EACR11Unorm:
    case WGPUTextureFormat_EACR11Snorm:
        n = 8;
        break;
    case WGPUTextureFormat_BC2RGBAUnorm:
    case WGPUTextureFormat_BC2RGBAUnormSrgb:
    case WGPUTextureFormat_BC3RGBAUnorm:
    case WGPUTextureFormat_BC3RGBAUnormSrgb:
    case WGPUTextureFormat_BC5RGUnorm:
    case WGPUTextureFormat_BC5RGSnorm:
    case WGPUTextureFormat_BC6HRGBUfloat:
    case WGPUTextureFormat_BC6HRGBFloat:
    case WGPUTextureFormat_BC7RGBAUnorm:
    case WGPUTextureFormat_BC7RGBAUnormSrgb:
    case WGPUTextureFormat_ETC2RGBA8Unorm:
    case WGPUTextureFormat_ETC2RGBA8UnormSrgb:
    case WGPUTextureFormat_EACRG11Unorm:
    case WGPUTextureFormat_EACRG11Snorm:
    case WGPUTextureFormat_ASTC4x4Unorm:
    case WGPUTextureFormat_ASTC4x4UnormSrgb:
    case WGPUTextureFormat_ASTC5x4Unorm:
    case WGPUTextureFormat_ASTC5x4UnormSrgb:
    case WGPUTextureFormat_ASTC5x5Unorm:
    case WGPUTextureFormat_ASTC5x5UnormSrgb:
    case WGPUTextureFormat_ASTC6x5Unorm:
    case WGPUTextureFormat_ASTC6x5UnormSrgb:
    case WGPUTextureFormat_ASTC6x6Unorm:
    case WGPUTextureFormat_ASTC6x6UnormSrgb:
    case WGPUTextureFormat_ASTC8x5Unorm:
    case WGPUTextureFormat_ASTC8x5UnormSrgb:
    case WGPUTextureFormat_ASTC8x6Unorm:
    case WGPUTextureFormat_ASTC8x6UnormSrgb:
    case WGPUTextureFormat_ASTC8x8Unorm:
    case WGPUTextureFormat_ASTC8x8UnormSrgb:
    case WGPUTextureFormat_ASTC10x5Unorm:
    case WGPUTextureFormat_ASTC10x5UnormSrgb:
    case WGPUTextureFormat_ASTC10x6Unorm:
    case WGPUTextureFormat_ASTC10x6UnormSrgb:
    case WGPUTextureFormat_ASTC10x8Unorm:
    case WGPUTextureFormat_ASTC10x8UnormSrgb:
    case WGPUTextureFormat_ASTC10x10Unorm:
    case WGPUTextureFormat_ASTC10x10UnormSrgb:
    case WGPUTextureFormat_ASTC12x10Unorm:
    case WGPUTextureFormat_ASTC12x10UnormSrgb:
    case WGPUTextureFormat_ASTC12x12Unorm:
    case WGPUTextureFormat_ASTC12x12UnormSrgb:
        n = 16;
        break;
    case WGPUTextureFormat_RGBA32Uint:
    case WGPUTextureFormat_RGBA32Sint:
    case WGPUTextureFormat_RGBA32Float:
        n = 16;
        break;
    default: return DOE_RESOURCE_UNSUPPORTED;
    }
    uint32_t w = 1, h = 1;
    if (format >= WGPUTextureFormat_BC1RGBAUnorm && format <= WGPUTextureFormat_EACRG11Snorm) {
        w = 4;
        h = 4;
    } else if (format >= WGPUTextureFormat_ASTC4x4Unorm && format <= WGPUTextureFormat_ASTC12x12UnormSrgb) {
        static const uint8_t extents[][2] = {
            {4,4}, {5,4}, {5,5}, {6,5}, {6,6}, {8,5}, {8,6},
            {8,8}, {10,5}, {10,6}, {10,8}, {10,10}, {12,10}, {12,12}
        };
        size_t index = ((unsigned)format - WGPUTextureFormat_ASTC4x4Unorm) / 2;
        w = extents[index][0];
        h = extents[index][1];
    }
    *bytes = n;
    *width = w;
    *height = h;
    return DOE_RESOURCE_OK;
}

bool resource_format_compatible(WGPUTextureFormat base, WGPUTextureFormat view)
{
    if (base == view) return true;
    return (base == WGPUTextureFormat_RGBA8Unorm && view == WGPUTextureFormat_RGBA8UnormSrgb) ||
        (view == WGPUTextureFormat_RGBA8Unorm && base == WGPUTextureFormat_RGBA8UnormSrgb) ||
        (base == WGPUTextureFormat_BGRA8Unorm && view == WGPUTextureFormat_BGRA8UnormSrgb) ||
        (view == WGPUTextureFormat_BGRA8Unorm && base == WGPUTextureFormat_BGRA8UnormSrgb);
}

bool resource_format_depth(WGPUTextureFormat format)
{
    return format >= WGPUTextureFormat_Stencil8 && format <= WGPUTextureFormat_Depth32FloatStencil8;
}

bool resource_format_aspect(WGPUTextureFormat format, WGPUTextureAspect aspect)
{
    if (aspect == WGPUTextureAspect_All) return true;
    if (aspect == WGPUTextureAspect_DepthOnly)
        return resource_format_depth(format) && format != WGPUTextureFormat_Stencil8;
    if (aspect == WGPUTextureAspect_StencilOnly)
        return format == WGPUTextureFormat_Stencil8 ||
            format == WGPUTextureFormat_Depth24PlusStencil8 || format == WGPUTextureFormat_Depth32FloatStencil8;
    return false;
}

unsigned resource_format_aspects(WGPUTextureFormat format)
{
    if (!resource_format_depth(format)) return 1;
    unsigned aspects = 0;
    if (resource_format_aspect(format, WGPUTextureAspect_DepthOnly)) aspects |= 2;
    if (resource_format_aspect(format, WGPUTextureAspect_StencilOnly)) aspects |= 4;
    return aspects;
}

static WGPUTextureSampleType format_sample_type(WGPUTextureFormat format)
{
    switch (format) {
    case WGPUTextureFormat_R32Float:
    case WGPUTextureFormat_RG32Float:
    case WGPUTextureFormat_RGBA32Float:
        return WGPUTextureSampleType_UnfilterableFloat;
    case WGPUTextureFormat_Stencil8:
        return WGPUTextureSampleType_Uint;
    case WGPUTextureFormat_Depth16Unorm:
    case WGPUTextureFormat_Depth24Plus:
    case WGPUTextureFormat_Depth24PlusStencil8:
    case WGPUTextureFormat_Depth32Float:
    case WGPUTextureFormat_Depth32FloatStencil8:
        return WGPUTextureSampleType_Depth;
    case WGPUTextureFormat_R8Uint:
    case WGPUTextureFormat_R16Uint:
    case WGPUTextureFormat_RG8Uint:
    case WGPUTextureFormat_R32Uint:
    case WGPUTextureFormat_RG16Uint:
    case WGPUTextureFormat_RGBA8Uint:
    case WGPUTextureFormat_RGB10A2Uint:
    case WGPUTextureFormat_RG32Uint:
    case WGPUTextureFormat_RGBA16Uint:
    case WGPUTextureFormat_RGBA32Uint:
        return WGPUTextureSampleType_Uint;
    case WGPUTextureFormat_R8Sint:
    case WGPUTextureFormat_R16Sint:
    case WGPUTextureFormat_RG8Sint:
    case WGPUTextureFormat_R32Sint:
    case WGPUTextureFormat_RG16Sint:
    case WGPUTextureFormat_RGBA8Sint:
    case WGPUTextureFormat_RG32Sint:
    case WGPUTextureFormat_RGBA16Sint:
    case WGPUTextureFormat_RGBA32Sint:
        return WGPUTextureSampleType_Sint;
    default: return WGPUTextureSampleType_Float;
    }
}

doe_resource_status doe_resource_format_aspect_format(WGPUTextureFormat format,
    WGPUTextureAspect aspect, WGPUTextureFormat *out)
{
    if (!out) return DOE_RESOURCE_INVALID_ARGUMENT;
    uint32_t bytes, width, height;
    doe_resource_status status = resource_format_block(format, &bytes, &width, &height);
    if (status != DOE_RESOURCE_OK) return status;
    if (!resource_format_aspect(format, aspect)) return DOE_RESOURCE_LAYOUT;
    WGPUTextureFormat resolved = format;
    if (aspect == WGPUTextureAspect_StencilOnly) resolved = WGPUTextureFormat_Stencil8;
    else if (aspect == WGPUTextureAspect_DepthOnly) {
        if (format == WGPUTextureFormat_Depth24PlusStencil8) resolved = WGPUTextureFormat_Depth24Plus;
        if (format == WGPUTextureFormat_Depth32FloatStencil8) resolved = WGPUTextureFormat_Depth32Float;
    }
    *out = resolved;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_format_sample_type(WGPUTextureFormat format,
    WGPUTextureAspect aspect, WGPUTextureSampleType *out)
{
    if (!out) return DOE_RESOURCE_INVALID_ARGUMENT;
    WGPUTextureFormat resolved;
    doe_resource_status status = doe_resource_format_aspect_format(format, aspect, &resolved);
    if (status != DOE_RESOURCE_OK) return status;
    *out = format_sample_type(resolved);
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_format_color_components(WGPUTextureFormat format,
    uint32_t *out)
{
    if (!out) return DOE_RESOURCE_INVALID_ARGUMENT;
    uint32_t bytes, width, height;
    doe_resource_status status = resource_format_block(format, &bytes, &width, &height);
    if (status != DOE_RESOURCE_OK) return status;
    if (resource_format_depth(format)) return DOE_RESOURCE_LAYOUT;
    uint32_t components = 4;
    switch (format) {
    case WGPUTextureFormat_R8Unorm:
    case WGPUTextureFormat_R8Snorm:
    case WGPUTextureFormat_R8Uint:
    case WGPUTextureFormat_R8Sint:
    case WGPUTextureFormat_R16Unorm:
    case WGPUTextureFormat_R16Snorm:
    case WGPUTextureFormat_R16Uint:
    case WGPUTextureFormat_R16Sint:
    case WGPUTextureFormat_R16Float:
    case WGPUTextureFormat_R32Uint:
    case WGPUTextureFormat_R32Sint:
    case WGPUTextureFormat_R32Float:
    case WGPUTextureFormat_BC4RUnorm:
    case WGPUTextureFormat_BC4RSnorm:
    case WGPUTextureFormat_EACR11Unorm:
    case WGPUTextureFormat_EACR11Snorm:
        components = 1;
        break;
    case WGPUTextureFormat_RG8Unorm:
    case WGPUTextureFormat_RG8Snorm:
    case WGPUTextureFormat_RG8Uint:
    case WGPUTextureFormat_RG8Sint:
    case WGPUTextureFormat_RG16Unorm:
    case WGPUTextureFormat_RG16Snorm:
    case WGPUTextureFormat_RG16Uint:
    case WGPUTextureFormat_RG16Sint:
    case WGPUTextureFormat_RG16Float:
    case WGPUTextureFormat_RG32Uint:
    case WGPUTextureFormat_RG32Sint:
    case WGPUTextureFormat_RG32Float:
    case WGPUTextureFormat_BC5RGUnorm:
    case WGPUTextureFormat_BC5RGSnorm:
    case WGPUTextureFormat_EACRG11Unorm:
    case WGPUTextureFormat_EACRG11Snorm:
        components = 2;
        break;
    case WGPUTextureFormat_RG11B10Ufloat:
    case WGPUTextureFormat_RGB9E5Ufloat:
    case WGPUTextureFormat_BC6HRGBUfloat:
    case WGPUTextureFormat_BC6HRGBFloat:
    case WGPUTextureFormat_ETC2RGB8Unorm:
    case WGPUTextureFormat_ETC2RGB8UnormSrgb:
        components = 3;
        break;
    default:
        break; /* Remaining known color formats all contain RGBA channels. */
    }
    *out = components;
    return DOE_RESOURCE_OK;
}

/* WebGPU core storage-texture formats (texture format capabilities table):
 * read-only and write-only for all, read-write only for the r32 formats.
 * bgra8unorm needs bgra8unorm-storage and tier formats need texture-formats-
 * tier1/2; neither feature is offered, so those remain non-storage formats.
 * https://gpuweb.github.io/gpuweb/#plain-color-formats */
bool resource_storage_format(WGPUTextureFormat format, bool *read_write)
{
    switch (format) {
    case WGPUTextureFormat_R32Float:
    case WGPUTextureFormat_R32Uint:
    case WGPUTextureFormat_R32Sint:
        *read_write = true;
        return true;
    case WGPUTextureFormat_RGBA8Unorm:
    case WGPUTextureFormat_RGBA8Snorm:
    case WGPUTextureFormat_RGBA8Uint:
    case WGPUTextureFormat_RGBA8Sint:
    case WGPUTextureFormat_RGBA16Uint:
    case WGPUTextureFormat_RGBA16Sint:
    case WGPUTextureFormat_RGBA16Float:
    case WGPUTextureFormat_RG32Uint:
    case WGPUTextureFormat_RG32Sint:
    case WGPUTextureFormat_RG32Float:
    case WGPUTextureFormat_RGBA32Uint:
    case WGPUTextureFormat_RGBA32Sint:
    case WGPUTextureFormat_RGBA32Float:
        *read_write = false;
        return true;
    default:
        return false;
    }
}
