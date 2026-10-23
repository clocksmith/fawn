/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <math.h>
#include <string.h>

static uint32_t maximum(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

doe_resource_status doe_resource_texture_validate(const doe_resource_texture_desc *desc,
    const doe_resource_texture_limits *limits)
{
    if (!desc || !limits || (desc->view_format_count && !desc->view_formats))
        return DOE_RESOURCE_INVALID_ARGUMENT;
    if (desc->view_format_count > SIZE_MAX / sizeof(*desc->view_formats)) return DOE_RESOURCE_OVERFLOW;
    const uint64_t valid_usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding |
        WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TransientAttachment;
    if (!desc->usage || (desc->usage & ~valid_usage)) return DOE_RESOURCE_USAGE;
    if (desc->usage & WGPUTextureUsage_TransientAttachment) {
        if (desc->usage != (WGPUTextureUsage_TransientAttachment | WGPUTextureUsage_RenderAttachment))
            return DOE_RESOURCE_USAGE;
        if (desc->view_format_count || desc->dimension != WGPUTextureDimension_2D ||
            desc->mip_levels != 1 || desc->depth_or_layers != 1) return DOE_RESOURCE_LAYOUT;
    }
    uint32_t bytes, block_width, block_height;
    doe_resource_status status = resource_format_block(desc->format, &bytes, &block_width, &block_height);
    if (status != DOE_RESOURCE_OK) return status;
    if (!desc->width || !desc->height || !desc->depth_or_layers || !desc->mip_levels)
        return DOE_RESOURCE_RANGE;
    if (desc->samples != 1 && desc->samples != 4) return DOE_RESOURCE_UNSUPPORTED;
    uint32_t extent = desc->width;
    switch (desc->dimension) {
    case WGPUTextureDimension_1D:
        if (desc->width > limits->dimension_1d || desc->height != 1 || desc->depth_or_layers != 1)
            return DOE_RESOURCE_RANGE;
        if (block_width != 1 || block_height != 1) return DOE_RESOURCE_UNSUPPORTED;
        if (desc->mip_levels != 1) return DOE_RESOURCE_RANGE;
        if (desc->usage & WGPUTextureUsage_RenderAttachment) return DOE_RESOURCE_USAGE;
        break;
    case WGPUTextureDimension_2D:
        if (desc->width > limits->dimension_2d || desc->height > limits->dimension_2d ||
            desc->depth_or_layers > limits->array_layers) return DOE_RESOURCE_RANGE;
        extent = maximum(extent, desc->height);
        break;
    case WGPUTextureDimension_3D:
        if (desc->width > limits->dimension_3d || desc->height > limits->dimension_3d ||
            desc->depth_or_layers > limits->dimension_3d) return DOE_RESOURCE_RANGE;
        extent = maximum(maximum(extent, desc->height), desc->depth_or_layers);
        break;
    default: return DOE_RESOURCE_UNSUPPORTED;
    }
    uint32_t mips = 0;
    while (extent) {
        mips++;
        extent >>= 1;
    }
    if (desc->mip_levels > mips) return DOE_RESOURCE_RANGE;
    if (desc->samples > 1 && (desc->dimension != WGPUTextureDimension_2D ||
        desc->depth_or_layers != 1 || desc->mip_levels != 1 ||
        (desc->usage & (WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | WGPUTextureUsage_StorageBinding))))
        return DOE_RESOURCE_LAYOUT;
    if (desc->samples > 1 && !(desc->usage & WGPUTextureUsage_RenderAttachment))
        return DOE_RESOURCE_USAGE;
    if (resource_format_depth(desc->format) && desc->dimension != WGPUTextureDimension_2D)
        return DOE_RESOURCE_LAYOUT;
    for (size_t i = 0; i < desc->view_format_count; i++)
        if (!resource_format_compatible(desc->format, desc->view_formats[i])) return DOE_RESOURCE_LAYOUT;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_texture_create(const doe_resource_config *config,
    const doe_resource_texture_desc *desc, const doe_resource_texture_limits *limits,
    doe_resource_native native, doe_resource_texture **out)
{
    if (!out || !resource_config_valid(config) || !native.handle || !native.release)
        return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity)) return DOE_RESOURCE_UNAVAILABLE;
    doe_resource_status status = doe_resource_texture_validate(desc, limits);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_texture *texture = resource_allocate(config, sizeof(*texture), _Alignof(doe_resource_texture));
    if (!texture) return DOE_RESOURCE_OUT_OF_MEMORY;
    memset(texture, 0, sizeof(*texture));
    texture->desc = *desc;
    texture->desc.view_format_count = 0;
    texture->desc.view_formats = texture->view_formats;
    for (size_t i = 0; i < desc->view_format_count; i++) {
        bool exists = false;
        for (size_t j = 0; j < texture->desc.view_format_count; j++)
            if (texture->view_formats[j] == desc->view_formats[i]) exists = true;
        if (!exists) texture->view_formats[texture->desc.view_format_count++] = desc->view_formats[i];
    }
    resource_init(&texture->base, config, native);
    *out = texture;
    return DOE_RESOURCE_OK;
}

void doe_resource_texture_retain(doe_resource_texture *texture)
{
    if (texture) resource_retain(&texture->base);
}

void doe_resource_texture_release(doe_resource_texture *texture)
{
    if (texture && resource_last_reference(&texture->base))
        resource_finalize(&texture->base, sizeof(*texture), _Alignof(doe_resource_texture));
}

void doe_resource_texture_destroy(doe_resource_texture *texture)
{
    if (texture) texture->destroyed = true;
}

const doe_resource_texture_desc *doe_resource_texture_description(const doe_resource_texture *texture)
{
    return texture ? &texture->desc : NULL;
}

void *doe_resource_texture_native(const doe_resource_texture *texture)
{
    return texture ? texture->base.native.handle : NULL;
}

static doe_resource_status resolve_count(uint32_t total, uint32_t base,
    uint32_t requested, uint32_t *out)
{
    if (base >= total) return DOE_RESOURCE_RANGE;
    uint32_t count = !requested || requested == UINT32_MAX ? total - base : requested;
    if (!count || count > total - base) return DOE_RESOURCE_RANGE;
    *out = count;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_view_validate(const doe_resource_texture *texture,
    const doe_resource_view_desc *input, doe_resource_view_desc *out)
{
    if (!texture || !input || !out) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!resource_texture_available(texture)) return DOE_RESOURCE_UNAVAILABLE;
    const doe_resource_texture_desc *tex = &texture->desc;
    doe_resource_view_desc view = *input;
    if (!view.aspect) view.aspect = WGPUTextureAspect_All;
    if (!resource_format_aspect(tex->format, view.aspect)) return DOE_RESOURCE_ASPECT;
    WGPUTextureFormat aspect_format;
    doe_resource_status status = doe_resource_format_aspect_format(tex->format, view.aspect, &aspect_format);
    if (status != DOE_RESOURCE_OK) return status;
    if (!view.format) view.format = aspect_format;
    bool format_allowed = view.format == aspect_format;
    if (view.aspect == WGPUTextureAspect_All)
        for (size_t i = 0; i < tex->view_format_count; i++)
            if (tex->view_formats[i] == view.format) format_allowed = true;
    if (!format_allowed) return DOE_RESOURCE_LAYOUT;
    if (!view.usage) view.usage = tex->usage;
    if (view.usage & ~tex->usage) return DOE_RESOURCE_USAGE;
    if ((tex->usage & WGPUTextureUsage_TransientAttachment) && view.usage != tex->usage)
        return DOE_RESOURCE_USAGE;
    if (!view.dimension) {
        if (tex->dimension == WGPUTextureDimension_1D) view.dimension = WGPUTextureViewDimension_1D;
        else if (tex->dimension == WGPUTextureDimension_3D) view.dimension = WGPUTextureViewDimension_3D;
        else view.dimension = tex->depth_or_layers > 1 ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
    }
    status = resolve_count(tex->mip_levels, view.base_mip, view.mip_count, &view.mip_count);
    if (status != DOE_RESOURCE_OK) return status;
    uint32_t layers = tex->dimension == WGPUTextureDimension_3D ? 1 : tex->depth_or_layers;
    if (!view.layer_count || view.layer_count == UINT32_MAX) {
        if (view.dimension == WGPUTextureViewDimension_1D ||
            view.dimension == WGPUTextureViewDimension_2D ||
            view.dimension == WGPUTextureViewDimension_3D) view.layer_count = 1;
        else if (view.dimension == WGPUTextureViewDimension_Cube) view.layer_count = 6;
    }
    status = resolve_count(layers, view.base_layer, view.layer_count, &view.layer_count);
    if (status != DOE_RESOURCE_OK) return status;
    switch (view.dimension) {
    case WGPUTextureViewDimension_1D:
        if (tex->dimension != WGPUTextureDimension_1D || view.layer_count != 1) return DOE_RESOURCE_LAYOUT;
        break;
    case WGPUTextureViewDimension_2D:
        if (tex->dimension != WGPUTextureDimension_2D || view.layer_count != 1) return DOE_RESOURCE_LAYOUT;
        break;
    case WGPUTextureViewDimension_2DArray:
        if (tex->dimension != WGPUTextureDimension_2D) return DOE_RESOURCE_LAYOUT;
        break;
    case WGPUTextureViewDimension_Cube:
    case WGPUTextureViewDimension_CubeArray:
        if (tex->dimension != WGPUTextureDimension_2D || tex->width != tex->height ||
            view.layer_count % 6 || (view.dimension == WGPUTextureViewDimension_Cube && view.layer_count != 6))
            return DOE_RESOURCE_LAYOUT;
        break;
    case WGPUTextureViewDimension_3D:
        if (tex->dimension != WGPUTextureDimension_3D) return DOE_RESOURCE_LAYOUT;
        break;
    default: return DOE_RESOURCE_UNSUPPORTED;
    }
    if (tex->samples > 1 && view.dimension != WGPUTextureViewDimension_2D) return DOE_RESOURCE_LAYOUT;
    *out = view;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_view_create(doe_resource_texture *texture,
    const doe_resource_view_desc *desc, doe_resource_native native, doe_resource_view **out)
{
    if (!texture || !desc || !out || !native.handle || !native.release) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_view_desc resolved;
    doe_resource_status status = doe_resource_view_validate(texture, desc, &resolved);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_view *view = resource_allocate(&texture->base.config, sizeof(*view), _Alignof(doe_resource_view));
    if (!view) return DOE_RESOURCE_OUT_OF_MEMORY;
    view->texture = texture;
    view->desc = resolved;
    doe_resource_texture_retain(texture);
    resource_init(&view->base, &texture->base.config, native);
    *out = view;
    return DOE_RESOURCE_OK;
}

void doe_resource_view_retain(doe_resource_view *view)
{
    if (view) resource_retain(&view->base);
}

void doe_resource_view_release(doe_resource_view *view)
{
    if (!view || !resource_last_reference(&view->base)) return;
    doe_resource_texture *texture = view->texture;
    resource_finalize(&view->base, sizeof(*view), _Alignof(doe_resource_view));
    doe_resource_texture_release(texture);
}

const doe_resource_view_desc *doe_resource_view_description(const doe_resource_view *view)
{
    return view ? &view->desc : NULL;
}

void *doe_resource_view_native(const doe_resource_view *view)
{
    return view ? view->base.native.handle : NULL;
}

doe_resource_texture *doe_resource_view_texture(const doe_resource_view *view)
{
    return view ? view->texture : NULL;
}

doe_resource_status doe_resource_sampler_validate(const WGPUSamplerDescriptor *desc,
    WGPUSamplerDescriptor *out)
{
    if (!desc || !out) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (desc->nextInChain) return DOE_RESOURCE_UNSUPPORTED;
    WGPUSamplerDescriptor resolved = *desc;
    resolved.label = (WGPUStringView){NULL, 0};
    if (!resolved.addressModeU) resolved.addressModeU = WGPUAddressMode_ClampToEdge;
    if (!resolved.addressModeV) resolved.addressModeV = WGPUAddressMode_ClampToEdge;
    if (!resolved.addressModeW) resolved.addressModeW = WGPUAddressMode_ClampToEdge;
    if (!resolved.minFilter) resolved.minFilter = WGPUFilterMode_Nearest;
    if (!resolved.magFilter) resolved.magFilter = WGPUFilterMode_Nearest;
    if (!resolved.mipmapFilter) resolved.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    if (resolved.addressModeU < WGPUAddressMode_ClampToEdge || resolved.addressModeU > WGPUAddressMode_MirrorRepeat ||
        resolved.addressModeV < WGPUAddressMode_ClampToEdge || resolved.addressModeV > WGPUAddressMode_MirrorRepeat ||
        resolved.addressModeW < WGPUAddressMode_ClampToEdge || resolved.addressModeW > WGPUAddressMode_MirrorRepeat ||
        resolved.minFilter < WGPUFilterMode_Nearest || resolved.minFilter > WGPUFilterMode_Linear ||
        resolved.magFilter < WGPUFilterMode_Nearest || resolved.magFilter > WGPUFilterMode_Linear ||
        resolved.mipmapFilter < WGPUMipmapFilterMode_Nearest || resolved.mipmapFilter > WGPUMipmapFilterMode_Linear ||
        resolved.compare > WGPUCompareFunction_Always) return DOE_RESOURCE_LAYOUT;
    if (!isfinite(resolved.lodMinClamp) || !isfinite(resolved.lodMaxClamp) ||
        resolved.lodMinClamp < 0 || resolved.lodMaxClamp < resolved.lodMinClamp ||
        !resolved.maxAnisotropy) return DOE_RESOURCE_RANGE;
    /* No upper bound: the used value is clamped to the device maximum.
     * https://gpuweb.github.io/gpuweb/#dom-gpusamplerdescriptor-maxanisotropy */
    if (resolved.maxAnisotropy > 1 && (resolved.minFilter != WGPUFilterMode_Linear ||
        resolved.magFilter != WGPUFilterMode_Linear || resolved.mipmapFilter != WGPUMipmapFilterMode_Linear))
        return DOE_RESOURCE_LAYOUT;
    *out = resolved;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_sampler_create(const doe_resource_config *config,
    const WGPUSamplerDescriptor *desc, doe_resource_native native, doe_resource_sampler **out)
{
    if (!resource_config_valid(config) || !desc || !out || !native.handle || !native.release)
        return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity)) return DOE_RESOURCE_UNAVAILABLE;
    WGPUSamplerDescriptor resolved;
    doe_resource_status status = doe_resource_sampler_validate(desc, &resolved);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_sampler *sampler = resource_allocate(config, sizeof(*sampler), _Alignof(doe_resource_sampler));
    if (!sampler) return DOE_RESOURCE_OUT_OF_MEMORY;
    sampler->desc = resolved;
    resource_init(&sampler->base, config, native);
    *out = sampler;
    return DOE_RESOURCE_OK;
}

void doe_resource_sampler_retain(doe_resource_sampler *sampler)
{
    if (sampler) resource_retain(&sampler->base);
}

void doe_resource_sampler_release(doe_resource_sampler *sampler)
{
    if (sampler && resource_last_reference(&sampler->base))
        resource_finalize(&sampler->base, sizeof(*sampler), _Alignof(doe_resource_sampler));
}

void *doe_resource_sampler_native(const doe_resource_sampler *sampler)
{
    return sampler ? sampler->base.native.handle : NULL;
}
