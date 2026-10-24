/* SPDX-License-Identifier: MIT */
#include "d3d12_texture.h"
#include "d3d12_texture_desc.h"

#if defined(_WIN32)
static native_failure buffer_texture(native_command_handle *commands,const doe_resource_command *record)
{
    const doe_resource_texture_copy *copy = &record->value.texture.copy;
    if (!copy->width || !copy->height || !copy->depth_or_layers) return native_failure_status(NATIVE_OK);
    doe_resource_texture_copy normalized = *copy;
    normalized.aspect = record->value.texture.region.aspect;
    doe_dx_texture_region region;
    if (!native_d3d12_texture_region(&normalized,&region)) return native_failure_status(NATIVE_UNSUPPORTED);
    native_texture *texture = doe_resource_texture_native(record->value.texture.texture);
    native_buffer *buffer = doe_resource_buffer_native(record->value.texture.buffer);
    const doe_resource_copy_region *public = &record->value.texture.region;
    doe_dx_texture_desc desc;
    doe_dx_texture_footprint footprint;
    doe_dx_status status = doe_dx_texture_description(texture->d3d12,&desc);
    if (status == DOE_DX_OK)
        status = doe_dx_texture_region_footprint(&desc,region,(doe_dx_texture_layout){0},&footprint);
    if (status != DOE_DX_OK) return native_failure_d3d12(status,NULL);
    if (footprint.span > native_allocation_bound(texture->device)) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    doe_dx_buffer *zero = NULL, *scratch = NULL;
    doe_dx_error error = {0};
    status = doe_dx_buffer_create(commands->value.d3d12.context,footprint.span,DOE_DX_UPLOAD,&zero,&error);
    if (status == DOE_DX_OK)
        status = doe_dx_buffer_create(commands->value.d3d12.context,footprint.span,DOE_DX_DEFAULT,&scratch,&error);
    if (status == DOE_DX_OK)
        status = doe_dx_commands_copy(commands->value.d3d12.commands,zero,0,scratch,0,footprint.span,&error);
    const bool upload = record->value.texture.direction == DOE_RESOURCE_BUFFER_TO_TEXTURE;
    if (status == DOE_DX_OK && !upload)
        status = doe_dx_commands_texture_region(commands->value.d3d12.commands,scratch,
            (doe_dx_texture_layout){0},texture->d3d12,region,false,&error);
    /* Canonical rows and ranges have already been admitted. Only occupied bytes
     * move; public pitch/offset/padding are never widened to native placement. */
    for (uint32_t row = 0; status == DOE_DX_OK && row < copy->height; ++row) {
        uint64_t public_offset = copy->offset + (uint64_t)row * public->pitch;
        uint64_t native_offset = (uint64_t)row * footprint.row_pitch;
        status = upload ? doe_dx_commands_copy(commands->value.d3d12.commands,
            buffer->handle.d3d12->allocation,public_offset,scratch,native_offset,public->row_bytes,&error) :
            doe_dx_commands_copy(commands->value.d3d12.commands,scratch,native_offset,
                buffer->handle.d3d12->allocation,public_offset,public->row_bytes,&error);
    }
    if (status == DOE_DX_OK && upload)
        status = doe_dx_commands_texture_region(commands->value.d3d12.commands,scratch,
            (doe_dx_texture_layout){0},texture->d3d12,region,true,&error);
    native_failure failure = native_failure_d3d12(status,&error);
    doe_dx_buffer_release(zero);
    doe_dx_buffer_release(scratch);
    return failure;
}
#endif

native_failure native_d3d12_texture_copy(native_command_handle *commands,const doe_resource_command *record)
{
#if defined(_WIN32)
    if (record->kind == DOE_COMMAND_BUFFER_TEXTURE) return buffer_texture(commands,record);
    if (record->kind != DOE_COMMAND_COPY_TEXTURE) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const doe_resource_texture_blit *copy = &record->value.blit;
    if (!copy->width || !copy->height || !copy->depth_or_layers) return native_failure_status(NATIVE_OK);
    if (copy->source.mip || copy->destination.mip || copy->depth_or_layers != 1 ||
        copy->source.origin[2] || copy->destination.origin[2] || copy->source.aspect != WGPUTextureAspect_All ||
        copy->destination.aspect != WGPUTextureAspect_All) return native_failure_status(NATIVE_UNSUPPORTED);
    native_texture *source = doe_resource_texture_native(copy->source.texture);
    native_texture *destination = doe_resource_texture_native(copy->destination.texture);
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_commands_blit_texture(commands->value.d3d12.commands,source->d3d12,
        (doe_dx_texture_region){copy->source.origin[0],copy->source.origin[1],copy->width,copy->height},
        destination->d3d12,copy->destination.origin[0],copy->destination.origin[1],&error);
    return native_failure_d3d12(status,&error);
#else
    (void)commands;
    (void)record;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
