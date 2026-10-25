/* SPDX-License-Identifier: MIT */
#include "internal.h"

static doe_dx_status region_shape(const doe_dx_texture *texture,doe_dx_texture_region_ex region,doe_dx_texture_desc *out)
{
    if (!texture || region.mip>=texture->shape.mip_levels || region.aspect>2 || !region.rect.width || !region.rect.height)
        return DOE_DX_INVALID_ARGUMENT;
    doe_dx_texture_desc desc=texture->desc;
    desc.width>>=region.mip; if (!desc.width) desc.width=1;
    desc.height>>=region.mip; if (!desc.height) desc.height=1;
    doe_dx_format_info info;
    if (!doe_dx_format_properties(desc.format,&info)) return DOE_DX_UNSUPPORTED;
    desc.width=(desc.width+info.block_width-1)/info.block_width*info.block_width;
    desc.height=(desc.height+info.block_height-1)/info.block_height*info.block_height;
    uint32_t depth=texture->shape.depth_or_layers;
    if (texture->shape.dimension==3) { depth>>=region.mip; if (!depth) depth=1; }
    if ((texture->shape.dimension==3 && (region.layer || region.z>=depth)) ||
        (texture->shape.dimension!=3 && (region.z || region.layer>=depth)) ||
        region.rect.x>desc.width || region.rect.width>desc.width-region.rect.x ||
        region.rect.y>desc.height || region.rect.height>desc.height-region.rect.y)
        return DOE_DX_RANGE;
    if ((region.aspect==1 && !doe_dx_format_depth(desc.format)) ||
        (region.aspect==2 && !doe_dx_format_stencil(desc.format))) return DOE_DX_INVALID_ARGUMENT;
    *out=desc;
    return DOE_DX_OK;
}
static bool whole(const doe_dx_texture_desc *desc,doe_dx_texture_region_ex region)
{
    return !region.rect.x && !region.rect.y && region.rect.width==desc->width && region.rect.height==desc->height;
}
doe_dx_status doe_dx_texture_region_layout(const doe_dx_texture *texture,doe_dx_texture_region_ex region,
    doe_dx_texture_layout layout,doe_dx_texture_footprint *out)
{
    doe_dx_texture_desc desc;
    doe_dx_status status=region_shape(texture,region,&desc);
    if (status!=DOE_DX_OK) return status;
    if (texture->samples!=1) return DOE_DX_UNSUPPORTED;
    if (doe_dx_format_depth(desc.format)) {
        /* A buffer footprint describes exactly one plane, never an implicit
         * depth-only projection of a combined depth/stencil All selection. */
        if (doe_dx_format_stencil(desc.format) && !region.aspect) return DOE_DX_INVALID_ARGUMENT;
        if (!whole(&desc,region)) return DOE_DX_UNSUPPORTED;
        if (region.aspect==2) desc.format=DOE_DX_R8_UINT;
        else if (desc.format==DOE_DX_D16_UNORM) desc.format=DOE_DX_R16_UNORM;
        else if (desc.format==DOE_DX_D32_FLOAT || desc.format==DOE_DX_D32_FLOAT_S8_UINT) desc.format=DOE_DX_R32_FLOAT;
        else return DOE_DX_UNSUPPORTED; /* WebGPU depth24plus has no byte-copy representation. */
    }
    return doe_dx_texture_region_footprint(&desc,region.rect,layout,out);
}
doe_dx_status doe_dx_commands_texture_region_ex(doe_dx_commands *commands,doe_dx_buffer *buffer,
    doe_dx_texture_layout layout,doe_dx_texture *texture,doe_dx_texture_region_ex region,bool upload,doe_dx_error *error)
{
    doe_dx_status status=doe_dx_recording(commands,error);
    if (status!=DOE_DX_OK) return status;
    status=doe_dx_buffer_preflight(commands,buffer,upload);
    if (status==DOE_DX_OK) status=doe_dx_texture_preflight(commands,texture,
        upload ? DOE_DX_TEXTURE_COPY_DST : DOE_DX_TEXTURE_COPY_SRC,true);
    if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"texture.subresource.resources",error);
    if (buffer->memory!=DOE_DX_DEFAULT && buffer->memory!=(upload ? DOE_DX_UPLOAD : DOE_DX_READBACK))
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"texture.subresource.heap",error);
    doe_dx_texture_footprint footprint;
    status=doe_dx_texture_region_layout(texture,region,layout,&footprint);
    if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"texture.subresource.layout",error);
    if (!doe_dx_range(buffer->size,footprint.offset,footprint.span)) return doe_dx_fail(DOE_DX_RANGE,0,"texture.subresource.span",error);
    bool full_buffer=!footprint.offset && footprint.span==buffer->size && (footprint.rows==1 || footprint.row_pitch==footprint.row_bytes);
    if (!upload && !full_buffer) {
        status=doe_dx_buffer_preflight(commands,buffer,true);
        if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"texture.subresource.padding",error);
    }
    doe_dx_reservation needed={.buffers=doe_dx_find_buffer(commands,buffer) ? 0u : 1u,
        .textures=doe_dx_find_texture(commands,texture) ? 0u : 1u};
    status=doe_dx_reserve_uses(commands,needed,error);
    if (status!=DOE_DX_OK) return status;
    doe_dx_use_buffer(commands,buffer); doe_dx_use_texture(commands,texture);
    doe_dx_native_texture_region_transfer_ex(commands->native,buffer->native,texture->native,&footprint,region,upload);
    if (!upload && full_buffer) doe_dx_find_buffer(commands,buffer)->initialized=true;
    return doe_dx_fail(DOE_DX_OK,0,"texture.subresource",error);
}
doe_dx_status doe_dx_commands_blit_texture_ex(doe_dx_commands *commands,doe_dx_texture *source,
    doe_dx_texture_region_ex from,doe_dx_texture *destination,doe_dx_texture_region_ex to,doe_dx_error *error)
{
    doe_dx_status status=doe_dx_recording(commands,error);
    if (status!=DOE_DX_OK) return status;
    status=doe_dx_texture_preflight(commands,source,DOE_DX_TEXTURE_COPY_SRC,true);
    if (status==DOE_DX_OK) status=doe_dx_texture_preflight(commands,destination,DOE_DX_TEXTURE_COPY_DST,true);
    if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"texture.subresource_copy.resources",error);
    doe_dx_texture_desc a,b;
    status=region_shape(source,from,&a);
    if (status==DOE_DX_OK) status=region_shape(destination,to,&b);
    if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"texture.subresource_copy.region",error);
    if (!doe_dx_format_compatible(a.format,b.format) || source->samples!=destination->samples || from.aspect!=to.aspect ||
        from.rect.width!=to.rect.width || from.rect.height!=to.rect.height)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"texture.subresource_copy.shape",error);
    if ((source->samples>1 || doe_dx_format_depth(a.format)) && (!whole(&a,from) || !whole(&b,to)))
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"texture.subresource_copy.whole",error);
    if (!doe_dx_format_depth(a.format) && source->samples==1) {
        doe_dx_texture_footprint checked;
        status=doe_dx_texture_region_layout(source,from,(doe_dx_texture_layout){0},&checked);
        if (status==DOE_DX_OK) status=doe_dx_texture_region_layout(destination,to,(doe_dx_texture_layout){0},&checked);
        if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"texture.subresource_copy.blocks",error);
    }
    if (source==destination && from.mip==to.mip && from.layer==to.layer) {
        bool overlap=from.z==to.z && from.rect.x<to.rect.x+to.rect.width && to.rect.x<from.rect.x+from.rect.width &&
            from.rect.y<to.rect.y+to.rect.height && to.rect.y<from.rect.y+from.rect.height;
        if (overlap) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"texture.subresource_copy.overlap",error);
        if (source->samples!=1 || doe_dx_format_depth(a.format))
            return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"texture.subresource_copy.scratch_format",error);
        doe_dx_texture_desc desc={from.rect.width,from.rect.height,a.format,DOE_DX_TEXTURE_COPY_SRC|DOE_DX_TEXTURE_COPY_DST};
        doe_dx_texture *scratch=NULL;
        status=doe_dx_texture_create(commands->context,&desc,&scratch,error);
        if (status!=DOE_DX_OK) return status;
        doe_dx_reservation reserve={.textures=1u+(doe_dx_find_texture(commands,source) ? 0u : 1u)};
        status=doe_dx_reserve_uses(commands,reserve,error);
        if (status==DOE_DX_OK) {
            /* Exact full scratch copy initializes every texel before it is read;
             * no zero bit, CPU pixel copy or untracked temporary allocation. */
            doe_dx_use_texture(commands,source); doe_dx_use_texture(commands,scratch);
            doe_dx_texture_region_ex whole_scratch={.rect={0,0,from.rect.width,from.rect.height}};
            doe_dx_native_texture_blit_ex(commands->native,source->native,from,scratch->native,whole_scratch);
            doe_dx_find_texture(commands,scratch)->initialized=true;
            doe_dx_native_texture_blit_ex(commands->native,scratch->native,whole_scratch,destination->native,to);
        }
        doe_dx_texture_release(scratch);
        return status;
    }
    doe_dx_reservation needed={.textures=(doe_dx_find_texture(commands,source) ? 0u : 1u)+
        (source!=destination && !doe_dx_find_texture(commands,destination) ? 1u : 0u)};
    status=doe_dx_reserve_uses(commands,needed,error);
    if (status!=DOE_DX_OK) return status;
    doe_dx_use_texture(commands,source); doe_dx_use_texture(commands,destination);
    doe_dx_native_texture_blit_ex(commands->native,source->native,from,destination->native,to);
    return doe_dx_fail(DOE_DX_OK,0,"texture.subresource_copy",error);
}
