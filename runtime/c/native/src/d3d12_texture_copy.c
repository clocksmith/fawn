/* SPDX-License-Identifier: MIT */
#include "d3d12_texture.h"
#include "d3d12_texture_desc.h"

#if defined(_WIN32)
static bool region_span(const doe_resource_texture_desc *texture,uint32_t mip,const uint32_t origin[3],
    uint32_t width,uint32_t height,WGPUTextureAspect aspect,uint32_t image,doe_dx_texture_region_ex *out)
{
    doe_dx_texture_region_ex result={.rect={origin[0],origin[1],width,height},.mip=mip};
    switch (aspect) {
    case WGPUTextureAspect_All: break;
    case WGPUTextureAspect_DepthOnly: result.aspect=1; break;
    case WGPUTextureAspect_StencilOnly: result.aspect=2; break;
    default: return false;
    }
    if (texture->format==WGPUTextureFormat_Stencil8) result.aspect=2;
    if (image>UINT32_MAX-origin[2]) return false;
    if (texture->dimension==WGPUTextureDimension_3D) result.z=origin[2]+image;
    else result.layer=origin[2]+image;
    *out=result;
    return true;
}
static native_failure buffer_texture(native_command_handle *commands,const doe_resource_command *record)
{
    const doe_resource_texture_copy *copy = &record->value.texture.copy;
    if (!copy->width || !copy->height || !copy->depth_or_layers) return native_failure_status(NATIVE_OK);
    native_texture *texture = doe_resource_texture_native(record->value.texture.texture);
    native_buffer *buffer = doe_resource_buffer_native(record->value.texture.buffer);
    const doe_resource_copy_region *public = &record->value.texture.region;
    const doe_resource_texture_desc *desc=doe_resource_texture_description(record->value.texture.texture);
    doe_dx_texture_region_ex region;
    doe_dx_texture_footprint footprint={0};
    doe_dx_status status=DOE_DX_OK;
    /* Preflight every image before any staging copy is recorded. */
    for (uint32_t image=0;image<copy->depth_or_layers;++image) {
        if (!region_span(desc,copy->mip,copy->origin,copy->width,copy->height,public->aspect,image,&region))
            return native_failure_status(NATIVE_UNSUPPORTED);
        status=doe_dx_texture_region_layout(texture->d3d12,region,(doe_dx_texture_layout){0},&footprint);
        if (status!=DOE_DX_OK) break;
    }
    if (status != DOE_DX_OK) return native_failure_d3d12(status,NULL);
    if (footprint.span > native_allocation_bound(texture->device)) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    doe_dx_buffer *zero = NULL, *scratch = NULL;
    doe_dx_error error = {0};
    status = doe_dx_buffer_create(commands->value.d3d12.context,footprint.span,DOE_DX_UPLOAD,&zero,&error);
    if (status == DOE_DX_OK)
        status = doe_dx_buffer_create(commands->value.d3d12.context,footprint.span,DOE_DX_DEFAULT,&scratch,&error);
    if (status == DOE_DX_OK)
        status = doe_dx_commands_copy_bytes(commands->value.d3d12.commands,zero,0,scratch,0,footprint.span,&error);
    const bool upload = record->value.texture.direction == DOE_RESOURCE_BUFFER_TO_TEXTURE;
    for (uint32_t image=0;status==DOE_DX_OK && image<copy->depth_or_layers;++image) {
        if (!region_span(desc,copy->mip,copy->origin,copy->width,copy->height,public->aspect,image,&region)) {
            status=DOE_DX_INVALID_ARGUMENT; break;
        }
        if (!upload) status=doe_dx_commands_texture_region_ex(commands->value.d3d12.commands,scratch,
            (doe_dx_texture_layout){0},texture->d3d12,region,false,&error);
        /* Only occupied block rows move. No caller pitch/padding is widened to
         * native placement, including independent array and 3D image strides. */
        for (uint32_t row=0;status==DOE_DX_OK && row<public->rows;++row) {
            uint64_t public_offset=copy->offset+((uint64_t)image*public->image_rows+row)*public->pitch;
            uint64_t native_offset=(uint64_t)row*footprint.row_pitch;
            status=upload ? doe_dx_commands_copy_bytes(commands->value.d3d12.commands,
                buffer->handle.d3d12->allocation,public_offset,scratch,native_offset,public->row_bytes,&error) :
                doe_dx_commands_copy_bytes(commands->value.d3d12.commands,scratch,native_offset,
                    buffer->handle.d3d12->allocation,public_offset,public->row_bytes,&error);
        }
        if (status==DOE_DX_OK && upload)
            status=doe_dx_commands_texture_region_ex(commands->value.d3d12.commands,scratch,
                (doe_dx_texture_layout){0},texture->d3d12,region,true,&error);
    }
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
    native_texture *source=doe_resource_texture_native(copy->source.texture);
    native_texture *destination=doe_resource_texture_native(copy->destination.texture);
    const doe_resource_texture_desc *source_desc=doe_resource_texture_description(copy->source.texture);
    const doe_resource_texture_desc *destination_desc=doe_resource_texture_description(copy->destination.texture);
    doe_dx_error error={0};
    doe_dx_status status=DOE_DX_OK;
    for (uint32_t image=0;status==DOE_DX_OK && image<copy->depth_or_layers;++image) {
        doe_dx_texture_region_ex from,to;
        if (!region_span(source_desc,copy->source.mip,copy->source.origin,copy->width,copy->height,copy->source.aspect,image,&from) ||
            !region_span(destination_desc,copy->destination.mip,copy->destination.origin,copy->width,copy->height,copy->destination.aspect,image,&to))
            return native_failure_status(NATIVE_UNSUPPORTED);
        status=doe_dx_commands_blit_texture_ex(commands->value.d3d12.commands,source->d3d12,from,destination->d3d12,to,&error);
    }
    return native_failure_d3d12(status,&error);
#else
    (void)commands;
    (void)record;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
