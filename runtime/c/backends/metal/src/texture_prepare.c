/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"

/* Called only after the complete transfer's format/range/layout/alias checks
 * and resource leases. Full destination overwrites never enter this path. */
doe_mtl_status doe_mtl_initialize_texture_region(doe_mtl_commands *commands, doe_mtl_texture *texture,
    doe_mtl_texture_region region, uint8_t aspects, doe_mtl_error *error) {
    if (doe_mtl_texture_region_aspects_initialized(commands, texture, region, aspects))
        return doe_mtl_fail(DOE_MTL_OK, "initialized texture region", error);
    if (!(texture->desc.usage & DOE_MTL_TEXTURE_RENDER_TARGET))
        return doe_mtl_fail(DOE_MTL_UNINITIALIZED, "uninitialized texture region", error);
    if (texture->desc.type == DOE_MTL_TEXTURE_3D || texture->desc.type == DOE_MTL_TEXTURE_1D ||
        texture->desc.type == DOE_MTL_TEXTURE_1D_ARRAY)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "texture region clear shape", error);
    doe_mtl_texture_view_desc span = {.type=texture->desc.sample_count > 1 ? DOE_MTL_TEXTURE_2D_MS_ARRAY : DOE_MTL_TEXTURE_2D_ARRAY,
        .format=texture->desc.format,.base_mip=region.mip,.mip_count=1,.base_layer=region.z,.layer_count=region.depth_or_layers,
        .red=DOE_MTL_SWIZZLE_RED,.green=DOE_MTL_SWIZZLE_GREEN,.blue=DOE_MTL_SWIZZLE_BLUE,.alpha=DOE_MTL_SWIZZLE_ALPHA};
    doe_mtl_texture_view *view = NULL;
    doe_mtl_status status = doe_mtl_texture_view_create(texture, &span, &view, error);
    if (status == DOE_MTL_OK) status = doe_mtl_commands_initialize_attachment(commands, view, error);
    doe_mtl_texture_view_release(view);
    return status;
}
