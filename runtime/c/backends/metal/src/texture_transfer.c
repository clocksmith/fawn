/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"

/* All extents are nonempty. Block-compressed edge blocks may be partial only
 * at the edge of a mip, never at an interior origin or interior boundary. */
doe_mtl_status doe_mtl_texture_region_check(const doe_mtl_texture *texture,
    doe_mtl_texture_region r, bool *full) {
    uint32_t width, height, depth;
    const doe_mtl_format_info *f = &texture->format;
    if (r.mip >= texture->desc.mip_levels) return DOE_MTL_RANGE;
    width = doe_mtl_mip_extent(texture->desc.width, r.mip);
    height = doe_mtl_mip_extent(texture->desc.height, r.mip);
    depth = texture->desc.type == DOE_MTL_TEXTURE_3D ?
        doe_mtl_mip_extent(texture->desc.depth_or_layers, r.mip) : texture->desc.depth_or_layers;
    if (r.width == 0 || r.height == 0 || r.depth_or_layers == 0 ||
        r.x > width || r.width > width - r.x || r.y > height || r.height > height - r.y ||
        r.z > depth || r.depth_or_layers > depth - r.z) return DOE_MTL_RANGE;
    if (r.x % f->block_width != 0 || r.y % f->block_height != 0 ||
        (r.width % f->block_width != 0 && r.x + r.width != width) ||
        (r.height % f->block_height != 0 && r.y + r.height != height))
        return DOE_MTL_ALIGNMENT;
    *full = r.x == 0 && r.y == 0 && r.width == width && r.height == height &&
        (texture->desc.type != DOE_MTL_TEXTURE_3D || (r.z == 0 && r.depth_or_layers == depth));
    return DOE_MTL_OK;
}

static doe_mtl_status transfer_layout(const doe_mtl_texture *texture,
    doe_mtl_texture_region region, doe_mtl_texture_buffer_layout layout,
    const doe_mtl_buffer *buffer, doe_mtl_native_texture_copy *copy, bool *covers_buffer) {
    const doe_mtl_format_info *f = &texture->format;
    uint64_t columns = ((uint64_t)region.width + f->block_width - 1) / f->block_width;
    uint64_t rows = ((uint64_t)region.height + f->block_height - 1) / f->block_height;
    uint64_t row_bytes = columns * f->block_bytes;
    uint64_t pitch = layout.bytes_per_row != 0 ? layout.bytes_per_row : (row_bytes + 255) / 256 * 256;
    uint64_t image_rows = layout.rows_per_image != 0 ? layout.rows_per_image : rows;
    uint32_t max_width = texture->desc.type == DOE_MTL_TEXTURE_3D ? texture->context->caps.max_texture_3d :
        (texture->desc.type == DOE_MTL_TEXTURE_1D || texture->desc.type == DOE_MTL_TEXTURE_1D_ARRAY ?
        texture->context->caps.max_texture_1d : texture->context->caps.max_texture_2d);
    uint64_t max_pitch = (uint64_t)max_width / f->block_width * f->block_bytes;
    uint64_t stride, tail, length;
    if (layout.offset % f->block_bytes != 0 || pitch % 256 != 0) return DOE_MTL_ALIGNMENT;
    if (pitch < row_bytes || pitch > max_pitch || image_rows < rows || pitch > SIZE_MAX ||
        image_rows > SIZE_MAX / pitch) return DOE_MTL_RANGE;
    stride = pitch * image_rows;
    tail = pitch * (rows - 1) + row_bytes;
    if (region.depth_or_layers - 1 > (SIZE_MAX - tail) / stride) return DOE_MTL_RANGE;
    length = stride * (region.depth_or_layers - 1) + tail;
    if (!doe_mtl_range(buffer->size, layout.offset, length)) return DOE_MTL_RANGE;
    copy->buffer_offset = (size_t)layout.offset;
    copy->bytes_per_row = (size_t)pitch;
    copy->bytes_per_image = (size_t)stride;
    copy->region = region;
    copy->volume = texture->desc.type == DOE_MTL_TEXTURE_3D;
    *covers_buffer = layout.offset == 0 && length == buffer->size &&
        (rows == 1 || pitch == row_bytes) &&
        (region.depth_or_layers == 1 || stride == row_bytes * rows);
    return DOE_MTL_OK;
}

static doe_mtl_status buffer_texture_transfer(doe_mtl_commands *commands,
    doe_mtl_buffer *buffer, doe_mtl_texture_buffer_layout layout,
    doe_mtl_texture *texture, doe_mtl_texture_region region, bool upload, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_native_texture_copy copy = {0};
    doe_mtl_texture_use *texture_use;
    doe_mtl_buffer_use *buffer_use;
    doe_mtl_native *native;
    doe_mtl_error detail = {0};
    bool full = false, covers_buffer = false;
    if (status != DOE_MTL_OK) return status;
    status = doe_mtl_buffer_preflight(commands, buffer, upload);
    if (status == DOE_MTL_OK) status = doe_mtl_texture_preflight(commands, texture,
        upload ? DOE_MTL_TEXTURE_COPY_DST : DOE_MTL_TEXTURE_COPY_SRC);
    if (status == DOE_MTL_OK) status = doe_mtl_texture_region_check(texture, region, &full);
    if (status == DOE_MTL_OK && (!upload || !full) &&
        !doe_mtl_texture_region_initialized(commands, texture, region)) status = DOE_MTL_UNINITIALIZED;
    if (status == DOE_MTL_OK)
        status = transfer_layout(texture, region, layout, buffer, &copy, &covers_buffer);
    native = &commands->context->native;
    if (status == DOE_MTL_OK && (upload ? native->upload_texture == NULL : native->read_texture == NULL))
        status = DOE_MTL_UNSUPPORTED;
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "texture transfer validation", error);
    status = doe_mtl_use_buffer(commands, buffer, &buffer_use);
    if (status == DOE_MTL_OK) status = doe_mtl_use_texture(commands, texture, &texture_use);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "texture transfer lease", error);
    if (upload) status = native->upload_texture(native->userdata, commands->native,
        buffer->native, texture->native, &copy, &detail);
    else status = native->read_texture(native->userdata, commands->native,
        texture->native, buffer->native, &copy, &detail);
    if (status == DOE_MTL_OK) {
        if (upload && full) doe_mtl_texture_region_initialize(texture_use, region);
        if (!upload && covers_buffer) buffer_use->initialized = true;
    }
    return doe_mtl_native_record_result(commands, status, &detail, error);
}

doe_mtl_status doe_mtl_commands_upload_texture(doe_mtl_commands *commands,
    doe_mtl_buffer *source, doe_mtl_texture_buffer_layout layout,
    doe_mtl_texture *destination, doe_mtl_texture_region region, doe_mtl_error *error) {
    return buffer_texture_transfer(commands, source, layout, destination, region, true, error);
}
doe_mtl_status doe_mtl_commands_read_texture(doe_mtl_commands *commands,
    doe_mtl_texture *source, doe_mtl_texture_region region,
    doe_mtl_buffer *destination, doe_mtl_texture_buffer_layout layout, doe_mtl_error *error) {
    return buffer_texture_transfer(commands, destination, layout, source, region, false, error);
}

static bool overlaps(doe_mtl_texture_region a, doe_mtl_texture_region b) {
    return a.mip == b.mip && a.x < b.x + b.width && b.x < a.x + a.width &&
        a.y < b.y + b.height && b.y < a.y + a.height &&
        a.z < b.z + b.depth_or_layers && b.z < a.z + a.depth_or_layers;
}

doe_mtl_status doe_mtl_commands_copy_texture(doe_mtl_commands *commands,
    doe_mtl_texture *source, doe_mtl_texture_region source_region,
    doe_mtl_texture *destination, doe_mtl_texture_region destination_region, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_texture_use *source_use, *destination_use;
    doe_mtl_native_texture_copy from = {0}, to = {0};
    doe_mtl_native *native;
    doe_mtl_error detail = {0};
    bool full = false;
    if (status != DOE_MTL_OK) return status;
    status = doe_mtl_texture_preflight(commands, source, DOE_MTL_TEXTURE_COPY_SRC);
    if (status == DOE_MTL_OK) status = doe_mtl_texture_preflight(commands, destination, DOE_MTL_TEXTURE_COPY_DST);
    if (status == DOE_MTL_OK) status = doe_mtl_texture_region_check(source, source_region, &full);
    if (status == DOE_MTL_OK) status = doe_mtl_texture_region_check(destination, destination_region, &full);
    if (status == DOE_MTL_OK && (source->desc.format != destination->desc.format ||
        source_region.width != destination_region.width || source_region.height != destination_region.height ||
        source_region.depth_or_layers != destination_region.depth_or_layers)) status = DOE_MTL_INVALID_ARGUMENT;
    if (status == DOE_MTL_OK && source == destination && overlaps(source_region, destination_region))
        status = DOE_MTL_RANGE;
    if (status == DOE_MTL_OK && (!doe_mtl_texture_region_initialized(commands, source, source_region) ||
        (!full && !doe_mtl_texture_region_initialized(commands, destination, destination_region))))
        status = DOE_MTL_UNINITIALIZED;
    native = &commands->context->native;
    if (status == DOE_MTL_OK && native->copy_texture == NULL) status = DOE_MTL_UNSUPPORTED;
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "texture copy validation", error);
    status = doe_mtl_use_texture(commands, source, &source_use);
    if (status == DOE_MTL_OK) status = doe_mtl_use_texture(commands, destination, &destination_use);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "texture copy lease", error);
    from.region = source_region;
    from.volume = source->desc.type == DOE_MTL_TEXTURE_3D;
    to.region = destination_region;
    to.volume = destination->desc.type == DOE_MTL_TEXTURE_3D;
    status = native->copy_texture(native->userdata, commands->native, source->native,
        &from, destination->native, &to, &detail);
    if (status == DOE_MTL_OK && full) doe_mtl_texture_region_initialize(destination_use, destination_region);
    return doe_mtl_native_record_result(commands, status, &detail, error);
}
