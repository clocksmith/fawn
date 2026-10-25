/* SPDX-License-Identifier: MIT */
#include "internal.h"

static doe_dx_status transfer(doe_dx_commands *commands, doe_dx_buffer *buffer,
    doe_dx_texture_layout layout, doe_dx_texture *texture, bool upload, doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    status = doe_dx_buffer_preflight(commands, buffer, upload);
    if (status == DOE_DX_OK)
        status = doe_dx_texture_preflight(commands, texture,
            upload ? DOE_DX_TEXTURE_COPY_DST : DOE_DX_TEXTURE_COPY_SRC, !upload);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "texture.transfer", error);
    if (buffer->memory != (upload ? DOE_DX_UPLOAD : DOE_DX_READBACK))
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.transfer.heap", error);
    doe_dx_texture_footprint footprint;
    status = doe_dx_texture_copy_footprint(texture, layout, &footprint);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "texture.transfer.layout", error);
    if (!doe_dx_range(buffer->size, footprint.offset, footprint.span))
        return doe_dx_fail(DOE_DX_RANGE, 0, "texture.transfer.capacity", error);
    bool covers_buffer = !footprint.offset && footprint.span == buffer->size &&
        (footprint.rows == 1 || footprint.row_pitch == footprint.row_bytes);
    if (!upload && !covers_buffer) {
        status = doe_dx_buffer_preflight(commands, buffer, true);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "texture.readback.padding", error);
    }
    doe_dx_reservation needed = {.buffers=doe_dx_find_buffer(commands, buffer) ? 0u : 1u,
        .textures=doe_dx_find_texture(commands, texture) ? 0u : 1u};
    status = doe_dx_reserve_uses(commands, needed, error);
    if (status != DOE_DX_OK) return status;
    doe_dx_use_buffer(commands, buffer);
    doe_dx_use_texture(commands, texture);
    doe_dx_native_texture_transfer(commands->native, buffer->native, texture->native, &footprint, upload);
    if (upload) doe_dx_find_texture(commands, texture)->initialized = true;
    else if (covers_buffer) doe_dx_find_buffer(commands, buffer)->initialized = true;
    return doe_dx_fail(DOE_DX_OK, 0, "texture.transfer", error);
}

doe_dx_status doe_dx_commands_upload_texture(doe_dx_commands *commands,
    doe_dx_buffer *source, doe_dx_texture_layout layout, doe_dx_texture *destination,
    doe_dx_error *error) {
    return transfer(commands, source, layout, destination, true, error);
}

doe_dx_status doe_dx_commands_read_texture(doe_dx_commands *commands,
    doe_dx_texture *source, doe_dx_buffer *destination, doe_dx_texture_layout layout,
    doe_dx_error *error) {
    return transfer(commands, destination, layout, source, false, error);
}

doe_dx_status doe_dx_commands_copy_texture(doe_dx_commands *commands,
    doe_dx_texture *source, doe_dx_texture *destination, doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    status = doe_dx_texture_preflight(commands, source, DOE_DX_TEXTURE_COPY_SRC, true);
    if (status == DOE_DX_OK)
        status = doe_dx_texture_preflight(commands, destination, DOE_DX_TEXTURE_COPY_DST, false);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "texture.copy", error);
    if (source == destination) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "texture.copy.alias", error);
    if (source->desc.width != destination->desc.width || source->desc.height != destination->desc.height ||
        source->desc.format != destination->desc.format || source->samples != destination->samples || source->shape.dimension!=destination->shape.dimension ||
        source->shape.depth_or_layers!=destination->shape.depth_or_layers || source->shape.mip_levels!=destination->shape.mip_levels)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.copy.shape", error);
    doe_dx_reservation needed = {.textures=(doe_dx_find_texture(commands, source) ? 0u : 1u) +
        (doe_dx_find_texture(commands, destination) ? 0u : 1u)};
    status = doe_dx_reserve_uses(commands, needed, error);
    if (status != DOE_DX_OK) return status;
    doe_dx_use_texture(commands, source);
    doe_dx_use_texture(commands, destination);
    doe_dx_native_texture_copy(commands->native, source->native, destination->native);
    doe_dx_find_texture(commands, destination)->initialized = true;
    return doe_dx_fail(DOE_DX_OK, 0, "texture.copy", error);
}
