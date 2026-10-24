/* SPDX-License-Identifier: MIT */
#include "d3d12_binding_internal.h"

static bool compare(WGPUCompareFunction value,doe_dx_sampler_compare *out)
{
    switch (value) {
    case WGPUCompareFunction_Undefined: *out=DOE_DX_SAMPLER_NO_COMPARE; return true;
    case WGPUCompareFunction_Never: *out=DOE_DX_SAMPLER_NEVER; return true;
    case WGPUCompareFunction_Less: *out=DOE_DX_SAMPLER_LESS; return true;
    case WGPUCompareFunction_Equal: *out=DOE_DX_SAMPLER_EQUAL; return true;
    case WGPUCompareFunction_LessEqual: *out=DOE_DX_SAMPLER_LESS_EQUAL; return true;
    case WGPUCompareFunction_Greater: *out=DOE_DX_SAMPLER_GREATER; return true;
    case WGPUCompareFunction_NotEqual: *out=DOE_DX_SAMPLER_NOT_EQUAL; return true;
    case WGPUCompareFunction_GreaterEqual: *out=DOE_DX_SAMPLER_GREATER_EQUAL; return true;
    case WGPUCompareFunction_Always: *out=DOE_DX_SAMPLER_ALWAYS; return true;
    default: return false;
    }
}
static bool address(WGPUAddressMode value,doe_dx_address *out)
{
    switch (value) {
    case WGPUAddressMode_ClampToEdge: *out=DOE_DX_CLAMP; return true;
    case WGPUAddressMode_Repeat: *out=DOE_DX_REPEAT; return true;
    case WGPUAddressMode_MirrorRepeat: *out=DOE_DX_MIRROR; return true;
    default: return false;
    }
}
bool native_d3d12_sampler_descriptor(const WGPUSamplerDescriptor *normalized,doe_dx_sampler_desc *out)
{
    if (!normalized || !out ||
        (normalized->minFilter != WGPUFilterMode_Nearest && normalized->minFilter != WGPUFilterMode_Linear) ||
        (normalized->magFilter != WGPUFilterMode_Nearest && normalized->magFilter != WGPUFilterMode_Linear) ||
        (normalized->mipmapFilter != WGPUMipmapFilterMode_Nearest && normalized->mipmapFilter != WGPUMipmapFilterMode_Linear))
        return false;
    doe_dx_sampler_desc result = {.min_filter=normalized->minFilter == WGPUFilterMode_Linear ? DOE_DX_LINEAR : DOE_DX_NEAREST,
        .mag_filter=normalized->magFilter == WGPUFilterMode_Linear ? DOE_DX_LINEAR : DOE_DX_NEAREST,
        .mip_filter=normalized->mipmapFilter == WGPUMipmapFilterMode_Linear ? DOE_DX_LINEAR : DOE_DX_NEAREST,
        .min_lod=normalized->lodMinClamp,.max_lod=normalized->lodMaxClamp,.max_anisotropy=normalized->maxAnisotropy};
    if (!address(normalized->addressModeU,&result.address_u) || !address(normalized->addressModeV,&result.address_v) ||
        !address(normalized->addressModeW,&result.address_w) || !compare(normalized->compare,&result.compare)) return false;
    *out = result;
    return true;
}

