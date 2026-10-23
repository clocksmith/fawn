/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

static doe_mtl_status buffer_image(void *userdata, void *command, void *buffer,
    void *texture, const doe_mtl_native_texture_copy *copy, bool upload, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    const doe_mtl_texture_region *region = &copy->region;
    void *pool = doe_mtl_objc_pool(r), *encoder;
    uint32_t layer, count = copy->volume ? 1 : region->depth_or_layers;
    const doe_mtl_size size = {region->width, region->height,
        copy->volume ? region->depth_or_layers : 1};
    const doe_mtl_origin origin = {region->x, region->y, copy->volume ? region->z : 0};
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    encoder = doe_mtl_objc_object(r, command, r->blit_encoder);
    if (encoder == NULL) {
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "blitCommandEncoder", error);
    }
    /* An array is a sequence of native 2D copies, not a depth extent. The
     * padded image stride advances buffer offsets but is zero at the 2D ABI. */
    for (layer = 0; layer < count; ++layer) {
        size_t offset = copy->buffer_offset + (size_t)layer * copy->bytes_per_image;
        size_t slice = copy->volume ? 0 : region->z + layer;
        size_t stride = copy->volume ? copy->bytes_per_image : 0;
        if (upload) r->upload_image_call(encoder, r->upload_image, buffer,
            offset, copy->bytes_per_row, stride, size, texture, slice, region->mip, origin, 0);
        else r->read_image_call(encoder, r->read_image, texture, slice, region->mip,
            origin, size, buffer, offset, copy->bytes_per_row, stride, 0);
    }
    doe_mtl_objc_void(r, encoder, r->end_encoding);
    doe_mtl_objc_drain(r, pool);
    return doe_mtl_fail(DOE_MTL_OK, upload ? "copyFromBuffer:toTexture:" : "copyFromTexture:toBuffer:", error);
}

doe_mtl_status doe_mtl_objc_upload_image(void *userdata, void *command, void *buffer,
    void *texture, const doe_mtl_native_texture_copy *copy, doe_mtl_error *error) {
    return buffer_image(userdata, command, buffer, texture, copy, true, error);
}
doe_mtl_status doe_mtl_objc_read_image(void *userdata, void *command, void *texture,
    void *buffer, const doe_mtl_native_texture_copy *copy, doe_mtl_error *error) {
    return buffer_image(userdata, command, buffer, texture, copy, false, error);
}

doe_mtl_status doe_mtl_objc_copy_image(void *userdata, void *command, void *source,
    const doe_mtl_native_texture_copy *from, void *destination,
    const doe_mtl_native_texture_copy *to, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    void *pool = doe_mtl_objc_pool(r), *encoder;
    uint32_t layer;
    doe_mtl_size size = {from->region.width, from->region.height, 1};
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    encoder = doe_mtl_objc_object(r, command, r->blit_encoder);
    if (encoder == NULL) {
        doe_mtl_objc_drain(r, pool);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "blitCommandEncoder", error);
    }
    for (layer = 0; layer < from->region.depth_or_layers; ++layer) {
        doe_mtl_origin source_origin = {from->region.x, from->region.y,
            from->volume ? from->region.z + layer : 0};
        doe_mtl_origin destination_origin = {to->region.x, to->region.y,
            to->volume ? to->region.z + layer : 0};
        r->copy_image_call(encoder, r->copy_image, source,
            from->volume ? 0 : from->region.z + layer, from->region.mip, source_origin,
            size, destination, to->volume ? 0 : to->region.z + layer,
            to->region.mip, destination_origin);
    }
    doe_mtl_objc_void(r, encoder, r->end_encoding);
    doe_mtl_objc_drain(r, pool);
    return doe_mtl_fail(DOE_MTL_OK, "copyFromTexture:toTexture:", error);
}
