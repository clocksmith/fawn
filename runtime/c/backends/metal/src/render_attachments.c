/* SPDX-License-Identifier: MIT */
#include "render_internal.h"
#include <math.h>

static bool same_subresource(const doe_mtl_texture_view *a, const doe_mtl_texture_view *b) {
    return a != NULL && b != NULL && a->texture == b->texture &&
        a->desc.base_mip == b->desc.base_mip && a->desc.base_layer == b->desc.base_layer;
}
static bool combined(const doe_mtl_texture_view *view) {
    return view != NULL && doe_mtl_format_aspects(view->desc.format) ==
        (DOE_MTL_ASPECT_DEPTH | DOE_MTL_ASPECT_STENCIL);
}
static doe_mtl_status view_check(doe_mtl_commands *commands, doe_mtl_texture_view *view,
    uint8_t aspect, uint32_t *width, uint32_t *height) {
    doe_mtl_texture *texture = view->texture;
    doe_mtl_format_info format;
    uint32_t w, h;
    doe_mtl_status status = doe_mtl_texture_preflight(commands, texture, DOE_MTL_TEXTURE_RENDER_TARGET);
    if (status != DOE_MTL_OK) return status;
    if (view->desc.mip_count != 1 || view->desc.layer_count != 1 ||
        texture->desc.type == DOE_MTL_TEXTURE_3D || texture->desc.type == DOE_MTL_TEXTURE_1D ||
        texture->desc.type == DOE_MTL_TEXTURE_1D_ARRAY) return DOE_MTL_UNSUPPORTED;
    if (view->desc.red != DOE_MTL_SWIZZLE_RED || view->desc.green != DOE_MTL_SWIZZLE_GREEN ||
        view->desc.blue != DOE_MTL_SWIZZLE_BLUE || view->desc.alpha != DOE_MTL_SWIZZLE_ALPHA)
        return DOE_MTL_UNSUPPORTED;
    status = doe_mtl_texture_format_info(view->desc.format, &format);
    if (status != DOE_MTL_OK || format.compressed ||
        view->desc.format == DOE_MTL_X24_STENCIL8 || view->desc.format == DOE_MTL_X32_STENCIL8 ||
        (doe_mtl_format_aspects(view->desc.format) & aspect) == 0) return DOE_MTL_UNSUPPORTED;
    w = doe_mtl_mip_extent(texture->desc.width, view->desc.base_mip);
    h = doe_mtl_mip_extent(texture->desc.height, view->desc.base_mip);
    if (*width != 0 && (*width != w || *height != h)) return DOE_MTL_RANGE;
    *width = w;
    *height = h;
    return DOE_MTL_OK;
}
static doe_mtl_status attachment_check(doe_mtl_commands *commands,
    const doe_mtl_render_attachment *attachment, uint8_t aspect,
    doe_mtl_native_render_attachment *native, doe_mtl_native_render_pass *pass) {
    doe_mtl_texture_view *view = attachment->view;
    doe_mtl_texture_view *resolve = attachment->resolve_view;
    doe_mtl_status status;
    if (view == NULL) return resolve == NULL ? DOE_MTL_OK : DOE_MTL_INVALID_ARGUMENT;
    status = view_check(commands, view, aspect, &pass->width, &pass->height);
    if (status != DOE_MTL_OK) return status;
    if (pass->samples != 0 && pass->samples != view->texture->desc.sample_count) return DOE_MTL_INVALID_ARGUMENT;
    pass->samples = view->texture->desc.sample_count;
    if ((unsigned)attachment->load > DOE_MTL_CLEAR || (unsigned)attachment->store > DOE_MTL_STORE_RESOLVE)
        return DOE_MTL_INVALID_ARGUMENT;
    if (attachment->load == DOE_MTL_CLEAR &&
        ((aspect == DOE_MTL_ASPECT_COLOR && (!isfinite(attachment->clear_color.red) || !isfinite(attachment->clear_color.green) ||
          !isfinite(attachment->clear_color.blue) || !isfinite(attachment->clear_color.alpha))) ||
         (aspect == DOE_MTL_ASPECT_DEPTH && (!isfinite(attachment->clear_depth) || attachment->clear_depth < 0 || attachment->clear_depth > 1)) ||
         (aspect == DOE_MTL_ASPECT_STENCIL && attachment->clear_stencil > 255))) return DOE_MTL_RANGE;
    if (attachment->load == DOE_MTL_LOAD && !doe_mtl_texture_aspect_initialized(commands, view, aspect))
        return DOE_MTL_UNINITIALIZED;
    if ((attachment->store >= DOE_MTL_RESOLVE) != (resolve != NULL)) return DOE_MTL_INVALID_ARGUMENT;
    if (attachment->store == DOE_MTL_STORE_RESOLVE && !commands->context->caps.store_and_resolve)
        return DOE_MTL_UNSUPPORTED;
    if (resolve != NULL) {
        if (pass->samples <= 1 || resolve->texture->desc.sample_count != 1 || resolve->desc.format != view->desc.format)
            return DOE_MTL_INVALID_ARGUMENT;
        status = view_check(commands, resolve, aspect, &pass->width, &pass->height);
        if (status != DOE_MTL_OK) return status;
        if ((aspect == DOE_MTL_ASPECT_COLOR && attachment->resolve_filter != 0) ||
            (aspect == DOE_MTL_ASPECT_DEPTH && attachment->resolve_filter > 2) ||
            (aspect == DOE_MTL_ASPECT_STENCIL && attachment->resolve_filter > 1)) return DOE_MTL_INVALID_ARGUMENT;
        if ((aspect == DOE_MTL_ASPECT_DEPTH && !commands->context->caps.depth_resolve) ||
            (aspect == DOE_MTL_ASPECT_STENCIL && !commands->context->caps.stencil_resolve))
            return DOE_MTL_UNSUPPORTED;
        native->resolve = resolve->native;
        native->resolve_filter = attachment->resolve_filter;
    } else if (attachment->resolve_filter != 0) return DOE_MTL_INVALID_ARGUMENT;
    native->texture = view->native;
    native->load = attachment->load;
    native->store = attachment->store;
    native->color = attachment->clear_color;
    native->depth = attachment->clear_depth;
    native->stencil = attachment->clear_stencil;
    return DOE_MTL_OK;
}
static doe_mtl_status alias_check(const doe_mtl_commands *commands, const doe_mtl_render_pass_desc *pass,
    doe_mtl_native_render_pass *native) {
    const doe_mtl_texture_view *source[10] = {0}, *resolve[10] = {0};
    uint32_t i, j;
    for (i = 0; i < pass->color_count; ++i) {
        source[i] = pass->colors[i].view;
        resolve[i] = pass->colors[i].resolve_view;
    }
    source[8] = pass->depth.view;
    source[9] = pass->stencil.view;
    resolve[8] = pass->depth.resolve_view;
    resolve[9] = pass->stencil.resolve_view;
    /* Combined formats require both aspects explicitly represented, avoiding
     * implicit driver preservation assumptions about the omitted aspect. */
    if ((combined(source[8]) || combined(source[9])) &&
        (!same_subresource(source[8], source[9]) || source[8]->desc.format != source[9]->desc.format))
        return DOE_MTL_UNSUPPORTED;
    if ((combined(resolve[8]) || combined(resolve[9])) &&
        (!same_subresource(resolve[8], resolve[9]) || resolve[8]->desc.format != resolve[9]->desc.format))
        return DOE_MTL_UNSUPPORTED;
    if (source[8] != NULL && source[9] != NULL && !commands->context->caps.separate_depth_stencil &&
        !same_subresource(source[8], source[9])) return DOE_MTL_UNSUPPORTED;
    for (i = 0; i < 10; ++i) {
        for (j = 0; j < 10; ++j)
            if (same_subresource(source[i], resolve[j])) return DOE_MTL_INVALID_ARGUMENT;
        for (j = 0; j < i; ++j) {
            if (same_subresource(source[i], source[j]) && !(i == 9 && j == 8 && combined(source[i])))
                return DOE_MTL_INVALID_ARGUMENT;
            if (same_subresource(resolve[i], resolve[j]) && !(i == 9 && j == 8 && combined(resolve[i])))
                return DOE_MTL_INVALID_ARGUMENT;
        }
    }
    if (pass->stencil.resolve_filter == 1 &&
        (!same_subresource(source[8], source[9]) || !same_subresource(resolve[8], resolve[9])))
        return DOE_MTL_INVALID_ARGUMENT;
    /* Equivalent parent-backed views must present one native combined texture
     * identity to Metal while both wrapper leases remain in the command owner. */
    if (same_subresource(source[8], source[9])) native->stencil.texture = native->depth.texture;
    if (same_subresource(resolve[8], resolve[9])) native->stencil.resolve = native->depth.resolve;
    return DOE_MTL_OK;
}
doe_mtl_status doe_mtl_render_pass_preflight(doe_mtl_commands *commands,
    const doe_mtl_render_pass_desc *pass, doe_mtl_native_render_pass *native) {
    uint32_t i;
    doe_mtl_status status;
    if (pass == NULL || pass->color_count > DOE_MTL_COLOR_ATTACHMENTS) return DOE_MTL_INVALID_ARGUMENT;
    for (i = 0; i < pass->color_count; ++i) {
        status = attachment_check(commands, &pass->colors[i], DOE_MTL_ASPECT_COLOR, &native->colors[i], native);
        if (status != DOE_MTL_OK) return status;
    }
    status = attachment_check(commands, &pass->depth, DOE_MTL_ASPECT_DEPTH, &native->depth, native);
    if (status == DOE_MTL_OK)
        status = attachment_check(commands, &pass->stencil, DOE_MTL_ASPECT_STENCIL, &native->stencil, native);
    if (status == DOE_MTL_OK) status = alias_check(commands, pass, native);
    if (status != DOE_MTL_OK) return status;
    if (native->width == 0) return DOE_MTL_INVALID_ARGUMENT;
    if (pass->visibility_buffer != NULL) {
        status = doe_mtl_buffer_preflight(commands, pass->visibility_buffer, true);
        if (status != DOE_MTL_OK) return status;
        if (commands->context->caps.max_visibility_offset == 0) return DOE_MTL_UNSUPPORTED;
        native->visibility_buffer = pass->visibility_buffer->native;
    }
    native->color_count = pass->color_count;
    return DOE_MTL_OK;
}
static doe_mtl_status attachment_retain(doe_mtl_commands *commands, const doe_mtl_render_attachment *attachment) {
    doe_mtl_status status = DOE_MTL_OK;
    if (attachment->view != NULL) status = doe_mtl_use_view(commands, attachment->view);
    if (status == DOE_MTL_OK && attachment->resolve_view != NULL) status = doe_mtl_use_view(commands, attachment->resolve_view);
    return status;
}
doe_mtl_status doe_mtl_render_pass_retain(doe_mtl_commands *commands, const doe_mtl_render_pass_desc *pass) {
    doe_mtl_status status = DOE_MTL_OK;
    uint32_t i;
    for (i = 0; i < pass->color_count && status == DOE_MTL_OK; ++i)
        status = attachment_retain(commands, &pass->colors[i]);
    if (status == DOE_MTL_OK) status = attachment_retain(commands, &pass->depth);
    if (status == DOE_MTL_OK) status = attachment_retain(commands, &pass->stencil);
    if (status == DOE_MTL_OK && pass->visibility_buffer != NULL) {
        doe_mtl_buffer_use *use;
        status = doe_mtl_use_buffer(commands, pass->visibility_buffer, &use);
    }
    return status;
}
static void attachment_publish(doe_mtl_commands *commands,
    const doe_mtl_render_attachment *attachment, uint8_t aspect) {
    const bool initialized = attachment->load != DOE_MTL_LOAD_DISCARD;
    const bool stored = attachment->store == DOE_MTL_STORE || attachment->store == DOE_MTL_STORE_RESOLVE;
    doe_mtl_texture_aspect_publish(commands, attachment->view, aspect, initialized && stored);
    doe_mtl_texture_aspect_publish(commands, attachment->resolve_view, aspect, initialized);
}
void doe_mtl_render_pass_publish(doe_mtl_commands *commands, const doe_mtl_render_pass_desc *pass) {
    uint32_t i;
    for (i = 0; i < pass->color_count; ++i) attachment_publish(commands, &pass->colors[i], DOE_MTL_ASPECT_COLOR);
    attachment_publish(commands, &pass->depth, DOE_MTL_ASPECT_DEPTH);
    attachment_publish(commands, &pass->stencil, DOE_MTL_ASPECT_STENCIL);
}
