/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"

static bool is_one_dimensional(doe_mtl_texture_type type) {
    return type == DOE_MTL_TEXTURE_1D || type == DOE_MTL_TEXTURE_1D_ARRAY;
}

static doe_mtl_status texture_validate(const doe_mtl_context *context,
    const doe_mtl_texture_desc *desc, doe_mtl_format_info *format) {
    uint32_t max_dimension, max_extent, mips = 1;
    doe_mtl_status status;
    if (desc == NULL || desc->width == 0 || desc->height == 0 || desc->depth_or_layers == 0 ||
        desc->mip_levels == 0 || desc->mip_levels > 32 || desc->usage == 0 || (desc->usage & ~31u) != 0)
        return DOE_MTL_INVALID_ARGUMENT;
    status = doe_mtl_texture_format_info(desc->format, format);
    if (status != DOE_MTL_OK) return status;
    if (!doe_mtl_samples_supported(&context->caps, desc->sample_count)) return DOE_MTL_UNSUPPORTED;
    if (desc->format == DOE_MTL_X24_STENCIL8 || desc->format == DOE_MTL_X32_STENCIL8)
        return DOE_MTL_UNSUPPORTED;
    if (desc->format == DOE_MTL_DEPTH24_STENCIL8 && !context->caps.depth24_stencil8)
        return DOE_MTL_UNSUPPORTED;
    if (desc->format == DOE_MTL_STENCIL8 && !context->caps.separate_depth_stencil)
        return DOE_MTL_UNSUPPORTED;
    if (desc->sample_count > 1) {
        if ((desc->type != DOE_MTL_TEXTURE_2D_MS && desc->type != DOE_MTL_TEXTURE_2D_MS_ARRAY) ||
            desc->mip_levels != 1 || format->compressed ||
            (desc->usage & (DOE_MTL_TEXTURE_COPY_SRC | DOE_MTL_TEXTURE_COPY_DST | DOE_MTL_TEXTURE_STORAGE)) != 0)
            return DOE_MTL_UNSUPPORTED;
    } else if (desc->type == DOE_MTL_TEXTURE_2D_MS || desc->type == DOE_MTL_TEXTURE_2D_MS_ARRAY)
        return DOE_MTL_INVALID_ARGUMENT;
    if ((format->depth || format->stencil) &&
        ((desc->usage & (DOE_MTL_TEXTURE_STORAGE | DOE_MTL_TEXTURE_COPY_SRC | DOE_MTL_TEXTURE_COPY_DST)) != 0 ||
         (desc->type != DOE_MTL_TEXTURE_2D && desc->type != DOE_MTL_TEXTURE_2D_ARRAY &&
          desc->type != DOE_MTL_TEXTURE_2D_MS && desc->type != DOE_MTL_TEXTURE_2D_MS_ARRAY)))
        return DOE_MTL_UNSUPPORTED;
    if (format->compressed && (!context->caps.bc_texture_compression ||
        (desc->usage & (DOE_MTL_TEXTURE_STORAGE | DOE_MTL_TEXTURE_RENDER_TARGET)) != 0))
        return DOE_MTL_UNSUPPORTED;
    if (format->srgb && (desc->usage & DOE_MTL_TEXTURE_STORAGE) != 0) return DOE_MTL_UNSUPPORTED;
    switch (desc->type) {
    case DOE_MTL_TEXTURE_1D:
        if (desc->depth_or_layers != 1) return DOE_MTL_RANGE;
        /* fall through */
    case DOE_MTL_TEXTURE_1D_ARRAY:
        if (desc->height != 1 || format->compressed) return DOE_MTL_UNSUPPORTED;
        max_dimension = context->caps.max_texture_1d;
        break;
    case DOE_MTL_TEXTURE_2D_MS:
    case DOE_MTL_TEXTURE_2D:
        if (desc->depth_or_layers != 1) return DOE_MTL_RANGE;
        max_dimension = context->caps.max_texture_2d;
        break;
    case DOE_MTL_TEXTURE_CUBE:
        if (desc->depth_or_layers != 6) return DOE_MTL_RANGE;
        /* fall through */
    case DOE_MTL_TEXTURE_CUBE_ARRAY:
        if (desc->width != desc->height || desc->depth_or_layers % 6 != 0) return DOE_MTL_RANGE;
        /* fall through */
    case DOE_MTL_TEXTURE_2D_MS_ARRAY:
        if (desc->type == DOE_MTL_TEXTURE_2D_MS_ARRAY && !context->caps.multisample_array)
            return DOE_MTL_UNSUPPORTED;
        /* fall through */
    case DOE_MTL_TEXTURE_2D_ARRAY:
        max_dimension = context->caps.max_texture_2d;
        break;
    case DOE_MTL_TEXTURE_3D:
        if (format->compressed) return DOE_MTL_UNSUPPORTED;
        if (desc->depth_or_layers > context->caps.max_texture_3d) return DOE_MTL_RANGE;
        max_dimension = context->caps.max_texture_3d;
        break;
    default: return DOE_MTL_UNSUPPORTED;
    }
    if (max_dimension == 0) return DOE_MTL_UNSUPPORTED;
    if (desc->width > max_dimension || desc->height > max_dimension ||
        (desc->type != DOE_MTL_TEXTURE_3D && desc->depth_or_layers > context->caps.max_texture_layers))
        return DOE_MTL_RANGE;
    max_extent = desc->width > desc->height ? desc->width : desc->height;
    if (desc->type == DOE_MTL_TEXTURE_3D && desc->depth_or_layers > max_extent)
        max_extent = desc->depth_or_layers;
    while (max_extent > 1) {
        max_extent >>= 1;
        mips++;
    }
    if (desc->mip_levels > mips) return DOE_MTL_RANGE;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_texture_create(doe_mtl_context *context,
    const doe_mtl_texture_desc *desc, doe_mtl_texture **out, doe_mtl_error *error) {
    doe_mtl_texture *texture;
    doe_mtl_format_info format;
    doe_mtl_status status;
    uint32_t layers;
    if (context == NULL || out == NULL)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "texture create", error);
    if (context->native.texture == NULL)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "texture native boundary", error);
    status = texture_validate(context, desc, &format);
    if (status != DOE_MTL_OK) return doe_mtl_fail(status, "texture descriptor", error);
    layers = doe_mtl_texture_layers(desc);
    if (layers > SIZE_MAX / desc->mip_levels)
        return doe_mtl_fail(DOE_MTL_RANGE, "texture subresource count", error);
    texture = calloc(1, sizeof(*texture));
    if (texture == NULL) return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "texture allocation", error);
    texture->subresource_count = (size_t)layers * desc->mip_levels;
    texture->initialized = calloc(texture->subresource_count, sizeof(uint8_t));
    if (texture->initialized == NULL) {
        free(texture);
        return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "texture initialization state", error);
    }
    status = doe_mtl_context_retain(context);
    if (status != DOE_MTL_OK) {
        free(texture->initialized);
        free(texture);
        return doe_mtl_fail(status, "texture context lease", error);
    }
    texture->context = context;
    texture->references = 1;
    texture->desc = *desc;
    texture->format = format;
    status = context->native.texture(context->native.userdata, context->device, desc, &texture->native, error);
    if (status != DOE_MTL_OK) {
        doe_mtl_texture_release(texture);
        return status;
    }
    *out = texture;
    return doe_mtl_fail(DOE_MTL_OK, "texture create", error);
}

doe_mtl_status doe_mtl_texture_retain(doe_mtl_texture *texture) {
    if (texture == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (texture->references == UINT32_MAX) return DOE_MTL_RANGE;
    texture->references++;
    return DOE_MTL_OK;
}

void doe_mtl_texture_release(doe_mtl_texture *texture) {
    doe_mtl_context *context;
    if (texture == NULL || --texture->references != 0) return;
    context = texture->context;
    if (texture->native != NULL) context->native.release(context->native.userdata, texture->native);
    if (texture->drawable) context->native.release(context->native.userdata, texture->drawable);
    doe_mtl_surface_release(texture->surface);
    free(texture->initialized);
    free(texture);
    doe_mtl_context_release(context);
}

void *doe_mtl_texture_native(const doe_mtl_texture *texture) {
    return texture != NULL ? texture->native : NULL;
}

const doe_mtl_texture_desc *doe_mtl_texture_description(const doe_mtl_texture *texture) {
    return texture != NULL ? &texture->desc : NULL;
}

static doe_mtl_status view_validate(const doe_mtl_texture *texture,
    const doe_mtl_texture_view_desc *desc) {
    const doe_mtl_texture_desc *parent = &texture->desc;
    const uint32_t layers = doe_mtl_texture_layers(parent);
    bool compatible_type;
    if (desc == NULL || desc->mip_count == 0 || desc->layer_count == 0 ||
        desc->base_mip >= parent->mip_levels || desc->mip_count > parent->mip_levels - desc->base_mip ||
        desc->base_layer >= layers || desc->layer_count > layers - desc->base_layer)
        return DOE_MTL_RANGE;
    if ((unsigned)desc->red > DOE_MTL_SWIZZLE_ALPHA || (unsigned)desc->green > DOE_MTL_SWIZZLE_ALPHA ||
        (unsigned)desc->blue > DOE_MTL_SWIZZLE_ALPHA || (unsigned)desc->alpha > DOE_MTL_SWIZZLE_ALPHA)
        return DOE_MTL_INVALID_ARGUMENT;
    if (!doe_mtl_format_compatible(parent->format, desc->format) ||
        (parent->format != desc->format && !parent->allow_format_views)) return DOE_MTL_UNSUPPORTED;
    if (parent->sample_count > 1) {
        if ((desc->type != DOE_MTL_TEXTURE_2D_MS && desc->type != DOE_MTL_TEXTURE_2D_MS_ARRAY) ||
            (desc->type == DOE_MTL_TEXTURE_2D_MS && desc->layer_count != 1) ||
            (desc->type == DOE_MTL_TEXTURE_2D_MS_ARRAY && !texture->context->caps.multisample_array))
            return DOE_MTL_UNSUPPORTED;
        return DOE_MTL_OK;
    }
    if (parent->type == DOE_MTL_TEXTURE_3D) return desc->type == DOE_MTL_TEXTURE_3D ? DOE_MTL_OK : DOE_MTL_UNSUPPORTED;
    compatible_type = is_one_dimensional(parent->type) ? desc->type == parent->type :
        (desc->type == DOE_MTL_TEXTURE_2D || desc->type == DOE_MTL_TEXTURE_2D_ARRAY ||
         desc->type == DOE_MTL_TEXTURE_CUBE || desc->type == DOE_MTL_TEXTURE_CUBE_ARRAY);
    if (!compatible_type) return DOE_MTL_UNSUPPORTED;
    if ((desc->type == DOE_MTL_TEXTURE_1D || desc->type == DOE_MTL_TEXTURE_2D) && desc->layer_count != 1)
        return DOE_MTL_RANGE;
    if (desc->type == DOE_MTL_TEXTURE_CUBE || desc->type == DOE_MTL_TEXTURE_CUBE_ARRAY) {
        if (parent->width != parent->height || desc->base_layer % 6 != 0 || desc->layer_count % 6 != 0 ||
            (desc->type == DOE_MTL_TEXTURE_CUBE && desc->layer_count != 6)) return DOE_MTL_RANGE;
    }
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_texture_view_create(doe_mtl_texture *texture,
    const doe_mtl_texture_view_desc *desc, doe_mtl_texture_view **out, doe_mtl_error *error) {
    doe_mtl_texture_view *view;
    doe_mtl_status status;
    doe_mtl_context *context;
    if (texture == NULL || out == NULL)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "texture view create", error);
    context = texture->context;
    if (context->native.view == NULL)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "texture view native boundary", error);
    status = view_validate(texture, desc);
    if (status != DOE_MTL_OK) return doe_mtl_fail(status, "texture view descriptor", error);
    view = calloc(1, sizeof(*view));
    if (view == NULL) return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "texture view allocation", error);
    status = doe_mtl_texture_retain(texture);
    if (status != DOE_MTL_OK) {
        free(view);
        return doe_mtl_fail(status, "texture view lease", error);
    }
    view->texture = texture;
    view->references = 1;
    view->desc = *desc;
    status = context->native.view(context->native.userdata, texture->native, desc, &view->native, error);
    if (status != DOE_MTL_OK) {
        doe_mtl_texture_view_release(view);
        return status;
    }
    *out = view;
    return doe_mtl_fail(DOE_MTL_OK, "texture view create", error);
}

doe_mtl_status doe_mtl_texture_view_retain(doe_mtl_texture_view *view) {
    if (view == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (view->references == UINT32_MAX) return DOE_MTL_RANGE;
    view->references++;
    return DOE_MTL_OK;
}

void doe_mtl_texture_view_release(doe_mtl_texture_view *view) {
    doe_mtl_texture *texture;
    if (view == NULL || --view->references != 0) return;
    texture = view->texture;
    if (view->native != NULL)
        texture->context->native.release(texture->context->native.userdata, view->native);
    free(view);
    doe_mtl_texture_release(texture);
}

void *doe_mtl_texture_view_native(const doe_mtl_texture_view *view) {
    return view != NULL ? view->native : NULL;
}
