/* SPDX-License-Identifier: MIT */
#include "render_internal.h"

typedef struct attachment_clear {
    doe_mtl_texture_view *view;
    doe_mtl_render_pass_desc pass;
    bool needed;
} attachment_clear;

static doe_mtl_render_attachment initialized_attachment(doe_mtl_commands *commands,
    doe_mtl_texture_view *view, uint8_t aspect, bool *needed) {
    bool initialized = doe_mtl_texture_aspect_initialized(commands, view, aspect);
    *needed |= !initialized;
    return (doe_mtl_render_attachment){.view = view,
        .load = initialized ? DOE_MTL_LOAD : DOE_MTL_CLEAR, .store = DOE_MTL_STORE};
}

static doe_mtl_status prepare_clear(doe_mtl_commands *commands, attachment_clear *clear,
    uint8_t aspects) {
    doe_mtl_native_render_pass checked = {0};
    if (aspects & DOE_MTL_ASPECT_COLOR) {
        clear->pass.color_count = 1;
        clear->pass.colors[0] = initialized_attachment(commands, clear->view, DOE_MTL_ASPECT_COLOR, &clear->needed);
    }
    if (aspects & DOE_MTL_ASPECT_DEPTH)
        clear->pass.depth = initialized_attachment(commands, clear->view, DOE_MTL_ASPECT_DEPTH, &clear->needed);
    if (aspects & DOE_MTL_ASPECT_STENCIL)
        clear->pass.stencil = initialized_attachment(commands, clear->view, DOE_MTL_ASPECT_STENCIL, &clear->needed);
    return doe_mtl_render_pass_preflight(commands, &clear->pass, &checked);
}

doe_mtl_status doe_mtl_commands_initialize_attachment(doe_mtl_commands *commands,
    doe_mtl_texture_view *view, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_format_info format;
    attachment_clear *clears;
    doe_mtl_error detail = {0};
    size_t count, at = 0;
    uint32_t mip, layer;
    uint8_t aspects;
    if (status != DOE_MTL_OK) return status;
    if (view == NULL)
        return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "initialize attachment view", error);
    status = doe_mtl_texture_preflight(commands, view->texture, DOE_MTL_TEXTURE_RENDER_TARGET);
    if (status == DOE_MTL_OK) status = doe_mtl_texture_format_info(view->desc.format, &format);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "initialize attachment texture", error);
    if (view->texture->desc.type == DOE_MTL_TEXTURE_3D || view->texture->desc.type == DOE_MTL_TEXTURE_1D ||
        view->texture->desc.type == DOE_MTL_TEXTURE_1D_ARRAY || format.compressed ||
        view->desc.format == DOE_MTL_X24_STENCIL8 || view->desc.format == DOE_MTL_X32_STENCIL8 ||
        view->desc.red != DOE_MTL_SWIZZLE_RED || view->desc.green != DOE_MTL_SWIZZLE_GREEN ||
        view->desc.blue != DOE_MTL_SWIZZLE_BLUE || view->desc.alpha != DOE_MTL_SWIZZLE_ALPHA)
        return doe_mtl_poison(commands, DOE_MTL_UNSUPPORTED, "initialize attachment shape/aspects", error);
    aspects = doe_mtl_format_aspects(view->desc.format);
    if (doe_mtl_texture_aspect_initialized(commands, view, aspects))
        return doe_mtl_fail(DOE_MTL_OK, "initialized attachment", error);
    if (view->desc.mip_count > SIZE_MAX / view->desc.layer_count)
        return doe_mtl_poison(commands, DOE_MTL_OUT_OF_MEMORY, "attachment clear count", error);
    count = (size_t)view->desc.mip_count * view->desc.layer_count;
    if (count > SIZE_MAX / sizeof(*clears))
        return doe_mtl_poison(commands, DOE_MTL_OUT_OF_MEMORY, "attachment clear storage", error);
    clears = calloc(count, sizeof(*clears));
    if (clears == NULL)
        return doe_mtl_poison(commands, DOE_MTL_OUT_OF_MEMORY, "attachment clear allocation", error);
    /* Build and preflight every selected subresource before recording a native
     * pass. Local view references protect preparation; encoded passes acquire
     * their ordinary command leases before any unretained native use. */
    for (mip = 0; mip < view->desc.mip_count && status == DOE_MTL_OK; ++mip) {
        for (layer = 0; layer < view->desc.layer_count && status == DOE_MTL_OK; ++layer) {
            doe_mtl_texture_view_desc selected = view->desc;
            attachment_clear *clear = &clears[at++];
            if (count == 1) {
                status = doe_mtl_texture_view_retain(view);
                if (status == DOE_MTL_OK) clear->view = view;
            } else {
                selected.type = view->texture->desc.sample_count > 1 ? DOE_MTL_TEXTURE_2D_MS : DOE_MTL_TEXTURE_2D;
                selected.base_mip += mip;
                selected.base_layer += layer;
                selected.mip_count = selected.layer_count = 1;
                status = doe_mtl_texture_view_create(view->texture, &selected, &clear->view, &detail);
            }
            if (status == DOE_MTL_OK) status = prepare_clear(commands, clear, aspects);
        }
    }
    if (status != DOE_MTL_OK) {
        if (detail.status == status) (void)doe_mtl_native_record_result(commands, status, &detail, error);
        else (void)doe_mtl_poison(commands, status, "attachment clear preparation", error);
    } else {
        for (size_t i = 0; i < count && status == DOE_MTL_OK; ++i)
            if (clears[i].needed) status = doe_mtl_commands_render(commands, &clears[i].pass, NULL, 0, error);
    }
    for (size_t i = 0; i < at; ++i) doe_mtl_texture_view_release(clears[i].view);
    free(clears);
    return status;
}
