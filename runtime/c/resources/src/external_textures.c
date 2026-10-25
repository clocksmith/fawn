/* SPDX-License-Identifier: MIT */
#include "internal.h"

static bool plane_available(const doe_resource_view *view)
{
    return view && resource_texture_available(view->texture) &&
        view->desc.dimension == WGPUTextureViewDimension_2D && view->desc.mip_count == 1 &&
        view->texture->desc.samples == 1 && (view->desc.usage & WGPUTextureUsage_TextureBinding);
}

bool resource_external_available(const doe_resource_external_texture *texture)
{
    if (!texture || texture->expired || !resource_alive(&texture->base)) return false;
    for (size_t i = 0; i < texture->plane_count; i++)
        if (!plane_available(texture->planes[i])) return false;
    return true;
}

doe_resource_status doe_resource_external_texture_create(const doe_resource_config *config,
    doe_resource_view *plane0, doe_resource_view *plane1, doe_resource_external_texture **out)
{
    if (!resource_config_valid(config) || !out || !plane0) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity) || !plane_available(plane0) ||
        (plane1 && !plane_available(plane1))) return DOE_RESOURCE_UNAVAILABLE;
    if (plane0->base.config.device.identity != config->device.identity ||
        (plane1 && plane1->base.config.device.identity != config->device.identity)) return DOE_RESOURCE_DEVICE_MISMATCH;
    doe_resource_external_texture *texture = resource_allocate(config,
        sizeof(*texture), _Alignof(doe_resource_external_texture));
    if (!texture) return DOE_RESOURCE_OUT_OF_MEMORY;
    texture->planes[0] = plane0;
    texture->planes[1] = plane1;
    texture->plane_count = plane1 ? 2 : 1;
    texture->expired = false;
    doe_resource_view_retain(plane0);
    doe_resource_view_retain(plane1);
    resource_init(&texture->base, config, (doe_resource_native){0});
    *out = texture;
    return DOE_RESOURCE_OK;
}

void doe_resource_external_texture_retain(doe_resource_external_texture *texture)
{
    if (texture) resource_retain(&texture->base);
}

void doe_resource_external_texture_release(doe_resource_external_texture *texture)
{
    if (!texture || !resource_last_reference(&texture->base)) return;
    for (size_t i = 0; i < texture->plane_count; i++) doe_resource_view_release(texture->planes[i]);
    resource_finalize(&texture->base, sizeof(*texture), _Alignof(doe_resource_external_texture));
}

void doe_resource_external_texture_expire(doe_resource_external_texture *texture)
{
    if (texture) texture->expired = true;
}

doe_resource_status doe_resource_external_texture_refresh(doe_resource_external_texture *texture)
{
    if (!texture) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!resource_alive(&texture->base)) return DOE_RESOURCE_UNAVAILABLE;
    for (size_t i = 0; i < texture->plane_count; i++)
        if (!plane_available(texture->planes[i])) return DOE_RESOURCE_UNAVAILABLE;
    texture->expired = false;
    return DOE_RESOURCE_OK;
}

doe_resource_view *doe_resource_external_texture_plane(const doe_resource_external_texture *texture,
    size_t plane)
{
    return texture && plane < texture->plane_count ? texture->planes[plane] : NULL;
}

bool doe_resource_external_texture_available(const doe_resource_external_texture *texture)
{
    return resource_external_available(texture);
}
