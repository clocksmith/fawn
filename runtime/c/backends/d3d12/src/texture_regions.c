/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "../include/doe_d3d12_texture_regions.h"

static bool whole(const doe_dx_texture_desc *desc, doe_dx_texture_region region)
{
    return !region.x && !region.y && region.width == desc->width && region.height == desc->height;
}

doe_dx_status doe_dx_texture_region_footprint(const doe_dx_texture_desc *texture,
    doe_dx_texture_region region, doe_dx_texture_layout layout, doe_dx_texture_footprint *out)
{
    if (!texture || !out || !region.width || !region.height) return DOE_DX_INVALID_ARGUMENT;
    if (!doe_dx_format_valid(texture->format) || texture->format == DOE_DX_D32_FLOAT) return DOE_DX_UNSUPPORTED;
    if (region.x > texture->width || region.width > texture->width - region.x ||
        region.y > texture->height || region.height > texture->height - region.y) return DOE_DX_RANGE;
    doe_dx_texture_desc span = *texture;
    span.width = region.width;
    span.height = region.height;
    return doe_dx_texture_layout_2d(&span,layout,out);
}

doe_dx_status doe_dx_commands_texture_region(doe_dx_commands *commands,
    doe_dx_buffer *buffer, doe_dx_texture_layout layout, doe_dx_texture *texture,
    doe_dx_texture_region region, bool upload, doe_dx_error *error)
{
    doe_dx_status status = doe_dx_recording(commands,error);
    if (status != DOE_DX_OK) return status;
    status = doe_dx_buffer_preflight(commands,buffer,upload);
    if (status == DOE_DX_OK)
        status = doe_dx_texture_preflight(commands,texture,
            upload ? DOE_DX_TEXTURE_COPY_DST : DOE_DX_TEXTURE_COPY_SRC,!upload);
    if (status != DOE_DX_OK) return doe_dx_fail(status,0,"texture.region.resources",error);
    if (buffer->memory != DOE_DX_DEFAULT && buffer->memory != (upload ? DOE_DX_UPLOAD : DOE_DX_READBACK))
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"texture.region.heap",error);
    doe_dx_texture_footprint footprint;
    status = doe_dx_texture_region_footprint(&texture->desc,region,layout,&footprint);
    if (status != DOE_DX_OK) return doe_dx_fail(status,0,"texture.region.layout",error);
    if (!doe_dx_range(buffer->size,footprint.offset,footprint.span))
        return doe_dx_fail(DOE_DX_RANGE,0,"texture.region.capacity",error);
    bool entire = whole(&texture->desc,region);
    bool covers_buffer = !footprint.offset && footprint.span == buffer->size &&
        (footprint.rows == 1 || footprint.row_pitch == footprint.row_bytes);
    if (upload && !entire)
        status = doe_dx_texture_preflight(commands,texture,DOE_DX_TEXTURE_COPY_DST,true);
    else if (!upload && !covers_buffer) status = doe_dx_buffer_preflight(commands,buffer,true);
    if (status != DOE_DX_OK) return doe_dx_fail(status,0,"texture.region.initialization",error);
    doe_dx_reservation needed = {.buffers=doe_dx_find_buffer(commands,buffer) ? 0u : 1u,
        .textures=doe_dx_find_texture(commands,texture) ? 0u : 1u};
    status = doe_dx_reserve_uses(commands,needed,error);
    if (status != DOE_DX_OK) return status;
    doe_dx_use_buffer(commands,buffer);
    doe_dx_use_texture(commands,texture);
    doe_dx_native_texture_region_transfer(commands->native,buffer->native,texture->native,
        &footprint,region,upload);
    if (upload && entire) doe_dx_find_texture(commands,texture)->initialized = true;
    if (!upload && covers_buffer) doe_dx_find_buffer(commands,buffer)->initialized = true;
    return doe_dx_fail(DOE_DX_OK,0,"texture.region",error);
}

doe_dx_status doe_dx_commands_blit_texture(doe_dx_commands *commands,
    doe_dx_texture *source, doe_dx_texture_region from, doe_dx_texture *destination,
    uint32_t destination_x, uint32_t destination_y, doe_dx_error *error)
{
    doe_dx_status status = doe_dx_recording(commands,error);
    if (status != DOE_DX_OK) return status;
    status = doe_dx_texture_preflight(commands,source,DOE_DX_TEXTURE_COPY_SRC,true);
    if (status == DOE_DX_OK)
        status = doe_dx_texture_preflight(commands,destination,DOE_DX_TEXTURE_COPY_DST,true);
    if (status != DOE_DX_OK) return doe_dx_fail(status,0,"texture.blit.resources",error);
    if (source == destination || source->desc.format != destination->desc.format)
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"texture.blit.alias_format",error);
    doe_dx_texture_footprint checked;
    status = doe_dx_texture_region_footprint(&source->desc,from,(doe_dx_texture_layout){0},&checked);
    doe_dx_texture_region to = {destination_x,destination_y,from.width,from.height};
    if (status == DOE_DX_OK)
        status = doe_dx_texture_region_footprint(&destination->desc,to,(doe_dx_texture_layout){0},&checked);
    if (status != DOE_DX_OK) return doe_dx_fail(status,0,"texture.blit.region",error);
    doe_dx_reservation needed = {.textures=(doe_dx_find_texture(commands,source) ? 0u : 1u) +
        (doe_dx_find_texture(commands,destination) ? 0u : 1u)};
    status = doe_dx_reserve_uses(commands,needed,error);
    if (status != DOE_DX_OK) return status;
    doe_dx_use_texture(commands,source);
    doe_dx_use_texture(commands,destination);
    doe_dx_native_texture_blit(commands->native,source->native,from,destination->native,destination_x,destination_y);
    return doe_dx_fail(DOE_DX_OK,0,"texture.blit",error);
}
