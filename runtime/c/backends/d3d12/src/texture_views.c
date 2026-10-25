/* SPDX-License-Identifier: MIT */
#include "internal.h"

bool doe_dx_format_compatible(doe_dx_format a,doe_dx_format b)
{
    if (a==b) return true;
#define PAIR(linear,srgb) if ((a==linear && b==srgb) || (a==srgb && b==linear)) return true
    PAIR(DOE_DX_RGBA8_UNORM,DOE_DX_RGBA8_UNORM_SRGB);
    PAIR(DOE_DX_BGRA8_UNORM,DOE_DX_BGRA8_UNORM_SRGB);
    PAIR(DOE_DX_BC1_UNORM,DOE_DX_BC1_UNORM_SRGB);
    PAIR(DOE_DX_BC2_UNORM,DOE_DX_BC2_UNORM_SRGB);
    PAIR(DOE_DX_BC3_UNORM,DOE_DX_BC3_UNORM_SRGB);
    PAIR(DOE_DX_BC7_UNORM,DOE_DX_BC7_UNORM_SRGB);
#undef PAIR
    return false;
}

bool doe_dx_texture_view_swizzled(const doe_dx_texture_view_desc *view)
{
    for (uint32_t i=0;i<4;++i)
        if (view->swizzle[i]!=DOE_DX_SWIZZLE_IDENTITY && view->swizzle[i]!=(doe_dx_swizzle)(DOE_DX_SWIZZLE_R+i)) return true;
    return false;
}

doe_dx_status doe_dx_texture_view_validate(const doe_dx_texture *texture,
    const doe_dx_texture_view_desc *input,doe_dx_texture_view_desc *out)
{
    if (!texture || !out) return DOE_DX_INVALID_ARGUMENT;
    const doe_dx_texture_shape *shape=&texture->shape;
    doe_dx_texture_view_desc view=input ? *input : (doe_dx_texture_view_desc){.format=texture->desc.format,
        .dimension=shape->dimension==1 ? 1u : shape->dimension==3 ? 6u : shape->depth_or_layers>1 ? 3u : 2u,
        .mip_count=shape->mip_levels,.layer_count=shape->dimension==3 ? 1u : shape->depth_or_layers};
    for (uint32_t i=0;i<4;++i)
        if ((unsigned)view.swizzle[i]>DOE_DX_SWIZZLE_A) return DOE_DX_INVALID_ARGUMENT;
    if (!view.mip_count || view.base_mip>=shape->mip_levels || view.mip_count>shape->mip_levels-view.base_mip ||
        !view.layer_count || view.aspect>2 || !doe_dx_format_compatible(view.format,texture->desc.format)) return DOE_DX_RANGE;
    uint32_t layers=shape->dimension==3 ? 1u : shape->depth_or_layers;
    if (view.base_layer>=layers || view.layer_count>layers-view.base_layer) return DOE_DX_RANGE;
    if ((view.aspect==1 && !doe_dx_format_depth(texture->desc.format)) ||
        (view.aspect==2 && !doe_dx_format_stencil(texture->desc.format))) return DOE_DX_INVALID_ARGUMENT;
    switch (view.dimension) {
    case 1: if (shape->dimension!=1 || view.layer_count!=1) return DOE_DX_INVALID_ARGUMENT; break;
    case 2: if (shape->dimension!=2 || view.layer_count!=1) return DOE_DX_INVALID_ARGUMENT; break;
    case 3: if (shape->dimension!=2) return DOE_DX_INVALID_ARGUMENT; break;
    case 4: case 5:
        if (shape->dimension!=2 || view.layer_count%6 || (view.dimension==4 && view.layer_count!=6) ||
            texture->desc.width!=texture->desc.height || texture->samples!=1) return DOE_DX_INVALID_ARGUMENT;
        break;
    case 6: if (shape->dimension!=3 || view.base_layer || view.layer_count!=1) return DOE_DX_INVALID_ARGUMENT; break;
    default: return DOE_DX_INVALID_ARGUMENT;
    }
    if (texture->samples>1 && (view.dimension!=2 || view.base_mip || view.mip_count!=1)) return DOE_DX_INVALID_ARGUMENT;
    *out=view;
    return DOE_DX_OK;
}
