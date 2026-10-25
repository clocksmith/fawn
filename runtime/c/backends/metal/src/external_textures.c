/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"
#include "doe_metal_external.h"

doe_mtl_status doe_mtl_texture_adopt_external(doe_mtl_context *context,
    const doe_mtl_texture_desc *desc, void *native, doe_mtl_texture **out,
    doe_mtl_error *error)
{
    if (!native || desc->type != DOE_MTL_TEXTURE_2D || !desc->width || !desc->height ||
        desc->width > context->caps.max_texture_2d || desc->height > context->caps.max_texture_2d ||
        desc->depth_or_layers != 1 || desc->mip_levels != 1 || desc->sample_count != 1 ||
        !desc->usage || (desc->usage & ~(uint32_t)(DOE_MTL_TEXTURE_SAMPLED |
            DOE_MTL_TEXTURE_RENDER_TARGET | DOE_MTL_TEXTURE_COPY_SRC | DOE_MTL_TEXTURE_COPY_DST)))
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "external plane shape", error);
    if (desc->format != DOE_MTL_RGBA8_UNORM && desc->format != DOE_MTL_BGRA8_UNORM &&
        desc->format != DOE_MTL_RGBA8_SRGB && desc->format != DOE_MTL_BGRA8_SRGB &&
        desc->format != DOE_MTL_R8_UNORM && desc->format != DOE_MTL_RG8_UNORM)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "external plane format", error);
    doe_mtl_texture *texture = calloc(1, sizeof(*texture));
    if (!texture) return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "external plane owner", error);
    texture->initialized = calloc(1, sizeof(*texture->initialized));
    if (!texture->initialized) {
        free(texture);
        return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "external plane initialization", error);
    }
    doe_mtl_status status = doe_mtl_context_retain(context);
    if (status != DOE_MTL_OK) {
        free(texture->initialized);
        free(texture);
        return doe_mtl_fail(status, "external plane context lease", error);
    }
    texture->context = context;
    texture->references = 1;
    texture->desc = *desc;
    texture->subresource_count = 1;
    texture->initialized[0] = DOE_MTL_ASPECT_COLOR;
    status = doe_mtl_texture_format_info(desc->format, &texture->format);
    if (status != DOE_MTL_OK) {
        doe_mtl_texture_release(texture);
        return doe_mtl_fail(status, "external plane format", error);
    }
    texture->native = native;
    *out = texture;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_external_texture_import(doe_mtl_context *context,
    doe_mtl_external_source source, void *handle, doe_mtl_external_planes *out,
    doe_mtl_error *error)
{
    if (!context || !handle || !out ||
        (source != DOE_MTL_EXTERNAL_IOSURFACE && source != DOE_MTL_EXTERNAL_CVPIXELBUFFER))
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "external texture import", error);
    if (!context->native.external_texture)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "external texture native boundary", error);
    doe_mtl_native_external imported = {0};
    doe_mtl_status status = context->native.external_texture(context->native.userdata,
        context->device, source, handle, &imported, error);
    if (status != DOE_MTL_OK) return status;
    doe_mtl_external_planes result = {0};
    if (!imported.count || imported.count > 2)
        status = doe_mtl_fail(DOE_MTL_UNSUPPORTED, "external texture plane count", error);
    for (uint32_t i = 0; status == DOE_MTL_OK && i < imported.count; ++i) {
        if (imported.desc[i].usage != (DOE_MTL_TEXTURE_SAMPLED | DOE_MTL_TEXTURE_COPY_SRC) ||
            imported.desc[i].allow_format_views)
            status = doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "external plane usage", error);
        else status = doe_mtl_texture_adopt_external(context, &imported.desc[i], imported.planes[i], &result.planes[i], error);
        if (status == DOE_MTL_OK) {
            imported.planes[i] = NULL;
            ++result.count;
        }
    }
    for (uint32_t i = 0; i < 2; ++i)
        if (imported.planes[i]) context->native.release(context->native.userdata, imported.planes[i]);
    if (status != DOE_MTL_OK) {
        for (uint32_t i = 0; i < result.count; ++i) doe_mtl_texture_release(result.planes[i]);
        return status;
    }
    *out = result;
    return doe_mtl_fail(DOE_MTL_OK, "external texture import", error);
}
