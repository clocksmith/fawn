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
    uint64_t physical_width = ((uint64_t)width + f->block_width - 1) / f->block_width * f->block_width;
    uint64_t physical_height = ((uint64_t)height + f->block_height - 1) / f->block_height * f->block_height;
    if (physical_width > UINT32_MAX || physical_height > UINT32_MAX ||
        r.width == 0 || r.height == 0 || r.depth_or_layers == 0 ||
        r.x >= width || r.width > physical_width - r.x || r.y >= height || r.height > physical_height - r.y ||
        r.z > depth || r.depth_or_layers > depth - r.z) return DOE_MTL_RANGE;
    if (r.x % f->block_width != 0 || r.y % f->block_height != 0 ||
        (r.width % f->block_width != 0 && r.x + r.width != width) ||
        (r.height % f->block_height != 0 && r.y + r.height != height))
        return DOE_MTL_ALIGNMENT;
    *full = r.x == 0 && r.y == 0 && r.width >= width && r.height >= height &&
        (texture->desc.type != DOE_MTL_TEXTURE_3D || (r.z == 0 && r.depth_or_layers == depth));
    return DOE_MTL_OK;
}

static doe_mtl_status transfer_format(const doe_mtl_texture *texture, doe_mtl_transfer_aspect aspect,
    doe_mtl_format_info *format, uint8_t *aspects, size_t *options) {
    *format = texture->format;
    *aspects = doe_mtl_format_aspects(texture->desc.format);
    *options = 0;
    if (aspect == DOE_MTL_TRANSFER_ALL) {
        if (format->depth && format->stencil) return DOE_MTL_INVALID_ARGUMENT;
    } else if (aspect == DOE_MTL_TRANSFER_DEPTH) {
        if (!format->depth) return DOE_MTL_INVALID_ARGUMENT;
        if (texture->desc.format == DOE_MTL_DEPTH24_STENCIL8) return DOE_MTL_UNSUPPORTED;
        *aspects = DOE_MTL_ASPECT_DEPTH;
        if (format->stencil) { format->block_bytes = 4; *options = 1; }
    } else if (aspect == DOE_MTL_TRANSFER_STENCIL) {
        if (!format->stencil) return DOE_MTL_INVALID_ARGUMENT;
        *aspects = DOE_MTL_ASPECT_STENCIL;
        format->block_bytes = 1;
        if (format->depth) *options = 2;
    } else return DOE_MTL_INVALID_ARGUMENT;
    return format->block_bytes ? DOE_MTL_OK : DOE_MTL_UNSUPPORTED;
}

static doe_mtl_status transfer_layout(const doe_mtl_texture *texture,
    doe_mtl_texture_region region, doe_mtl_texture_buffer_layout layout,
    const doe_mtl_buffer *buffer, const doe_mtl_format_info *f,
    doe_mtl_native_texture_copy *copy, bool *covers_buffer) {
    uint64_t columns = ((uint64_t)region.width + f->block_width - 1) / f->block_width;
    uint64_t rows = ((uint64_t)region.height + f->block_height - 1) / f->block_height;
    uint64_t row_bytes = columns * f->block_bytes;
    uint64_t pitch = layout.bytes_per_row != 0 ? layout.bytes_per_row : (row_bytes + 255) / 256 * 256;
    uint64_t image_rows = layout.rows_per_image != 0 ? layout.rows_per_image : rows;
    uint64_t stride, tail, length;
    if (layout.offset % f->block_bytes != 0 || pitch % 256 != 0) return DOE_MTL_ALIGNMENT;
    if (pitch < row_bytes || image_rows < rows || pitch > SIZE_MAX ||
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
    /* Metal buffer/image transfers name virtual texels at a compressed mip
     * edge, while the validated buffer footprint still contains whole blocks. */
    uint32_t remaining_width = doe_mtl_mip_extent(texture->desc.width, region.mip) - region.x;
    uint32_t remaining_height = doe_mtl_mip_extent(texture->desc.height, region.mip) - region.y;
    if (copy->region.width > remaining_width) copy->region.width = remaining_width;
    if (copy->region.height > remaining_height) copy->region.height = remaining_height;
    copy->volume = texture->desc.type == DOE_MTL_TEXTURE_3D;
    *covers_buffer = layout.offset == 0 && length == buffer->size &&
        (rows == 1 || pitch == row_bytes) &&
        (region.depth_or_layers == 1 || stride == row_bytes * rows);
    return DOE_MTL_OK;
}

/* Metal bounds native row strides by its maximum texture width. WebGPU
 * allows larger padding; lower those copies into block rows, without copying
 * padding or changing the logical footprint already checked above. */
static doe_mtl_status encode_transfer(doe_mtl_commands *commands, doe_mtl_buffer *buffer,
    doe_mtl_texture *texture, const doe_mtl_format_info *f,
    const doe_mtl_native_texture_copy *copy, bool upload, doe_mtl_error *error) {
    uint32_t max_width = texture->desc.type == DOE_MTL_TEXTURE_3D ? texture->context->caps.max_texture_3d :
        (texture->desc.type == DOE_MTL_TEXTURE_1D || texture->desc.type == DOE_MTL_TEXTURE_1D_ARRAY ?
        texture->context->caps.max_texture_1d : texture->context->caps.max_texture_2d);
    uint64_t max_pitch = (uint64_t)max_width / f->block_width * f->block_bytes;
    doe_mtl_native *native = &commands->context->native;
    bool split = copy->bytes_per_row > max_pitch;
    doe_mtl_native_texture_copy part = *copy;
    uint32_t layers = split ? copy->region.depth_or_layers : 1;
    uint32_t rows = split ? (uint32_t)(((uint64_t)copy->region.height + f->block_height - 1) / f->block_height) : 1;
    if (split) {
        uint64_t columns = ((uint64_t)copy->region.width + f->block_width - 1) / f->block_width;
        /* Each native call has one block row: only block-byte alignment is
         * required by Metal, and no public padding is consumed by this row. */
        part.bytes_per_row = (size_t)(columns * f->block_bytes);
        if (part.bytes_per_row > max_pitch) return DOE_MTL_RANGE;
        part.bytes_per_image = part.bytes_per_row;
        part.region.depth_or_layers = 1;
    }
    for (uint32_t layer = 0; layer < layers; ++layer) {
        for (uint32_t row = 0; row < rows; ++row) {
            if (split) {
                uint32_t y = row * f->block_height;
                part.buffer_offset = copy->buffer_offset + (size_t)layer * copy->bytes_per_image +
                    (size_t)row * copy->bytes_per_row;
                part.region.z = copy->region.z + layer;
                part.region.y = copy->region.y + y;
                part.region.height = copy->region.height - y;
                if (part.region.height > f->block_height) part.region.height = f->block_height;
            }
            doe_mtl_status status = upload ? native->upload_texture(native->userdata, commands->native,
                buffer->native, texture->native, &part, error) :
                native->read_texture(native->userdata, commands->native, texture->native, buffer->native, &part, error);
            if (status != DOE_MTL_OK) return status;
        }
    }
    return DOE_MTL_OK;
}

static doe_mtl_status buffer_texture_transfer(doe_mtl_commands *commands,
    doe_mtl_buffer *buffer, doe_mtl_texture_buffer_layout layout,
    doe_mtl_texture *texture, doe_mtl_texture_region region, doe_mtl_transfer_aspect aspect,
    bool upload, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_native_texture_copy copy = {0};
    doe_mtl_texture_use *texture_use;
    doe_mtl_buffer_use *buffer_use;
    doe_mtl_native *native;
    doe_mtl_error detail = {0};
    bool full = false, covers_buffer = false;
    doe_mtl_format_info format = {0};
    uint8_t aspects = 0;
    if (status != DOE_MTL_OK) return status;
    status = doe_mtl_buffer_preflight(commands, buffer, upload);
    if (status == DOE_MTL_OK) status = doe_mtl_texture_preflight(commands, texture,
        upload ? DOE_MTL_TEXTURE_COPY_DST : DOE_MTL_TEXTURE_COPY_SRC);
    if (status == DOE_MTL_OK && texture->desc.sample_count != 1) status = DOE_MTL_UNSUPPORTED;
    if (status == DOE_MTL_OK) status = transfer_format(texture, aspect, &format, &aspects, &copy.options);
    if (status == DOE_MTL_OK) status = doe_mtl_texture_region_check(texture, region, &full);
    if (status == DOE_MTL_OK && (format.depth || format.stencil) && !full) status = DOE_MTL_UNSUPPORTED;
    if (status == DOE_MTL_OK && (!upload || !full) &&
        !doe_mtl_texture_region_aspects_initialized(commands, texture, region, aspects)) status = DOE_MTL_UNINITIALIZED;
    if (status == DOE_MTL_OK)
        status = transfer_layout(texture, region, layout, buffer, &format, &copy, &covers_buffer);
    native = &commands->context->native;
    if (status == DOE_MTL_OK && (upload ? native->upload_texture == NULL : native->read_texture == NULL))
        status = DOE_MTL_UNSUPPORTED;
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "texture transfer validation", error);
    status = doe_mtl_use_buffer(commands, buffer, &buffer_use);
    if (status == DOE_MTL_OK) status = doe_mtl_use_texture(commands, texture, &texture_use);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "texture transfer lease", error);
    status = encode_transfer(commands, buffer, texture, &format, &copy, upload, &detail);
    if (status == DOE_MTL_OK) {
        if (upload && full) doe_mtl_texture_region_aspects_initialize(texture_use, region, aspects);
        if (!upload && covers_buffer) buffer_use->initialized = true;
    }
    return doe_mtl_native_record_result(commands, status, &detail, error);
}

doe_mtl_status doe_mtl_commands_upload_texture(doe_mtl_commands *commands,
    doe_mtl_buffer *source, doe_mtl_texture_buffer_layout layout,
    doe_mtl_texture *destination, doe_mtl_texture_region region, doe_mtl_error *error) {
    return buffer_texture_transfer(commands, source, layout, destination, region, DOE_MTL_TRANSFER_ALL, true, error);
}
doe_mtl_status doe_mtl_commands_read_texture(doe_mtl_commands *commands,
    doe_mtl_texture *source, doe_mtl_texture_region region,
    doe_mtl_buffer *destination, doe_mtl_texture_buffer_layout layout, doe_mtl_error *error) {
    return buffer_texture_transfer(commands, destination, layout, source, region, DOE_MTL_TRANSFER_ALL, false, error);
}

doe_mtl_status doe_mtl_commands_upload_texture_aspect(doe_mtl_commands *commands,
    doe_mtl_buffer *source, doe_mtl_texture_buffer_layout layout, doe_mtl_texture *destination,
    doe_mtl_texture_region region, doe_mtl_transfer_aspect aspect, doe_mtl_error *error) {
    return buffer_texture_transfer(commands, source, layout, destination, region, aspect, true, error);
}
doe_mtl_status doe_mtl_commands_read_texture_aspect(doe_mtl_commands *commands,
    doe_mtl_texture *source, doe_mtl_texture_region region, doe_mtl_transfer_aspect aspect,
    doe_mtl_buffer *destination, doe_mtl_texture_buffer_layout layout, doe_mtl_error *error) {
    return buffer_texture_transfer(commands, destination, layout, source, region, aspect, false, error);
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
    if (status == DOE_MTL_OK && (source->format.depth || source->format.stencil || source->desc.sample_count > 1) && !full)
        status = DOE_MTL_UNSUPPORTED;
    if (status == DOE_MTL_OK) status = doe_mtl_texture_region_check(destination, destination_region, &full);
    if (status == DOE_MTL_OK && (destination->format.depth || destination->format.stencil || destination->desc.sample_count > 1) && !full)
        status = DOE_MTL_UNSUPPORTED;
    /* View compatibility also includes combined depth/stencil aspect views;
     * a copy must retain all aspects and otherwise only changes sRGB encoding. */
    if (status == DOE_MTL_OK && (source->desc.sample_count != destination->desc.sample_count ||
        !doe_mtl_format_compatible(source->desc.format, destination->desc.format) ||
        source->format.depth != destination->format.depth || source->format.stencil != destination->format.stencil ||
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
