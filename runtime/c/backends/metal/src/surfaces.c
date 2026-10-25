/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"

struct doe_mtl_surface {
    doe_mtl_context *context;
    uint32_t references;
    void *layer;
    doe_mtl_surface_desc desc;
};

bool doe_mtl_surface_supported(doe_mtl_context *context, void *layer) {
    return context && layer && context->native.surface_supported &&
        context->native.surface_supported(context->native.userdata, layer);
}

doe_mtl_status doe_mtl_surface_create(doe_mtl_context *context, void *layer,
    const doe_mtl_surface_desc *desc, doe_mtl_surface **out, doe_mtl_error *error) {
    if (!context || !layer || !desc || !out || !desc->width || !desc->height ||
        desc->width > context->caps.max_texture_2d || desc->height > context->caps.max_texture_2d ||
        !desc->usage || (desc->usage & ~UINT32_C(23)))
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "Metal surface configuration", error);
    if ((desc->format != DOE_MTL_BGRA8_UNORM && desc->format != DOE_MTL_BGRA8_SRGB) ||
        !doe_mtl_surface_supported(context, layer) || !context->native.surface_configure ||
        !context->native.surface_acquire || !context->native.surface_present || !context->native.surface_observe)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "CAMetalLayer presentation", error);
    doe_mtl_surface *surface = calloc(1, sizeof(*surface));
    if (!surface) return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "Metal surface owner", error);
    doe_mtl_status status = doe_mtl_context_retain(context);
    if (status != DOE_MTL_OK) { free(surface); return status; }
    surface->context = context;
    surface->references = 1;
    surface->desc = *desc;
    status = context->native.surface_configure(context->native.userdata, context->device,
        layer, desc, &surface->layer, error);
    if (status != DOE_MTL_OK) { doe_mtl_surface_release(surface); return status; }
    *out = surface;
    return DOE_MTL_OK;
}

void doe_mtl_surface_release(doe_mtl_surface *surface) {
    if (!surface || --surface->references) return;
    doe_mtl_context *context = surface->context;
    if (surface->layer) context->native.release(context->native.userdata, surface->layer);
    free(surface);
    doe_mtl_context_release(context);
}

doe_mtl_status doe_mtl_surface_acquire(doe_mtl_surface *surface,
    doe_mtl_texture **out, doe_mtl_error *error) {
    if (!surface || !out) return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "Metal surface acquisition", error);
    if (surface->references == UINT32_MAX) return doe_mtl_fail(DOE_MTL_RANGE, "Metal surface leases", error);
    doe_mtl_texture *texture = calloc(1, sizeof(*texture));
    if (!texture) return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "drawable texture owner", error);
    texture->initialized = calloc(1, 1);
    if (!texture->initialized) { free(texture); return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "drawable initialization", error); }
    doe_mtl_context *context = surface->context;
    doe_mtl_status status = doe_mtl_context_retain(context);
    if (status != DOE_MTL_OK) { free(texture->initialized); free(texture); return status; }
    texture->context = context;
    texture->references = 1;
    texture->subresource_count = 1;
    texture->surface = surface;
    ++surface->references;
    texture->desc = (doe_mtl_texture_desc){.type=DOE_MTL_TEXTURE_2D,.format=surface->desc.format,
        .width=surface->desc.width,.height=surface->desc.height,.depth_or_layers=1,.mip_levels=1,
        .sample_count=1,.usage=surface->desc.usage | DOE_MTL_TEXTURE_RENDER_TARGET};
    status = doe_mtl_texture_format_info(surface->desc.format, &texture->format);
    if (status == DOE_MTL_OK) status = context->native.surface_acquire(context->native.userdata,
        surface->layer, &surface->desc, &texture->drawable, &texture->native, error);
    if (status != DOE_MTL_OK) { doe_mtl_texture_release(texture); return status; }
    *out = texture;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_commands_present(doe_mtl_commands *commands,
    doe_mtl_texture *texture, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    if (status != DOE_MTL_OK) return status;
    if (!texture || !texture->surface || !texture->drawable || commands->present_texture)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "drawable presentation", error);
    status = doe_mtl_texture_preflight(commands, texture, DOE_MTL_TEXTURE_RENDER_TARGET);
    doe_mtl_texture_use *use = NULL;
    if (status == DOE_MTL_OK) status = doe_mtl_use_texture(commands, texture, &use);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "drawable presentation lease", error);
    if (!(use->initialized[0] & DOE_MTL_ASPECT_COLOR))
        return doe_mtl_poison(commands, DOE_MTL_UNINITIALIZED, "drawable presentation contents", error);
    doe_mtl_native *native = &commands->context->native;
    doe_mtl_error detail = {0};
    status = native->surface_present(native->userdata, commands->native, texture->drawable, &detail);
    if (status == DOE_MTL_OK) {
        commands->present_texture = texture;
        texture->presentation_registered = true;
    }
    return doe_mtl_native_record_result(commands, status, &detail, error);
}
