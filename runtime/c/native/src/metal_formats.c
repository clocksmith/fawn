/* SPDX-License-Identifier: MIT */
#include "metal_frontend.h"

bool native_metal_format(uint32_t features, WGPUTextureFormat format, doe_mtl_format *out)
{
    doe_vk_texture_format admitted;
    /* Preserve the common public feature/format admission, then cross the named
     * enum domains explicitly. No Vulkan object/capability is synthesized. */
    if (!native_texture_format(features, format, &admitted)) return false;
    switch (format) {
    case WGPUTextureFormat_Depth16Unorm: *out = DOE_MTL_DEPTH16_UNORM; return true;
    case WGPUTextureFormat_Depth32Float: *out = DOE_MTL_DEPTH32_FLOAT; return true;
    case WGPUTextureFormat_Stencil8: *out = DOE_MTL_STENCIL8; return true;
    case WGPUTextureFormat_Depth32FloatStencil8: *out = DOE_MTL_DEPTH32_STENCIL8; return true;
    case WGPUTextureFormat_R8Unorm: *out = DOE_MTL_R8_UNORM; return true;
    case WGPUTextureFormat_RG8Unorm: *out = DOE_MTL_RG8_UNORM; return true;
    case WGPUTextureFormat_RGBA8Unorm: *out = DOE_MTL_RGBA8_UNORM; return true;
    case WGPUTextureFormat_RGBA8UnormSrgb: *out = DOE_MTL_RGBA8_SRGB; return true;
    case WGPUTextureFormat_RGBA8Snorm: *out = DOE_MTL_RGBA8_SNORM; return true;
    case WGPUTextureFormat_RGBA8Uint: *out = DOE_MTL_RGBA8_UINT; return true;
    case WGPUTextureFormat_RGBA8Sint: *out = DOE_MTL_RGBA8_SINT; return true;
    case WGPUTextureFormat_RGBA16Uint: *out = DOE_MTL_RGBA16_UINT; return true;
    case WGPUTextureFormat_RGBA16Sint: *out = DOE_MTL_RGBA16_SINT; return true;
    case WGPUTextureFormat_BGRA8Unorm: *out = DOE_MTL_BGRA8_UNORM; return true;
    case WGPUTextureFormat_BGRA8UnormSrgb: *out = DOE_MTL_BGRA8_SRGB; return true;
    case WGPUTextureFormat_R16Float: *out = DOE_MTL_R16_FLOAT; return true;
    case WGPUTextureFormat_RG16Float: *out = DOE_MTL_RG16_FLOAT; return true;
    case WGPUTextureFormat_RGBA16Float: *out = DOE_MTL_RGBA16_FLOAT; return true;
    case WGPUTextureFormat_R8Snorm: *out = DOE_MTL_R8_SNORM; return true;
    case WGPUTextureFormat_R8Uint: *out = DOE_MTL_R8_UINT; return true;
    case WGPUTextureFormat_R8Sint: *out = DOE_MTL_R8_SINT; return true;
    case WGPUTextureFormat_RG8Snorm: *out = DOE_MTL_RG8_SNORM; return true;
    case WGPUTextureFormat_RG8Uint: *out = DOE_MTL_RG8_UINT; return true;
    case WGPUTextureFormat_RG8Sint: *out = DOE_MTL_RG8_SINT; return true;
    case WGPUTextureFormat_R16Uint: *out = DOE_MTL_R16_UINT; return true;
    case WGPUTextureFormat_R16Sint: *out = DOE_MTL_R16_SINT; return true;
    case WGPUTextureFormat_RG16Uint: *out = DOE_MTL_RG16_UINT; return true;
    case WGPUTextureFormat_RG16Sint: *out = DOE_MTL_RG16_SINT; return true;
    case WGPUTextureFormat_RGB10A2Unorm: *out = DOE_MTL_RGB10A2_UNORM; return true;
    case WGPUTextureFormat_RGB10A2Uint: *out = DOE_MTL_RGB10A2_UINT; return true;
    case WGPUTextureFormat_RGB9E5Ufloat: *out = DOE_MTL_RGB9E5_FLOAT; return true;
    case WGPUTextureFormat_RG11B10Ufloat: *out = DOE_MTL_RG11B10_FLOAT; return true;
    case WGPUTextureFormat_R32Uint: *out = DOE_MTL_R32_UINT; return true;
    case WGPUTextureFormat_R32Sint: *out = DOE_MTL_R32_SINT; return true;
    case WGPUTextureFormat_R32Float: *out = DOE_MTL_R32_FLOAT; return true;
    case WGPUTextureFormat_RG32Uint: *out = DOE_MTL_RG32_UINT; return true;
    case WGPUTextureFormat_RG32Sint: *out = DOE_MTL_RG32_SINT; return true;
    case WGPUTextureFormat_RG32Float: *out = DOE_MTL_RG32_FLOAT; return true;
    case WGPUTextureFormat_RGBA32Uint: *out = DOE_MTL_RGBA32_UINT; return true;
    case WGPUTextureFormat_RGBA32Sint: *out = DOE_MTL_RGBA32_SINT; return true;
    case WGPUTextureFormat_RGBA32Float: *out = DOE_MTL_RGBA32_FLOAT; return true;
    case WGPUTextureFormat_Depth24Plus: *out = DOE_MTL_DEPTH32_FLOAT; return true;
    case WGPUTextureFormat_Depth24PlusStencil8: *out = DOE_MTL_DEPTH32_STENCIL8; return true;
    case WGPUTextureFormat_BC1RGBAUnorm: *out = DOE_MTL_BC1_RGBA; return true;
    case WGPUTextureFormat_BC1RGBAUnormSrgb: *out = DOE_MTL_BC1_RGBA_SRGB; return true;
    case WGPUTextureFormat_BC2RGBAUnorm: *out = DOE_MTL_BC2_RGBA; return true;
    case WGPUTextureFormat_BC2RGBAUnormSrgb: *out = DOE_MTL_BC2_RGBA_SRGB; return true;
    case WGPUTextureFormat_BC3RGBAUnorm: *out = DOE_MTL_BC3_RGBA; return true;
    case WGPUTextureFormat_BC3RGBAUnormSrgb: *out = DOE_MTL_BC3_RGBA_SRGB; return true;
    case WGPUTextureFormat_BC4RUnorm: *out = DOE_MTL_BC4_R_UNORM; return true;
    case WGPUTextureFormat_BC4RSnorm: *out = DOE_MTL_BC4_R_SNORM; return true;
    case WGPUTextureFormat_BC5RGUnorm: *out = DOE_MTL_BC5_RG_UNORM; return true;
    case WGPUTextureFormat_BC5RGSnorm: *out = DOE_MTL_BC5_RG_SNORM; return true;
    case WGPUTextureFormat_BC6HRGBUfloat: *out = DOE_MTL_BC6H_RGB_UFLOAT; return true;
    case WGPUTextureFormat_BC6HRGBFloat: *out = DOE_MTL_BC6H_RGB_FLOAT; return true;
    case WGPUTextureFormat_BC7RGBAUnorm: *out = DOE_MTL_BC7_RGBA; return true;
    case WGPUTextureFormat_BC7RGBAUnormSrgb: *out = DOE_MTL_BC7_RGBA_SRGB; return true;
    case WGPUTextureFormat_ETC2RGB8Unorm: *out = DOE_MTL_ETC2_RGB8_UNORM; return true;
    case WGPUTextureFormat_ETC2RGB8UnormSrgb: *out = DOE_MTL_ETC2_RGB8_SRGB; return true;
    case WGPUTextureFormat_ETC2RGB8A1Unorm: *out = DOE_MTL_ETC2_RGB8A1_UNORM; return true;
    case WGPUTextureFormat_ETC2RGB8A1UnormSrgb: *out = DOE_MTL_ETC2_RGB8A1_SRGB; return true;
    case WGPUTextureFormat_ETC2RGBA8Unorm: *out = DOE_MTL_ETC2_RGBA8_UNORM; return true;
    case WGPUTextureFormat_ETC2RGBA8UnormSrgb: *out = DOE_MTL_ETC2_RGBA8_SRGB; return true;
    case WGPUTextureFormat_EACR11Unorm: *out = DOE_MTL_EAC_R11_UNORM; return true;
    case WGPUTextureFormat_EACR11Snorm: *out = DOE_MTL_EAC_R11_SNORM; return true;
    case WGPUTextureFormat_EACRG11Unorm: *out = DOE_MTL_EAC_RG11_UNORM; return true;
    case WGPUTextureFormat_EACRG11Snorm: *out = DOE_MTL_EAC_RG11_SNORM; return true;
    case WGPUTextureFormat_ASTC4x4Unorm: *out = DOE_MTL_ASTC_4x4_UNORM; return true;
    case WGPUTextureFormat_ASTC4x4UnormSrgb: *out = DOE_MTL_ASTC_4x4_SRGB; return true;
    case WGPUTextureFormat_ASTC5x4Unorm: *out = DOE_MTL_ASTC_5x4_UNORM; return true;
    case WGPUTextureFormat_ASTC5x4UnormSrgb: *out = DOE_MTL_ASTC_5x4_SRGB; return true;
    case WGPUTextureFormat_ASTC5x5Unorm: *out = DOE_MTL_ASTC_5x5_UNORM; return true;
    case WGPUTextureFormat_ASTC5x5UnormSrgb: *out = DOE_MTL_ASTC_5x5_SRGB; return true;
    case WGPUTextureFormat_ASTC6x5Unorm: *out = DOE_MTL_ASTC_6x5_UNORM; return true;
    case WGPUTextureFormat_ASTC6x5UnormSrgb: *out = DOE_MTL_ASTC_6x5_SRGB; return true;
    case WGPUTextureFormat_ASTC6x6Unorm: *out = DOE_MTL_ASTC_6x6_UNORM; return true;
    case WGPUTextureFormat_ASTC6x6UnormSrgb: *out = DOE_MTL_ASTC_6x6_SRGB; return true;
    case WGPUTextureFormat_ASTC8x5Unorm: *out = DOE_MTL_ASTC_8x5_UNORM; return true;
    case WGPUTextureFormat_ASTC8x5UnormSrgb: *out = DOE_MTL_ASTC_8x5_SRGB; return true;
    case WGPUTextureFormat_ASTC8x6Unorm: *out = DOE_MTL_ASTC_8x6_UNORM; return true;
    case WGPUTextureFormat_ASTC8x6UnormSrgb: *out = DOE_MTL_ASTC_8x6_SRGB; return true;
    case WGPUTextureFormat_ASTC8x8Unorm: *out = DOE_MTL_ASTC_8x8_UNORM; return true;
    case WGPUTextureFormat_ASTC8x8UnormSrgb: *out = DOE_MTL_ASTC_8x8_SRGB; return true;
    case WGPUTextureFormat_ASTC10x5Unorm: *out = DOE_MTL_ASTC_10x5_UNORM; return true;
    case WGPUTextureFormat_ASTC10x5UnormSrgb: *out = DOE_MTL_ASTC_10x5_SRGB; return true;
    case WGPUTextureFormat_ASTC10x6Unorm: *out = DOE_MTL_ASTC_10x6_UNORM; return true;
    case WGPUTextureFormat_ASTC10x6UnormSrgb: *out = DOE_MTL_ASTC_10x6_SRGB; return true;
    case WGPUTextureFormat_ASTC10x8Unorm: *out = DOE_MTL_ASTC_10x8_UNORM; return true;
    case WGPUTextureFormat_ASTC10x8UnormSrgb: *out = DOE_MTL_ASTC_10x8_SRGB; return true;
    case WGPUTextureFormat_ASTC10x10Unorm: *out = DOE_MTL_ASTC_10x10_UNORM; return true;
    case WGPUTextureFormat_ASTC10x10UnormSrgb: *out = DOE_MTL_ASTC_10x10_SRGB; return true;
    case WGPUTextureFormat_ASTC12x10Unorm: *out = DOE_MTL_ASTC_12x10_UNORM; return true;
    case WGPUTextureFormat_ASTC12x10UnormSrgb: *out = DOE_MTL_ASTC_12x10_SRGB; return true;
    case WGPUTextureFormat_ASTC12x12Unorm: *out = DOE_MTL_ASTC_12x12_UNORM; return true;
    case WGPUTextureFormat_ASTC12x12UnormSrgb: *out = DOE_MTL_ASTC_12x12_SRGB; return true;
    default: return false;
    }
}
