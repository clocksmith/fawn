/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"

doe_mtl_status doe_mtl_texture_format_info(doe_mtl_format format, doe_mtl_format_info *out) {
    doe_mtl_format_info info = {1, 1, 0, false, false};
    if (out == NULL) return DOE_MTL_INVALID_ARGUMENT;
    switch (format) {
    case DOE_MTL_R8_UNORM: case DOE_MTL_R8_SNORM: case DOE_MTL_R8_UINT: case DOE_MTL_R8_SINT:
        info.block_bytes = 1;
        break;
    case DOE_MTL_R16_UNORM: case DOE_MTL_R16_SNORM: case DOE_MTL_R16_UINT:
    case DOE_MTL_R16_SINT: case DOE_MTL_R16_FLOAT:
    case DOE_MTL_RG8_UNORM: case DOE_MTL_RG8_SNORM: case DOE_MTL_RG8_UINT: case DOE_MTL_RG8_SINT:
        info.block_bytes = 2;
        break;
    case DOE_MTL_R32_UINT: case DOE_MTL_R32_SINT: case DOE_MTL_R32_FLOAT:
    case DOE_MTL_RG16_UNORM: case DOE_MTL_RG16_SNORM: case DOE_MTL_RG16_UINT:
    case DOE_MTL_RG16_SINT: case DOE_MTL_RG16_FLOAT:
    case DOE_MTL_RGBA8_UNORM: case DOE_MTL_RGBA8_SNORM: case DOE_MTL_RGBA8_UINT:
    case DOE_MTL_RGBA8_SINT: case DOE_MTL_BGRA8_UNORM:
    case DOE_MTL_RGB10A2_UNORM: case DOE_MTL_RGB10A2_UINT:
    case DOE_MTL_RG11B10_FLOAT: case DOE_MTL_RGB9E5_FLOAT:
        info.block_bytes = 4;
        break;
    case DOE_MTL_RGBA8_SRGB: case DOE_MTL_BGRA8_SRGB:
        info.block_bytes = 4;
        info.srgb = true;
        break;
    case DOE_MTL_RG32_UINT: case DOE_MTL_RG32_SINT: case DOE_MTL_RG32_FLOAT:
    case DOE_MTL_RGBA16_UNORM: case DOE_MTL_RGBA16_SNORM: case DOE_MTL_RGBA16_UINT:
    case DOE_MTL_RGBA16_SINT: case DOE_MTL_RGBA16_FLOAT:
        info.block_bytes = 8;
        break;
    case DOE_MTL_RGBA32_UINT: case DOE_MTL_RGBA32_SINT: case DOE_MTL_RGBA32_FLOAT:
        info.block_bytes = 16;
        break;
    case DOE_MTL_BC1_RGBA_SRGB: case DOE_MTL_BC2_RGBA_SRGB:
    case DOE_MTL_BC3_RGBA_SRGB: case DOE_MTL_BC7_RGBA_SRGB:
        info.srgb = true;
        info.compressed = true;
        break;
    case DOE_MTL_BC1_RGBA: case DOE_MTL_BC2_RGBA: case DOE_MTL_BC3_RGBA:
    case DOE_MTL_BC4_R_UNORM: case DOE_MTL_BC4_R_SNORM: case DOE_MTL_BC5_RG_UNORM:
    case DOE_MTL_BC5_RG_SNORM: case DOE_MTL_BC6H_RGB_FLOAT: case DOE_MTL_BC6H_RGB_UFLOAT:
    case DOE_MTL_BC7_RGBA:
        info.compressed = true;
        break;
    default: return DOE_MTL_UNSUPPORTED;
    }
    if (info.compressed) {
        info.block_width = 4;
        info.block_height = 4;
        info.block_bytes = format == DOE_MTL_BC1_RGBA || format == DOE_MTL_BC1_RGBA_SRGB ||
            format == DOE_MTL_BC4_R_UNORM || format == DOE_MTL_BC4_R_SNORM ? 8 : 16;
    }
    *out = info;
    return DOE_MTL_OK;
}

bool doe_mtl_format_compatible(doe_mtl_format a, doe_mtl_format b) {
    if (a == b) return true;
    if (a > b) {
        doe_mtl_format temporary = a;
        a = b;
        b = temporary;
    }
    return (a == DOE_MTL_RGBA8_UNORM && b == DOE_MTL_RGBA8_SRGB) ||
        (a == DOE_MTL_BGRA8_UNORM && b == DOE_MTL_BGRA8_SRGB) ||
        (a == DOE_MTL_BC1_RGBA && b == DOE_MTL_BC1_RGBA_SRGB) ||
        (a == DOE_MTL_BC2_RGBA && b == DOE_MTL_BC2_RGBA_SRGB) ||
        (a == DOE_MTL_BC3_RGBA && b == DOE_MTL_BC3_RGBA_SRGB) ||
        (a == DOE_MTL_BC7_RGBA && b == DOE_MTL_BC7_RGBA_SRGB);
}

uint32_t doe_mtl_mip_extent(uint32_t dimension, uint32_t mip) {
    const uint32_t value = dimension >> mip;
    return value == 0 ? 1 : value;
}

uint32_t doe_mtl_texture_layers(const doe_mtl_texture_desc *desc) {
    return desc->type == DOE_MTL_TEXTURE_3D ? 1 : desc->depth_or_layers;
}
