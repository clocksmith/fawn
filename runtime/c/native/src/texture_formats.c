/* SPDX-License-Identifier: MIT
 * Semantic names cross each enum domain; retained model numbers are not ABI. */
#include "internal.h"

/* A format whose WebGPU definition names a feature maps only when the device
 * enabled that feature, wherever a format is admitted (textures, views,
 * pipeline targets, bundles, layouts). Core formats need nothing. */
static bool format_feature_enabled(uint32_t features, WGPUTextureFormat format)
{
    switch (format) {
    case WGPUTextureFormat_Depth32FloatStencil8:
        return (features & NATIVE_FEATURE(WGPUFeatureName_Depth32FloatStencil8)) != 0;
    case WGPUTextureFormat_BC1RGBAUnorm: case WGPUTextureFormat_BC1RGBAUnormSrgb:
    case WGPUTextureFormat_BC2RGBAUnorm: case WGPUTextureFormat_BC2RGBAUnormSrgb:
    case WGPUTextureFormat_BC3RGBAUnorm: case WGPUTextureFormat_BC3RGBAUnormSrgb:
    case WGPUTextureFormat_BC4RUnorm: case WGPUTextureFormat_BC4RSnorm:
    case WGPUTextureFormat_BC5RGUnorm: case WGPUTextureFormat_BC5RGSnorm:
    case WGPUTextureFormat_BC6HRGBUfloat: case WGPUTextureFormat_BC6HRGBFloat:
    case WGPUTextureFormat_BC7RGBAUnorm: case WGPUTextureFormat_BC7RGBAUnormSrgb:
        return (features & NATIVE_FEATURE(WGPUFeatureName_TextureCompressionBC)) != 0;
    case WGPUTextureFormat_ETC2RGB8Unorm:
    case WGPUTextureFormat_ETC2RGB8UnormSrgb:
    case WGPUTextureFormat_ETC2RGB8A1Unorm:
    case WGPUTextureFormat_ETC2RGB8A1UnormSrgb:
    case WGPUTextureFormat_ETC2RGBA8Unorm:
    case WGPUTextureFormat_ETC2RGBA8UnormSrgb:
    case WGPUTextureFormat_EACR11Unorm:
    case WGPUTextureFormat_EACR11Snorm:
    case WGPUTextureFormat_EACRG11Unorm:
    case WGPUTextureFormat_EACRG11Snorm:
        return (features & NATIVE_FEATURE(WGPUFeatureName_TextureCompressionETC2)) != 0;
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
        return (features & NATIVE_FEATURE(WGPUFeatureName_TextureCompressionASTC)) != 0;
    default: return true;
    }
}

/* Plus depth may use 32-bit float backing. */
bool native_depth_format(uint32_t features, WGPUTextureFormat format, doe_vk_texture_format *out)
{
    if (!format_feature_enabled(features, format)) return false;
    switch (format) {
    case WGPUTextureFormat_Depth16Unorm: *out = DOE_VK_FORMAT_D16_UNORM; return true;
    case WGPUTextureFormat_Depth24Plus:
    case WGPUTextureFormat_Depth32Float: *out = DOE_VK_FORMAT_D32_FLOAT; return true;
    case WGPUTextureFormat_Stencil8: *out = DOE_VK_FORMAT_S8_UINT; return true;
    case WGPUTextureFormat_Depth24PlusStencil8:
    case WGPUTextureFormat_Depth32FloatStencil8: *out = DOE_VK_FORMAT_D32_FLOAT_S8_UINT; return true;
    default: return false;
    }
}

bool native_texture_format(uint32_t features, WGPUTextureFormat format, doe_vk_texture_format *out)
{
    if (!format_feature_enabled(features, format)) return false;
    switch (format) {
    case WGPUTextureFormat_R8Unorm: *out = DOE_VK_FORMAT_R8_UNORM; break;
    case WGPUTextureFormat_R8Snorm: *out = DOE_VK_FORMAT_R8_SNORM; break;
    case WGPUTextureFormat_R8Uint: *out = DOE_VK_FORMAT_R8_UINT; break;
    case WGPUTextureFormat_R8Sint: *out = DOE_VK_FORMAT_R8_SINT; break;
    case WGPUTextureFormat_RG8Unorm: *out = DOE_VK_FORMAT_RG8_UNORM; break;
    case WGPUTextureFormat_RG8Snorm: *out = DOE_VK_FORMAT_RG8_SNORM; break;
    case WGPUTextureFormat_RG8Uint: *out = DOE_VK_FORMAT_RG8_UINT; break;
    case WGPUTextureFormat_RG8Sint: *out = DOE_VK_FORMAT_RG8_SINT; break;
    case WGPUTextureFormat_R16Uint: *out = DOE_VK_FORMAT_R16_UINT; break;
    case WGPUTextureFormat_R16Sint: *out = DOE_VK_FORMAT_R16_SINT; break;
    case WGPUTextureFormat_RG16Uint: *out = DOE_VK_FORMAT_RG16_UINT; break;
    case WGPUTextureFormat_RG16Sint: *out = DOE_VK_FORMAT_RG16_SINT; break;
    case WGPUTextureFormat_RGB10A2Unorm: *out = DOE_VK_FORMAT_RGB10A2_UNORM; break;
    case WGPUTextureFormat_RGB10A2Uint: *out = DOE_VK_FORMAT_RGB10A2_UINT; break;
    case WGPUTextureFormat_RGB9E5Ufloat: *out = DOE_VK_FORMAT_RGB9E5_UFLOAT; break;
    case WGPUTextureFormat_RGBA8Unorm: *out = DOE_VK_FORMAT_RGBA8_UNORM; break;
    case WGPUTextureFormat_RGBA8UnormSrgb: *out = DOE_VK_FORMAT_RGBA8_SRGB; break;
    case WGPUTextureFormat_RGBA8Snorm: *out = DOE_VK_FORMAT_RGBA8_SNORM; break;
    case WGPUTextureFormat_RGBA8Uint: *out = DOE_VK_FORMAT_RGBA8_UINT; break;
    case WGPUTextureFormat_RGBA8Sint: *out = DOE_VK_FORMAT_RGBA8_SINT; break;
    case WGPUTextureFormat_RGBA16Uint: *out = DOE_VK_FORMAT_RGBA16_UINT; break;
    case WGPUTextureFormat_RGBA16Sint: *out = DOE_VK_FORMAT_RGBA16_SINT; break;
    case WGPUTextureFormat_BGRA8Unorm: *out = DOE_VK_FORMAT_BGRA8_UNORM; break;
    case WGPUTextureFormat_BGRA8UnormSrgb: *out = DOE_VK_FORMAT_BGRA8_SRGB; break;
    case WGPUTextureFormat_R16Float: *out = DOE_VK_FORMAT_R16_FLOAT; break;
    case WGPUTextureFormat_RG16Float: *out = DOE_VK_FORMAT_RG16_FLOAT; break;
    case WGPUTextureFormat_RGBA16Float: *out = DOE_VK_FORMAT_RGBA16_FLOAT; break;
    case WGPUTextureFormat_R32Uint: *out = DOE_VK_FORMAT_R32_UINT; break;
    case WGPUTextureFormat_R32Sint: *out = DOE_VK_FORMAT_R32_SINT; break;
    case WGPUTextureFormat_R32Float: *out = DOE_VK_FORMAT_R32_FLOAT; break;
    case WGPUTextureFormat_RG32Uint: *out = DOE_VK_FORMAT_RG32_UINT; break;
    case WGPUTextureFormat_RG32Sint: *out = DOE_VK_FORMAT_RG32_SINT; break;
    case WGPUTextureFormat_RG32Float: *out = DOE_VK_FORMAT_RG32_FLOAT; break;
    case WGPUTextureFormat_RGBA32Uint: *out = DOE_VK_FORMAT_RGBA32_UINT; break;
    case WGPUTextureFormat_RGBA32Sint: *out = DOE_VK_FORMAT_RGBA32_SINT; break;
    case WGPUTextureFormat_RGBA32Float: *out = DOE_VK_FORMAT_RGBA32_FLOAT; break;
    case WGPUTextureFormat_RG11B10Ufloat: *out = DOE_VK_FORMAT_RG11B10_UFLOAT; break;
    case WGPUTextureFormat_BC1RGBAUnorm: *out = DOE_VK_FORMAT_BC1_RGBA_UNORM; break;
    case WGPUTextureFormat_BC1RGBAUnormSrgb: *out = DOE_VK_FORMAT_BC1_RGBA_SRGB; break;
    case WGPUTextureFormat_BC2RGBAUnorm: *out = DOE_VK_FORMAT_BC2_RGBA_UNORM; break;
    case WGPUTextureFormat_BC2RGBAUnormSrgb: *out = DOE_VK_FORMAT_BC2_RGBA_SRGB; break;
    case WGPUTextureFormat_BC3RGBAUnorm: *out = DOE_VK_FORMAT_BC3_RGBA_UNORM; break;
    case WGPUTextureFormat_BC3RGBAUnormSrgb: *out = DOE_VK_FORMAT_BC3_RGBA_SRGB; break;
    case WGPUTextureFormat_BC4RUnorm: *out = DOE_VK_FORMAT_BC4_R_UNORM; break;
    case WGPUTextureFormat_BC4RSnorm: *out = DOE_VK_FORMAT_BC4_R_SNORM; break;
    case WGPUTextureFormat_BC5RGUnorm: *out = DOE_VK_FORMAT_BC5_RG_UNORM; break;
    case WGPUTextureFormat_BC5RGSnorm: *out = DOE_VK_FORMAT_BC5_RG_SNORM; break;
    case WGPUTextureFormat_BC6HRGBUfloat: *out = DOE_VK_FORMAT_BC6H_RGB_UFLOAT; break;
    case WGPUTextureFormat_BC6HRGBFloat: *out = DOE_VK_FORMAT_BC6H_RGB_FLOAT; break;
    case WGPUTextureFormat_BC7RGBAUnorm: *out = DOE_VK_FORMAT_BC7_RGBA_UNORM; break;
    case WGPUTextureFormat_BC7RGBAUnormSrgb: *out = DOE_VK_FORMAT_BC7_RGBA_SRGB; break;
    case WGPUTextureFormat_ETC2RGB8Unorm: *out = DOE_VK_FORMAT_ETC2_RGB8_UNORM; break;
    case WGPUTextureFormat_ETC2RGB8UnormSrgb: *out = DOE_VK_FORMAT_ETC2_RGB8_SRGB; break;
    case WGPUTextureFormat_ETC2RGB8A1Unorm: *out = DOE_VK_FORMAT_ETC2_RGB8A1_UNORM; break;
    case WGPUTextureFormat_ETC2RGB8A1UnormSrgb: *out = DOE_VK_FORMAT_ETC2_RGB8A1_SRGB; break;
    case WGPUTextureFormat_ETC2RGBA8Unorm: *out = DOE_VK_FORMAT_ETC2_RGBA8_UNORM; break;
    case WGPUTextureFormat_ETC2RGBA8UnormSrgb: *out = DOE_VK_FORMAT_ETC2_RGBA8_SRGB; break;
    case WGPUTextureFormat_EACR11Unorm: *out = DOE_VK_FORMAT_EAC_R11_UNORM; break;
    case WGPUTextureFormat_EACR11Snorm: *out = DOE_VK_FORMAT_EAC_R11_SNORM; break;
    case WGPUTextureFormat_EACRG11Unorm: *out = DOE_VK_FORMAT_EAC_RG11_UNORM; break;
    case WGPUTextureFormat_EACRG11Snorm: *out = DOE_VK_FORMAT_EAC_RG11_SNORM; break;
    case WGPUTextureFormat_ASTC4x4Unorm: *out = DOE_VK_FORMAT_ASTC_4x4_UNORM; break;
    case WGPUTextureFormat_ASTC4x4UnormSrgb: *out = DOE_VK_FORMAT_ASTC_4x4_SRGB; break;
    case WGPUTextureFormat_ASTC5x4Unorm: *out = DOE_VK_FORMAT_ASTC_5x4_UNORM; break;
    case WGPUTextureFormat_ASTC5x4UnormSrgb: *out = DOE_VK_FORMAT_ASTC_5x4_SRGB; break;
    case WGPUTextureFormat_ASTC5x5Unorm: *out = DOE_VK_FORMAT_ASTC_5x5_UNORM; break;
    case WGPUTextureFormat_ASTC5x5UnormSrgb: *out = DOE_VK_FORMAT_ASTC_5x5_SRGB; break;
    case WGPUTextureFormat_ASTC6x5Unorm: *out = DOE_VK_FORMAT_ASTC_6x5_UNORM; break;
    case WGPUTextureFormat_ASTC6x5UnormSrgb: *out = DOE_VK_FORMAT_ASTC_6x5_SRGB; break;
    case WGPUTextureFormat_ASTC6x6Unorm: *out = DOE_VK_FORMAT_ASTC_6x6_UNORM; break;
    case WGPUTextureFormat_ASTC6x6UnormSrgb: *out = DOE_VK_FORMAT_ASTC_6x6_SRGB; break;
    case WGPUTextureFormat_ASTC8x5Unorm: *out = DOE_VK_FORMAT_ASTC_8x5_UNORM; break;
    case WGPUTextureFormat_ASTC8x5UnormSrgb: *out = DOE_VK_FORMAT_ASTC_8x5_SRGB; break;
    case WGPUTextureFormat_ASTC8x6Unorm: *out = DOE_VK_FORMAT_ASTC_8x6_UNORM; break;
    case WGPUTextureFormat_ASTC8x6UnormSrgb: *out = DOE_VK_FORMAT_ASTC_8x6_SRGB; break;
    case WGPUTextureFormat_ASTC8x8Unorm: *out = DOE_VK_FORMAT_ASTC_8x8_UNORM; break;
    case WGPUTextureFormat_ASTC8x8UnormSrgb: *out = DOE_VK_FORMAT_ASTC_8x8_SRGB; break;
    case WGPUTextureFormat_ASTC10x5Unorm: *out = DOE_VK_FORMAT_ASTC_10x5_UNORM; break;
    case WGPUTextureFormat_ASTC10x5UnormSrgb: *out = DOE_VK_FORMAT_ASTC_10x5_SRGB; break;
    case WGPUTextureFormat_ASTC10x6Unorm: *out = DOE_VK_FORMAT_ASTC_10x6_UNORM; break;
    case WGPUTextureFormat_ASTC10x6UnormSrgb: *out = DOE_VK_FORMAT_ASTC_10x6_SRGB; break;
    case WGPUTextureFormat_ASTC10x8Unorm: *out = DOE_VK_FORMAT_ASTC_10x8_UNORM; break;
    case WGPUTextureFormat_ASTC10x8UnormSrgb: *out = DOE_VK_FORMAT_ASTC_10x8_SRGB; break;
    case WGPUTextureFormat_ASTC10x10Unorm: *out = DOE_VK_FORMAT_ASTC_10x10_UNORM; break;
    case WGPUTextureFormat_ASTC10x10UnormSrgb: *out = DOE_VK_FORMAT_ASTC_10x10_SRGB; break;
    case WGPUTextureFormat_ASTC12x10Unorm: *out = DOE_VK_FORMAT_ASTC_12x10_UNORM; break;
    case WGPUTextureFormat_ASTC12x10UnormSrgb: *out = DOE_VK_FORMAT_ASTC_12x10_SRGB; break;
    case WGPUTextureFormat_ASTC12x12Unorm: *out = DOE_VK_FORMAT_ASTC_12x12_UNORM; break;
    case WGPUTextureFormat_ASTC12x12UnormSrgb: *out = DOE_VK_FORMAT_ASTC_12x12_SRGB; break;
    default: return native_depth_format(features, format, out);
    }
    return true;
}

/* Feature-dependent renderability of a mapped color format: rg11b10ufloat is a
 * core sampled format, renderable (blendable, multisampled, resolvable) only
 * with rg11b10ufloat-renderable. The backend still checks native support. */
bool native_color_renderable(uint32_t features, WGPUTextureFormat format)
{
    return format != WGPUTextureFormat_RG11B10Ufloat ||
        (features & NATIVE_FEATURE(WGPUFeatureName_RG11B10UfloatRenderable)) != 0;
}

bool native_storage_format(uint32_t format, WGPUTextureFormat *out)
{
    switch (format) {
    case DOE_COMPILER_FORMAT_RGBA32FLOAT: *out = WGPUTextureFormat_RGBA32Float; break;
    case DOE_COMPILER_FORMAT_RGBA16FLOAT: *out = WGPUTextureFormat_RGBA16Float; break;
    case DOE_COMPILER_FORMAT_R32FLOAT: *out = WGPUTextureFormat_R32Float; break;
    case DOE_COMPILER_FORMAT_RGBA8UNORM: *out = WGPUTextureFormat_RGBA8Unorm; break;
    case DOE_COMPILER_FORMAT_RGBA8SNORM: *out = WGPUTextureFormat_RGBA8Snorm; break;
    case DOE_COMPILER_FORMAT_RGBA8SINT: *out = WGPUTextureFormat_RGBA8Sint; break;
    case DOE_COMPILER_FORMAT_RGBA8UINT: *out = WGPUTextureFormat_RGBA8Uint; break;
    case DOE_COMPILER_FORMAT_RGBA16SINT: *out = WGPUTextureFormat_RGBA16Sint; break;
    case DOE_COMPILER_FORMAT_RGBA16UINT: *out = WGPUTextureFormat_RGBA16Uint; break;
    case DOE_COMPILER_FORMAT_RG32FLOAT: *out = WGPUTextureFormat_RG32Float; break;
    case DOE_COMPILER_FORMAT_RGBA32SINT: *out = WGPUTextureFormat_RGBA32Sint; break;
    case DOE_COMPILER_FORMAT_R32SINT: *out = WGPUTextureFormat_R32Sint; break;
    case DOE_COMPILER_FORMAT_RG32SINT: *out = WGPUTextureFormat_RG32Sint; break;
    case DOE_COMPILER_FORMAT_RGBA32UINT: *out = WGPUTextureFormat_RGBA32Uint; break;
    case DOE_COMPILER_FORMAT_R32UINT: *out = WGPUTextureFormat_R32Uint; break;
    case DOE_COMPILER_FORMAT_RG32UINT: *out = WGPUTextureFormat_RG32Uint; break;
    default: return false;
    }
    return true;
}

bool native_compare_operation(WGPUCompareFunction value, uint32_t fallback, uint32_t *out)
{
    switch (value) {
    case WGPUCompareFunction_Never: *out = 0; return true;
    case WGPUCompareFunction_Less: *out = 1; return true;
    case WGPUCompareFunction_Equal: *out = 2; return true;
    case WGPUCompareFunction_LessEqual: *out = 3; return true;
    case WGPUCompareFunction_Greater: *out = 4; return true;
    case WGPUCompareFunction_NotEqual: *out = 5; return true;
    case WGPUCompareFunction_GreaterEqual: *out = 6; return true;
    case WGPUCompareFunction_Undefined: *out = fallback; return true;
    case WGPUCompareFunction_Always: *out = 7; return true;
    default: return false;
    }
}

bool native_texture_aspect(WGPUTextureAspect aspect, doe_vk_texture_aspect *out)
{
    switch (aspect) {
    case WGPUTextureAspect_All: *out = DOE_VK_ASPECT_ALL; return true;
    case WGPUTextureAspect_DepthOnly: *out = DOE_VK_ASPECT_DEPTH; return true;
    case WGPUTextureAspect_StencilOnly: *out = DOE_VK_ASPECT_STENCIL; return true;
    case WGPUTextureAspect_Undefined:
    case WGPUTextureAspect_Force32: return false;
    }
    return false;
}
