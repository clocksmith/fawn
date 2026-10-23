/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

static bool attachment(doe_mtl_objc *r, void *descriptor,
    const doe_mtl_native_render_attachment *attachment, unsigned aspect) {
    doe_mtl_objc_graphics *g = &r->graphics;
    if (descriptor == NULL) return false;
    r->set_object_call(descriptor, g->set_texture, attachment->texture);
    if (attachment->texture == NULL) return true;
    /* The retained view already rebases its single mip/layer to zero. */
    r->set_size_call(descriptor, g->set_level, 0);
    r->set_size_call(descriptor, g->set_slice, 0);
    r->set_size_call(descriptor, g->set_load, (size_t)attachment->load);
    r->set_size_call(descriptor, g->set_store, (size_t)attachment->store);
    if (attachment->resolve != NULL) {
        r->set_object_call(descriptor, g->set_resolve, attachment->resolve);
        r->set_size_call(descriptor, g->set_resolve_level, 0);
        r->set_size_call(descriptor, g->set_resolve_slice, 0);
        if (aspect != 0) r->set_size_call(descriptor,
            aspect == 1 ? g->set_depth_resolve : g->set_stencil_resolve, attachment->resolve_filter);
    }
    if (attachment->load == DOE_MTL_CLEAR) {
        if (aspect == 0) g->clear_color(descriptor, g->set_clear_color, attachment->color);
        else if (aspect == 1) g->set_double(descriptor, g->set_clear_depth, attachment->depth);
        else g->set_u32(descriptor, g->set_clear_stencil, attachment->stencil);
    }
    return true;
}
doe_mtl_status doe_mtl_objc_render(void *userdata, void *command,
    const doe_mtl_native_render_pass *pass, const doe_mtl_native_render_draw *draws,
    size_t draw_count, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    doe_mtl_objc_graphics *g = &r->graphics;
    void *pool = doe_mtl_objc_pool(r), *descriptor = NULL, *array, *encoder;
    doe_mtl_status status;
    size_t i, visibility_offset = 0;
    uint32_t visibility_mode = 0;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    descriptor = doe_mtl_objc_object(r, g->pass_class, r->new_object);
    if (descriptor == NULL) goto descriptor_failure;
    array = doe_mtl_objc_object(r, descriptor, g->colors);
    if (array == NULL) goto descriptor_failure;
    for (i = 0; i < pass->color_count; ++i)
        if (!attachment(r, g->index_object(array, g->index, i), &pass->colors[i], 0)) goto descriptor_failure;
    if (pass->depth.texture != NULL &&
        !attachment(r, doe_mtl_objc_object(r, descriptor, g->depth_attachment), &pass->depth, 1)) goto descriptor_failure;
    if (pass->stencil.texture != NULL &&
        !attachment(r, doe_mtl_objc_object(r, descriptor, g->stencil_attachment), &pass->stencil, 2)) goto descriptor_failure;
    if (pass->visibility_buffer != NULL)
        r->set_object_call(descriptor, g->set_visibility_buffer, pass->visibility_buffer);
    encoder = r->named_call(command, g->new_encoder, descriptor);
    if (encoder == NULL) {
        status = doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "renderCommandEncoderWithDescriptor:", error);
        goto cleanup;
    }
    for (i = 0; i < draw_count; ++i) {
        const doe_mtl_render_draw *draw = &draws[i].values;
        if (draw->visibility_mode != visibility_mode || draw->visibility_offset != visibility_offset) {
            visibility_mode = draw->visibility_mode;
            visibility_offset = (size_t)draw->visibility_offset;
            g->visibility(encoder, g->set_visibility, visibility_mode, visibility_offset);
        }
        doe_mtl_objc_render_encode_draw(r, encoder, pass, &draws[i]);
    }
    doe_mtl_objc_void(r, encoder, r->end_encoding);
    status = doe_mtl_fail(DOE_MTL_OK, "render pass encode", error);
    goto cleanup;
descriptor_failure:
    status = doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "render pass descriptor", error);
cleanup:
    if (descriptor != NULL) doe_mtl_objc_void(r, descriptor, r->release);
    doe_mtl_objc_drain(r, pool);
    return status;
}
