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
    default: return false;
    }
}
