/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"

doe_mtl_status doe_mtl_texture_format_info(doe_mtl_format format, doe_mtl_format_info *out) {
    doe_mtl_format_info info = {1, 1, 0, false, false, false, false};
    if (out == NULL) return DOE_MTL_INVALID_ARGUMENT;
    switch (format) {
    case DOE_MTL_DEPTH24_STENCIL8: case DOE_MTL_DEPTH32_STENCIL8:
        info.depth = true;
        info.stencil = true;
        break;
    case DOE_MTL_X24_STENCIL8: case DOE_MTL_X32_STENCIL8:
        info.stencil = true;
        break;
    case DOE_MTL_DEPTH16_UNORM:
        info.block_bytes = 2;
        info.depth = true;
        break;
    case DOE_MTL_DEPTH32_FLOAT:
        info.block_bytes = 4;
        info.depth = true;
        break;
    case DOE_MTL_STENCIL8:
        info.block_bytes = 1;
        info.stencil = true;
        break;
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
    case DOE_MTL_ETC2_RGB8_UNORM:
        *out = (doe_mtl_format_info){4,4,8,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ETC2_RGB8_SRGB:
        *out = (doe_mtl_format_info){4,4,8,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ETC2_RGB8A1_UNORM:
        *out = (doe_mtl_format_info){4,4,8,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ETC2_RGB8A1_SRGB:
        *out = (doe_mtl_format_info){4,4,8,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ETC2_RGBA8_UNORM:
        *out = (doe_mtl_format_info){4,4,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ETC2_RGBA8_SRGB:
        *out = (doe_mtl_format_info){4,4,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_EAC_R11_UNORM:
        *out = (doe_mtl_format_info){4,4,8,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_EAC_R11_SNORM:
        *out = (doe_mtl_format_info){4,4,8,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_EAC_RG11_UNORM:
        *out = (doe_mtl_format_info){4,4,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_EAC_RG11_SNORM:
        *out = (doe_mtl_format_info){4,4,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_4x4_UNORM:
        *out = (doe_mtl_format_info){4,4,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_4x4_SRGB:
        *out = (doe_mtl_format_info){4,4,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_5x4_UNORM:
        *out = (doe_mtl_format_info){5,4,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_5x4_SRGB:
        *out = (doe_mtl_format_info){5,4,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_5x5_UNORM:
        *out = (doe_mtl_format_info){5,5,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_5x5_SRGB:
        *out = (doe_mtl_format_info){5,5,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_6x5_UNORM:
        *out = (doe_mtl_format_info){6,5,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_6x5_SRGB:
        *out = (doe_mtl_format_info){6,5,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_6x6_UNORM:
        *out = (doe_mtl_format_info){6,6,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_6x6_SRGB:
        *out = (doe_mtl_format_info){6,6,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_8x5_UNORM:
        *out = (doe_mtl_format_info){8,5,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_8x5_SRGB:
        *out = (doe_mtl_format_info){8,5,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_8x6_UNORM:
        *out = (doe_mtl_format_info){8,6,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_8x6_SRGB:
        *out = (doe_mtl_format_info){8,6,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_8x8_UNORM:
        *out = (doe_mtl_format_info){8,8,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_8x8_SRGB:
        *out = (doe_mtl_format_info){8,8,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_10x5_UNORM:
        *out = (doe_mtl_format_info){10,5,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_10x5_SRGB:
        *out = (doe_mtl_format_info){10,5,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_10x6_UNORM:
        *out = (doe_mtl_format_info){10,6,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_10x6_SRGB:
        *out = (doe_mtl_format_info){10,6,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_10x8_UNORM:
        *out = (doe_mtl_format_info){10,8,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_10x8_SRGB:
        *out = (doe_mtl_format_info){10,8,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_10x10_UNORM:
        *out = (doe_mtl_format_info){10,10,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_10x10_SRGB:
        *out = (doe_mtl_format_info){10,10,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_12x10_UNORM:
        *out = (doe_mtl_format_info){12,10,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_12x10_SRGB:
        *out = (doe_mtl_format_info){12,10,16,true,true,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_12x12_UNORM:
        *out = (doe_mtl_format_info){12,12,16,true,false,false,false};
        return DOE_MTL_OK;
    case DOE_MTL_ASTC_12x12_SRGB:
        *out = (doe_mtl_format_info){12,12,16,true,true,false,false};
        return DOE_MTL_OK;
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
    if ((a == DOE_MTL_DEPTH24_STENCIL8 && b == DOE_MTL_X24_STENCIL8) ||
        (a == DOE_MTL_DEPTH32_STENCIL8 && b == DOE_MTL_X32_STENCIL8)) return true;
    if (a > b) {
        doe_mtl_format temporary = a;
        a = b;
        b = temporary;
    }
    if ((a == DOE_MTL_ETC2_RGB8_UNORM && b == DOE_MTL_ETC2_RGB8_SRGB) ||
        (b == DOE_MTL_ETC2_RGB8_UNORM && a == DOE_MTL_ETC2_RGB8_SRGB)) return true;
    if ((a == DOE_MTL_ETC2_RGB8A1_UNORM && b == DOE_MTL_ETC2_RGB8A1_SRGB) ||
        (b == DOE_MTL_ETC2_RGB8A1_UNORM && a == DOE_MTL_ETC2_RGB8A1_SRGB)) return true;
    if ((a == DOE_MTL_ETC2_RGBA8_UNORM && b == DOE_MTL_ETC2_RGBA8_SRGB) ||
        (b == DOE_MTL_ETC2_RGBA8_UNORM && a == DOE_MTL_ETC2_RGBA8_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_4x4_UNORM && b == DOE_MTL_ASTC_4x4_SRGB) ||
        (b == DOE_MTL_ASTC_4x4_UNORM && a == DOE_MTL_ASTC_4x4_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_5x4_UNORM && b == DOE_MTL_ASTC_5x4_SRGB) ||
        (b == DOE_MTL_ASTC_5x4_UNORM && a == DOE_MTL_ASTC_5x4_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_5x5_UNORM && b == DOE_MTL_ASTC_5x5_SRGB) ||
        (b == DOE_MTL_ASTC_5x5_UNORM && a == DOE_MTL_ASTC_5x5_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_6x5_UNORM && b == DOE_MTL_ASTC_6x5_SRGB) ||
        (b == DOE_MTL_ASTC_6x5_UNORM && a == DOE_MTL_ASTC_6x5_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_6x6_UNORM && b == DOE_MTL_ASTC_6x6_SRGB) ||
        (b == DOE_MTL_ASTC_6x6_UNORM && a == DOE_MTL_ASTC_6x6_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_8x5_UNORM && b == DOE_MTL_ASTC_8x5_SRGB) ||
        (b == DOE_MTL_ASTC_8x5_UNORM && a == DOE_MTL_ASTC_8x5_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_8x6_UNORM && b == DOE_MTL_ASTC_8x6_SRGB) ||
        (b == DOE_MTL_ASTC_8x6_UNORM && a == DOE_MTL_ASTC_8x6_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_8x8_UNORM && b == DOE_MTL_ASTC_8x8_SRGB) ||
        (b == DOE_MTL_ASTC_8x8_UNORM && a == DOE_MTL_ASTC_8x8_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_10x5_UNORM && b == DOE_MTL_ASTC_10x5_SRGB) ||
        (b == DOE_MTL_ASTC_10x5_UNORM && a == DOE_MTL_ASTC_10x5_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_10x6_UNORM && b == DOE_MTL_ASTC_10x6_SRGB) ||
        (b == DOE_MTL_ASTC_10x6_UNORM && a == DOE_MTL_ASTC_10x6_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_10x8_UNORM && b == DOE_MTL_ASTC_10x8_SRGB) ||
        (b == DOE_MTL_ASTC_10x8_UNORM && a == DOE_MTL_ASTC_10x8_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_10x10_UNORM && b == DOE_MTL_ASTC_10x10_SRGB) ||
        (b == DOE_MTL_ASTC_10x10_UNORM && a == DOE_MTL_ASTC_10x10_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_12x10_UNORM && b == DOE_MTL_ASTC_12x10_SRGB) ||
        (b == DOE_MTL_ASTC_12x10_UNORM && a == DOE_MTL_ASTC_12x10_SRGB)) return true;
    if ((a == DOE_MTL_ASTC_12x12_UNORM && b == DOE_MTL_ASTC_12x12_SRGB) ||
        (b == DOE_MTL_ASTC_12x12_UNORM && a == DOE_MTL_ASTC_12x12_SRGB)) return true;
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

uint8_t doe_mtl_format_aspects(doe_mtl_format format) {
    doe_mtl_format_info info;
    if (doe_mtl_texture_format_info(format, &info) != DOE_MTL_OK) return 0;
    if (info.depth || info.stencil)
        return (uint8_t)((info.depth ? DOE_MTL_ASPECT_DEPTH : 0) | (info.stencil ? DOE_MTL_ASPECT_STENCIL : 0));
    return DOE_MTL_ASPECT_COLOR;
}
bool doe_mtl_samples_supported(const doe_mtl_caps *caps, uint32_t samples) {
    uint32_t bit = 0;
    if (samples == 0 || samples > 16 || (samples & (samples - 1)) != 0) return false;
    while ((1u << bit) < samples) ++bit;
    return (caps->sample_count_mask & (1u << bit)) != 0;
}
