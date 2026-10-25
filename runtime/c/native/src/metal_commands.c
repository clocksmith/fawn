/* SPDX-License-Identifier: MIT */
#include "metal_graphics_internal.h"
#include "metal_indirect.h"

static doe_mtl_status initialize(doe_mtl_commands *commands, native_texture *texture, doe_mtl_error *error)
{
    const doe_mtl_texture_desc *desc = doe_mtl_texture_description(texture->metal);
    if (!(desc->usage & DOE_MTL_TEXTURE_RENDER_TARGET)) return DOE_MTL_OK;
    doe_mtl_texture_view_desc span = {.type=desc->type, .format=desc->format,
        .mip_count=desc->mip_levels, .layer_count=desc->depth_or_layers,
        .red=DOE_MTL_SWIZZLE_RED, .green=DOE_MTL_SWIZZLE_GREEN,
        .blue=DOE_MTL_SWIZZLE_BLUE, .alpha=DOE_MTL_SWIZZLE_ALPHA};
    doe_mtl_texture_view *view = NULL;
    doe_mtl_status status = doe_mtl_texture_view_create(texture->metal, &span, &view, error);
    if (status == DOE_MTL_OK) status = doe_mtl_commands_initialize_attachment(commands, view, error);
    doe_mtl_texture_view_release(view);
    return status;
}

static native_failure transfer(doe_mtl_commands *commands, const doe_resource_command *record)
{
    const doe_resource_texture_copy *copy = &record->value.texture.copy;
    if (!copy->width || !copy->height || !copy->depth_or_layers) return native_failure_status(NATIVE_OK);
    native_buffer *buffer = doe_resource_buffer_native(record->value.texture.buffer);
    native_texture *texture = doe_resource_texture_native(record->value.texture.texture);
    doe_mtl_error error = {0};
    doe_mtl_status status = initialize(commands, texture, &error);
    doe_mtl_texture_region region = {copy->mip, copy->origin[0], copy->origin[1], copy->origin[2],
        copy->width, copy->height, copy->depth_or_layers};
    uint32_t pitch = record->value.texture.region.pitch;
    if (pitch % 256) {
        /* Only one row/image has no native stride obligation. The backend
         * checks its true copied tail, not imaginary trailing row padding. */
        if (copy->height != 1 || copy->depth_or_layers != 1) return native_failure_status(NATIVE_UNSUPPORTED);
        pitch = 0;
    }
    doe_mtl_texture_buffer_layout layout = {copy->offset, pitch, record->value.texture.region.image_rows};
    if (status == DOE_MTL_OK) status = record->value.texture.direction == DOE_RESOURCE_BUFFER_TO_TEXTURE ?
        doe_mtl_commands_upload_texture(commands, buffer->handle.metal, layout, texture->metal, region, &error) :
        doe_mtl_commands_read_texture(commands, texture->metal, region, buffer->handle.metal, layout, &error);
    return native_failure_metal(status, &error);
}

native_failure native_metal_record_commands(doe_mtl_commands *commands, doe_resource_commands *resources)
{
    size_t count = 0;
    const doe_resource_command *records = doe_resource_commands_borrow(resources, &count);
    for (size_t i = 0; i < count; ++i) {
        const doe_resource_command *record = &records[i];
        doe_mtl_status status = DOE_MTL_OK;
        doe_mtl_error error = {0};
        native_failure failure = native_failure_status(NATIVE_OK);
        switch (record->kind) {
        case DOE_COMMAND_COPY_BUFFER: {
            if (!record->value.copy.size) continue;
            native_buffer *source = doe_resource_buffer_native(record->value.copy.source);
            native_buffer *destination = doe_resource_buffer_native(record->value.copy.destination);
            status = doe_mtl_commands_copy(commands, source->handle.metal, record->value.copy.source_offset,
                destination->handle.metal, record->value.copy.destination_offset, record->value.copy.size, &error);
            break;
        }
        case DOE_COMMAND_CLEAR_BUFFER: {
            if (!record->value.clear.size) continue;
            native_buffer *buffer = doe_resource_buffer_native(record->value.clear.buffer);
            status = doe_mtl_commands_fill(commands, buffer->handle.metal, record->value.clear.offset,
                record->value.clear.size, 0, &error);
            break;
        }
        case DOE_COMMAND_DISPATCH:
            failure = native_metal_dispatch(commands, record);
            break;
        case DOE_COMMAND_DISPATCH_INDIRECT:
            failure = native_metal_dispatch_indirect(commands, record);
            break;
        case DOE_COMMAND_BUFFER_TEXTURE:
            failure = transfer(commands, record);
            break;
        case DOE_COMMAND_COPY_TEXTURE: {
            const doe_resource_texture_blit *copy = &record->value.blit;
            if (!copy->width || !copy->height || !copy->depth_or_layers) continue;
            native_texture *source = doe_resource_texture_native(copy->source.texture);
            native_texture *destination = doe_resource_texture_native(copy->destination.texture);
            status = initialize(commands, source, &error);
            if (status == DOE_MTL_OK) status = initialize(commands, destination, &error);
            if (status == DOE_MTL_OK) status = doe_mtl_commands_copy_texture(commands, source->metal,
                (doe_mtl_texture_region){copy->source.mip,copy->source.origin[0],copy->source.origin[1],
                    copy->source.origin[2],copy->width,copy->height,copy->depth_or_layers}, destination->metal,
                (doe_mtl_texture_region){copy->destination.mip,copy->destination.origin[0],copy->destination.origin[1],
                    copy->destination.origin[2],copy->width,copy->height,copy->depth_or_layers}, &error);
            break;
        }
        case DOE_COMMAND_WRITE_TIMESTAMP:
        case DOE_COMMAND_RESOLVE_QUERY:
            failure = native_metal_record_query(commands, record);
            break;
        case DOE_COMMAND_RENDER: {
            size_t consumed = 0;
            failure = native_metal_render(commands, records + i, count - i, &consumed);
            if (failure.status == NATIVE_OK) i += consumed - 1;
            break;
        }
        default:
            return native_failure_status(NATIVE_UNSUPPORTED);
        }
        if (failure.status != NATIVE_OK) return failure;
        if (status != DOE_MTL_OK) return native_failure_metal(status, &error);
    }
    return native_failure_status(NATIVE_OK);
}
