/* SPDX-License-Identifier: MIT */
#include "render_internal.h"
#include <float.h>
#include <math.h>

bool native_vertex_format(WGPUVertexFormat format, doe_vk_vertex_format *native,
    uint32_t *scalar, uint32_t *bytes)
{
    static const struct vertex_format {
        WGPUVertexFormat public_format;
        doe_vk_vertex_format native_format;
        uint32_t scalar, bytes;
    } formats[] = {
        /* Normalized and float formats feed f32 shader inputs; uint/sint feed u32/i32. */
        {WGPUVertexFormat_Uint8,DOE_VK_VERTEX_UINT8,DOE_COMPILER_LEAF_U32,1},
        {WGPUVertexFormat_Sint8,DOE_VK_VERTEX_SINT8,DOE_COMPILER_LEAF_I32,1},
        {WGPUVertexFormat_Unorm8,DOE_VK_VERTEX_UNORM8,DOE_COMPILER_LEAF_F32,1},
        {WGPUVertexFormat_Snorm8,DOE_VK_VERTEX_SNORM8,DOE_COMPILER_LEAF_F32,1},
        {WGPUVertexFormat_Uint8x2,DOE_VK_VERTEX_UINT8X2,DOE_COMPILER_LEAF_U32,2},
        {WGPUVertexFormat_Sint8x2,DOE_VK_VERTEX_SINT8X2,DOE_COMPILER_LEAF_I32,2},
        {WGPUVertexFormat_Unorm8x2,DOE_VK_VERTEX_UNORM8X2,DOE_COMPILER_LEAF_F32,2},
        {WGPUVertexFormat_Snorm8x2,DOE_VK_VERTEX_SNORM8X2,DOE_COMPILER_LEAF_F32,2},
        {WGPUVertexFormat_Uint8x4,DOE_VK_VERTEX_UINT8X4,DOE_COMPILER_LEAF_U32,4},
        {WGPUVertexFormat_Sint8x4,DOE_VK_VERTEX_SINT8X4,DOE_COMPILER_LEAF_I32,4},
        {WGPUVertexFormat_Unorm8x4,DOE_VK_VERTEX_UNORM8X4,DOE_COMPILER_LEAF_F32,4},
        {WGPUVertexFormat_Snorm8x4,DOE_VK_VERTEX_SNORM8X4,DOE_COMPILER_LEAF_F32,4},
        {WGPUVertexFormat_Uint16,DOE_VK_VERTEX_UINT16,DOE_COMPILER_LEAF_U32,2},
        {WGPUVertexFormat_Sint16,DOE_VK_VERTEX_SINT16,DOE_COMPILER_LEAF_I32,2},
        {WGPUVertexFormat_Unorm16,DOE_VK_VERTEX_UNORM16,DOE_COMPILER_LEAF_F32,2},
        {WGPUVertexFormat_Snorm16,DOE_VK_VERTEX_SNORM16,DOE_COMPILER_LEAF_F32,2},
        {WGPUVertexFormat_Float16,DOE_VK_VERTEX_FLOAT16,DOE_COMPILER_LEAF_F32,2},
        {WGPUVertexFormat_Uint16x2,DOE_VK_VERTEX_UINT16X2,DOE_COMPILER_LEAF_U32,4},
        {WGPUVertexFormat_Sint16x2,DOE_VK_VERTEX_SINT16X2,DOE_COMPILER_LEAF_I32,4},
        {WGPUVertexFormat_Unorm16x2,DOE_VK_VERTEX_UNORM16X2,DOE_COMPILER_LEAF_F32,4},
        {WGPUVertexFormat_Snorm16x2,DOE_VK_VERTEX_SNORM16X2,DOE_COMPILER_LEAF_F32,4},
        {WGPUVertexFormat_Float16x2,DOE_VK_VERTEX_FLOAT16X2,DOE_COMPILER_LEAF_F32,4},
        {WGPUVertexFormat_Uint16x4,DOE_VK_VERTEX_UINT16X4,DOE_COMPILER_LEAF_U32,8},
        {WGPUVertexFormat_Sint16x4,DOE_VK_VERTEX_SINT16X4,DOE_COMPILER_LEAF_I32,8},
        {WGPUVertexFormat_Unorm16x4,DOE_VK_VERTEX_UNORM16X4,DOE_COMPILER_LEAF_F32,8},
        {WGPUVertexFormat_Snorm16x4,DOE_VK_VERTEX_SNORM16X4,DOE_COMPILER_LEAF_F32,8},
        {WGPUVertexFormat_Float16x4,DOE_VK_VERTEX_FLOAT16X4,DOE_COMPILER_LEAF_F32,8},
        {WGPUVertexFormat_Float32,DOE_VK_VERTEX_FLOAT32,DOE_COMPILER_LEAF_F32,4},
        {WGPUVertexFormat_Uint32,DOE_VK_VERTEX_UINT32,DOE_COMPILER_LEAF_U32,4},
        {WGPUVertexFormat_Sint32,DOE_VK_VERTEX_SINT32,DOE_COMPILER_LEAF_I32,4},
        {WGPUVertexFormat_Float32x2,DOE_VK_VERTEX_FLOAT32X2,DOE_COMPILER_LEAF_F32,8},
        {WGPUVertexFormat_Uint32x2,DOE_VK_VERTEX_UINT32X2,DOE_COMPILER_LEAF_U32,8},
        {WGPUVertexFormat_Sint32x2,DOE_VK_VERTEX_SINT32X2,DOE_COMPILER_LEAF_I32,8},
        {WGPUVertexFormat_Float32x3,DOE_VK_VERTEX_FLOAT32X3,DOE_COMPILER_LEAF_F32,12},
        {WGPUVertexFormat_Uint32x3,DOE_VK_VERTEX_UINT32X3,DOE_COMPILER_LEAF_U32,12},
        {WGPUVertexFormat_Sint32x3,DOE_VK_VERTEX_SINT32X3,DOE_COMPILER_LEAF_I32,12},
        {WGPUVertexFormat_Float32x4,DOE_VK_VERTEX_FLOAT32X4,DOE_COMPILER_LEAF_F32,16},
        {WGPUVertexFormat_Uint32x4,DOE_VK_VERTEX_UINT32X4,DOE_COMPILER_LEAF_U32,16},
        {WGPUVertexFormat_Sint32x4,DOE_VK_VERTEX_SINT32X4,DOE_COMPILER_LEAF_I32,16},
        {WGPUVertexFormat_Unorm10_10_10_2,DOE_VK_VERTEX_UNORM10_10_10_2,DOE_COMPILER_LEAF_F32,4},
        {WGPUVertexFormat_Unorm8x4BGRA,DOE_VK_VERTEX_UNORM8X4_BGRA,DOE_COMPILER_LEAF_F32,4}
    };
    for (size_t i = 0; i < sizeof(formats)/sizeof(formats[0]); ++i) if (formats[i].public_format == format) {
        *native = formats[i].native_format;
        *scalar = formats[i].scalar;
        *bytes = formats[i].bytes;
        return true;
    }
    return false;
}

bool native_color_scalar(WGPUTextureFormat format, uint32_t *scalar)
{
    WGPUTextureSampleType type;
    if (doe_resource_format_sample_type(format, WGPUTextureAspect_All, &type) != DOE_RESOURCE_OK) return false;
    switch (type) {
    case WGPUTextureSampleType_Float:
    case WGPUTextureSampleType_UnfilterableFloat: *scalar = DOE_COMPILER_LEAF_F32; return true;
    case WGPUTextureSampleType_Uint: *scalar = DOE_COMPILER_LEAF_U32; return true;
    case WGPUTextureSampleType_Sint: *scalar = DOE_COMPILER_LEAF_I32; return true;
    default: return false;
    }
}

/* WebGPU GPUColor conversion is to vec4 of the format's WGSL scalar, before
 * native format conversion. Integer EnforceRange truncates first; never cast an
 * out-of-range double. Float overflow midpoint is exactly representable in f64.
 * https://gpuweb.github.io/gpuweb/#abstract-opdef-to-a-texel-value-of-texture-format */
bool native_render_clear(WGPUTextureFormat format, const double values[4], doe_vk_clear_color *out)
{
    uint32_t scalar;
    if (!native_color_scalar(format, &scalar)) return false;
    doe_vk_clear_color converted;
    for (size_t i = 0; i < 4; ++i) {
        double value = values[i];
        if (!isfinite(value)) return false;
        if (scalar == DOE_COMPILER_LEAF_U32) {
            if (value <= -1.0 || value >= 4294967296.0) return false;
            converted.uint32[i] = (uint32_t)value;
        } else if (scalar == DOE_COMPILER_LEAF_I32) {
            if (value <= -2147483649.0 || value >= 2147483648.0) return false;
            converted.int32[i] = (int32_t)value;
        } else {
            const double overflow = 0x1.ffffffp127;
            if (value <= -overflow || value >= overflow) return false;
            if (value > FLT_MAX) value = FLT_MAX;
            if (value < -FLT_MAX) value = -FLT_MAX;
            converted.float32[i] = (float)value;
        }
    }
    *out = converted;
    return true;
}
