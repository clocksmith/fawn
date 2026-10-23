/* SPDX-License-Identifier: MIT
 * Semantic names cross each enum domain; retained model numbers are not ABI. */
#include "internal.h"

bool native_texture_format(WGPUTextureFormat format, doe_vk_texture_format *out)
{
    switch (format) {
    case WGPUTextureFormat_R8Unorm: *out = DOE_VK_FORMAT_R8_UNORM; break;
    case WGPUTextureFormat_RG8Unorm: *out = DOE_VK_FORMAT_RG8_UNORM; break;
    case WGPUTextureFormat_RGBA8Unorm: *out = DOE_VK_FORMAT_RGBA8_UNORM; break;
    case WGPUTextureFormat_RGBA8UnormSrgb: *out = DOE_VK_FORMAT_RGBA8_SRGB; break;
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
    default: return false;
    }
    return true;
}

bool native_storage_format(uint32_t format, WGPUTextureFormat *out)
{
    switch (format) {
    case DOE_COMPILER_FORMAT_RGBA32FLOAT: *out = WGPUTextureFormat_RGBA32Float; break;
    case DOE_COMPILER_FORMAT_RGBA16FLOAT: *out = WGPUTextureFormat_RGBA16Float; break;
    case DOE_COMPILER_FORMAT_R32FLOAT: *out = WGPUTextureFormat_R32Float; break;
    case DOE_COMPILER_FORMAT_RGBA8UNORM: *out = WGPUTextureFormat_RGBA8Unorm; break;
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
