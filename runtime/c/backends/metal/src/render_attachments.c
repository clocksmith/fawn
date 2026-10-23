/* SPDX-License-Identifier: MIT */
#include "render_internal.h"
#include <math.h>

static doe_mtl_status attachment_check(doe_mtl_commands *commands,
    const doe_mtl_render_attachment *attachment, unsigned aspect,
    doe_mtl_native_render_attachment *native, uint32_t *width, uint32_t *height) {
    doe_mtl_texture_view *view = attachment->view;
    doe_mtl_texture *texture;
    doe_mtl_texture_region region;
    doe_mtl_format_info format;
    doe_mtl_status status;
    uint32_t w, h;
    if (view == NULL) return DOE_MTL_OK;
    texture = view->texture;
    status = doe_mtl_texture_preflight(commands, texture, DOE_MTL_TEXTURE_RENDER_TARGET);
    if (status != DOE_MTL_OK) return status;
    if (view->desc.mip_count != 1 || view->desc.layer_count != 1 ||
        texture->desc.type == DOE_MTL_TEXTURE_3D || texture->desc.type == DOE_MTL_TEXTURE_1D ||
        texture->desc.type == DOE_MTL_TEXTURE_1D_ARRAY) return DOE_MTL_UNSUPPORTED;
    if (view->desc.red != DOE_MTL_SWIZZLE_RED || view->desc.green != DOE_MTL_SWIZZLE_GREEN ||
        view->desc.blue != DOE_MTL_SWIZZLE_BLUE || view->desc.alpha != DOE_MTL_SWIZZLE_ALPHA)
        return DOE_MTL_UNSUPPORTED;
    status = doe_mtl_texture_format_info(view->desc.format, &format);
    if (status != DOE_MTL_OK || format.compressed ||
        (aspect == 0 && (format.depth || format.stencil)) ||
        (aspect == 1 && !format.depth) || (aspect == 2 && !format.stencil)) return DOE_MTL_UNSUPPORTED;
    if ((unsigned)attachment->load > DOE_MTL_CLEAR || (unsigned)attachment->store > DOE_MTL_STORE)
        return DOE_MTL_INVALID_ARGUMENT;
    if (attachment->load == DOE_MTL_CLEAR &&
        ((aspect == 0 && (!isfinite(attachment->clear_color.red) || !isfinite(attachment->clear_color.green) ||
          !isfinite(attachment->clear_color.blue) || !isfinite(attachment->clear_color.alpha))) ||
         (aspect == 1 && (!isfinite(attachment->clear_depth) || attachment->clear_depth < 0 || attachment->clear_depth > 1)) ||
         (aspect == 2 && attachment->clear_stencil > 255))) return DOE_MTL_RANGE;
    w = doe_mtl_mip_extent(texture->desc.width, view->desc.base_mip);
    h = doe_mtl_mip_extent(texture->desc.height, view->desc.base_mip);
    if (*width != 0 && (*width != w || *height != h)) return DOE_MTL_RANGE;
    *width = w;
    *height = h;
    region = (doe_mtl_texture_region){view->desc.base_mip, 0, 0, view->desc.base_layer, w, h, 1};
    if (attachment->load == DOE_MTL_LOAD && !doe_mtl_texture_region_initialized(commands, texture, region))
        return DOE_MTL_UNINITIALIZED;
    native->texture = view->native;
    native->load = attachment->load;
    native->store = attachment->store;
    native->color = attachment->clear_color;
    native->depth = attachment->clear_depth;
    native->stencil = attachment->clear_stencil;
    return DOE_MTL_OK;
}
static bool same_subresource(const doe_mtl_texture_view *a, const doe_mtl_texture_view *b) {
    return a != NULL && b != NULL && a->texture == b->texture &&
        a->desc.base_mip == b->desc.base_mip && a->desc.base_layer == b->desc.base_layer;
}
doe_mtl_status doe_mtl_render_pass_preflight(doe_mtl_commands *commands,
    const doe_mtl_render_pass_desc *pass, doe_mtl_native_render_pass *native) {
    uint32_t i, j;
    doe_mtl_status status;
    if (pass == NULL || pass->color_count > DOE_MTL_COLOR_ATTACHMENTS) return DOE_MTL_INVALID_ARGUMENT;
    for (i = 0; i < pass->color_count; ++i) {
        status = attachment_check(commands, &pass->colors[i], 0, &native->colors[i], &native->width, &native->height);
        if (status != DOE_MTL_OK) return status;
        for (j = 0; j < i; ++j)
            if (same_subresource(pass->colors[i].view, pass->colors[j].view)) return DOE_MTL_INVALID_ARGUMENT;
    }
    /* Initialization is per subresource, not per aspect. Combined depth/stencil
     * needs aspect-qualified obligations before it can be admitted. */
    if (same_subresource(pass->depth.view, pass->stencil.view)) return DOE_MTL_UNSUPPORTED;
    status = attachment_check(commands, &pass->depth, 1, &native->depth, &native->width, &native->height);
    if (status == DOE_MTL_OK)
        status = attachment_check(commands, &pass->stencil, 2, &native->stencil, &native->width, &native->height);
    if (status != DOE_MTL_OK) return status;
    if (native->width == 0) return DOE_MTL_INVALID_ARGUMENT;
    native->color_count = pass->color_count;
    return DOE_MTL_OK;
}
doe_mtl_status doe_mtl_render_pass_retain(doe_mtl_commands *commands, const doe_mtl_render_pass_desc *pass) {
    doe_mtl_status status = DOE_MTL_OK;
    uint32_t i;
    for (i = 0; i < pass->color_count && status == DOE_MTL_OK; ++i)
        if (pass->colors[i].view != NULL) status = doe_mtl_use_view(commands, pass->colors[i].view);
    if (status == DOE_MTL_OK && pass->depth.view != NULL) status = doe_mtl_use_view(commands, pass->depth.view);
    if (status == DOE_MTL_OK && pass->stencil.view != NULL) status = doe_mtl_use_view(commands, pass->stencil.view);
    return status;
}
static void attachment_publish(doe_mtl_commands *commands, const doe_mtl_render_attachment *attachment) {
    doe_mtl_texture_view *view = attachment->view;
    doe_mtl_texture_use *use;
    size_t index;
    if (view == NULL) return;
    index = (size_t)view->desc.base_mip * doe_mtl_texture_layers(&view->texture->desc) + view->desc.base_layer;
    for (use = commands->textures; use != NULL; use = use->next) {
        if (use->texture != view->texture) continue;
        use->initialized[index] = attachment->store == DOE_MTL_STORE && attachment->load != DOE_MTL_LOAD_DISCARD;
        return;
    }
}
void doe_mtl_render_pass_publish(doe_mtl_commands *commands, const doe_mtl_render_pass_desc *pass) {
    uint32_t i;
    for (i = 0; i < pass->color_count; ++i) attachment_publish(commands, &pass->colors[i]);
    attachment_publish(commands, &pass->depth);
    attachment_publish(commands, &pass->stencil);
}
