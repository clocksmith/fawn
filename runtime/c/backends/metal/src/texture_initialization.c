/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"

static const uint8_t *initialization(const doe_mtl_commands *commands, const doe_mtl_texture *texture) {
    const doe_mtl_texture_use *use;
    for (use = commands->textures; use != NULL; use = use->next)
        if (use->texture == texture) return use->initialized;
    return texture->initialized;
}

bool doe_mtl_texture_region_aspects_initialized(const doe_mtl_commands *commands,
    const doe_mtl_texture *texture, doe_mtl_texture_region region, uint8_t aspects) {
    const uint8_t *initialized = initialization(commands, texture);
    const uint32_t layers = doe_mtl_texture_layers(&texture->desc);
    const uint32_t first = texture->desc.type == DOE_MTL_TEXTURE_3D ? 0 : region.z;
    const uint32_t count = texture->desc.type == DOE_MTL_TEXTURE_3D ? 1 : region.depth_or_layers;
    uint32_t layer;
    for (layer = first; layer < first + count; ++layer)
        if ((initialized[(size_t)region.mip * layers + layer] & aspects) != aspects) return false;
    return true;
}

void doe_mtl_texture_region_aspects_initialize(doe_mtl_texture_use *use,
    doe_mtl_texture_region region, uint8_t aspects) {
    const doe_mtl_texture *texture = use->texture;
    const uint32_t layers = doe_mtl_texture_layers(&texture->desc);
    const uint32_t first = texture->desc.type == DOE_MTL_TEXTURE_3D ? 0 : region.z;
    const uint32_t count = texture->desc.type == DOE_MTL_TEXTURE_3D ? 1 : region.depth_or_layers;
    uint32_t layer;
    for (layer = first; layer < first + count; ++layer)
        use->initialized[(size_t)region.mip * layers + layer] |= aspects;
}

bool doe_mtl_texture_region_initialized(const doe_mtl_commands *commands,
    const doe_mtl_texture *texture, doe_mtl_texture_region region) {
    return doe_mtl_texture_region_aspects_initialized(commands, texture, region,
        doe_mtl_format_aspects(texture->desc.format));
}
void doe_mtl_texture_region_initialize(doe_mtl_texture_use *use, doe_mtl_texture_region region) {
    doe_mtl_texture_region_aspects_initialize(use, region, doe_mtl_format_aspects(use->texture->desc.format));
}

bool doe_mtl_texture_aspect_initialized(const doe_mtl_commands *commands,
    const doe_mtl_texture_view *view, uint8_t aspects) {
    const uint8_t *initialized = initialization(commands, view->texture);
    const uint32_t layers = doe_mtl_texture_layers(&view->texture->desc);
    uint32_t mip, layer;
    for (mip = view->desc.base_mip; mip < view->desc.base_mip + view->desc.mip_count; ++mip)
        for (layer = view->desc.base_layer; layer < view->desc.base_layer + view->desc.layer_count; ++layer)
            if ((initialized[(size_t)mip * layers + layer] & aspects) != aspects) return false;
    return true;
}

/* Render preflight admits exactly one mip and one layer for this publisher. */
void doe_mtl_texture_aspect_publish(doe_mtl_commands *commands,
    const doe_mtl_texture_view *view, uint8_t aspects, bool initialized) {
    doe_mtl_texture_use *use;
    size_t index;
    if (view == NULL) return;
    index = (size_t)view->desc.base_mip * doe_mtl_texture_layers(&view->texture->desc) + view->desc.base_layer;
    for (use = commands->textures; use != NULL; use = use->next) {
        if (use->texture != view->texture) continue;
        if (initialized) use->initialized[index] |= aspects;
        else use->initialized[index] &= (uint8_t)~aspects;
        return;
    }
}
