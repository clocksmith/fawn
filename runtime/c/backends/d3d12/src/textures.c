/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "../../../common/include/doe_copy_footprint.h"

bool doe_dx_format_properties(doe_dx_format format,doe_dx_format_info *out) {
    switch (format) {
#define DOE_DX_FORMAT(name,public,sdk,bytes,block,kind) case DOE_DX_##name: *out=(doe_dx_format_info){bytes,block,block,kind}; return true;
#include "../include/doe_d3d12_formats.inc"
#undef DOE_DX_FORMAT
    }
    return false;
}
bool doe_dx_format_renderable(doe_dx_format format) {
    doe_dx_format_info info;
    return doe_dx_format_properties(format,&info) && info.kind<4 && info.block_width==1 && format!=DOE_DX_RGB9E5_FLOAT;
}
bool doe_dx_format_exact_clear(doe_dx_format format) {
    return format==DOE_DX_R32_UINT || format==DOE_DX_R32_SINT || format==DOE_DX_RG32_UINT ||
        format==DOE_DX_RG32_SINT || format==DOE_DX_RGBA32_UINT || format==DOE_DX_RGBA32_SINT;
}
bool doe_dx_format_depth(doe_dx_format format) {
    doe_dx_format_info info;
    return doe_dx_format_properties(format,&info) && info.kind>=4;
}
bool doe_dx_format_stencil(doe_dx_format format) {
    doe_dx_format_info info;
    return doe_dx_format_properties(format,&info) && info.kind==5;
}
bool doe_dx_format_valid(doe_dx_format format) {
    doe_dx_format_info info;
    return doe_dx_format_properties(format,&info);
}

doe_dx_status doe_dx_context_format_caps(const doe_dx_context *context,doe_dx_format format,
    doe_dx_format_caps *out,doe_dx_error *error)
{
    if (!context || !out || !doe_dx_format_valid(format))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"format_caps.arguments",error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST,0,"format_caps.device",error);
    doe_dx_format_caps result={0};
    doe_dx_status status=doe_dx_native_format_caps(context->native,format,&result,error);
    if (status==DOE_DX_OK) *out=result;
    return status;
}

doe_dx_status doe_dx_texture_create(doe_dx_context *context,
    const doe_dx_texture_desc *desc,doe_dx_texture **out,doe_dx_error *error) {
    return doe_dx_texture_create_samples(context,desc,1,out,error);
}
uint32_t doe_dx_texture_samples(const doe_dx_texture *texture) { return texture ? texture->samples : 0; }

doe_dx_status doe_dx_texture_create_samples(doe_dx_context *context,
    const doe_dx_texture_desc *desc,uint32_t samples,doe_dx_texture **out,doe_dx_error *error) {
    if (!desc) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"texture.create",error);
    doe_dx_texture_shape shape={.image=*desc,.dimension=2,.depth_or_layers=1,.mip_levels=1,.samples=samples};
    return doe_dx_texture_create_shape(context,&shape,out,error);
}
doe_dx_status doe_dx_texture_shape_description(const doe_dx_texture *texture,doe_dx_texture_shape *out) {
    if (!texture || !out) return DOE_DX_INVALID_ARGUMENT;
    *out=texture->shape; return DOE_DX_OK;
}
doe_dx_status doe_dx_texture_create_shape(doe_dx_context *context,const doe_dx_texture_shape *shape,
    doe_dx_texture **out,doe_dx_error *error) {
    if (!shape) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"texture.shape",error);
    const doe_dx_texture_desc *desc=&shape->image;
    uint32_t samples=shape->samples;
    if (!context || !desc || !out || !desc->width || !desc->height || !desc->usage)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "texture.create", error);
    if (shape->dimension<1 || shape->dimension>3 || !shape->depth_or_layers || !shape->mip_levels ||
        shape->mip_levels>15 || (shape->dimension==1 && (desc->height!=1 || shape->depth_or_layers!=1)) ||
        (shape->dimension==3 && (desc->width>DOE_DX_MAX_TEXTURE_DIMENSION_3D || desc->height>DOE_DX_MAX_TEXTURE_DIMENSION_3D ||
            shape->depth_or_layers>DOE_DX_MAX_TEXTURE_DIMENSION_3D)) ||
        (shape->dimension==2 && shape->depth_or_layers>DOE_DX_MAX_TEXTURE_ARRAY_LAYERS) ||
        (samples>1 && (shape->dimension!=2 || shape->mip_levels!=1 || shape->depth_or_layers!=1)))
        return doe_dx_fail(DOE_DX_RANGE,0,"texture.shape",error);
    uint32_t maximum=desc->width>desc->height ? desc->width : desc->height;
    if (shape->dimension==3 && shape->depth_or_layers>maximum) maximum=shape->depth_or_layers;
    uint32_t mip_count=1;
    while (maximum>1) { maximum>>=1; ++mip_count; }
    if (shape->mip_levels>mip_count) return doe_dx_fail(DOE_DX_RANGE,0,"texture.mip_levels",error);
    if ((samples!=1 && samples!=4) || (samples>1 &&
        ((desc->usage & DOE_DX_TEXTURE_STORAGE) || !(desc->usage &
        (DOE_DX_TEXTURE_RENDER_ATTACHMENT|DOE_DX_TEXTURE_DEPTH_ATTACHMENT)))))
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"texture.samples",error);
    if (!doe_dx_format_valid(desc->format) || (desc->usage & ~UINT32_C(63)))
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.format_usage", error);
    if ((desc->usage & DOE_DX_TEXTURE_RENDER_ATTACHMENT) &&
        !doe_dx_format_renderable(desc->format))
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.render_format", error);
    if (doe_dx_format_depth(desc->format)) {
        if (!(desc->usage & DOE_DX_TEXTURE_DEPTH_ATTACHMENT) || (desc->usage & ~UINT32_C(39)))
            return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.depth_usage", error);
    } else if (desc->usage & DOE_DX_TEXTURE_DEPTH_ATTACHMENT)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.depth_format", error);
    if (desc->width > DOE_DX_MAX_TEXTURE_DIMENSION_2D || desc->height > DOE_DX_MAX_TEXTURE_DIMENSION_2D || context->objects == SIZE_MAX)
        return doe_dx_fail(DOE_DX_RANGE, 0, "texture.extent", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "texture.create", error);
    doe_dx_texture *texture = calloc(1, sizeof(*texture));
    if (!texture) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "texture.allocate", error);
    doe_dx_status status = doe_dx_native_texture_create(context->native, shape, &texture->native, error);
    if (status != DOE_DX_OK) {
        free(texture);
        return doe_dx_context_result(context, status, error);
    }
    texture->context = context;
    texture->desc = *desc;
    texture->shape = *shape;
    texture->samples = samples;
    texture->references = 1;
    context->objects++;
    *out = texture;
    return doe_dx_fail(DOE_DX_OK, 0, "texture.create", error);
}

doe_dx_status doe_dx_texture_retain(doe_dx_texture *texture) {
    if (!texture) return DOE_DX_INVALID_ARGUMENT;
    if (texture->references == UINT32_MAX) return DOE_DX_RANGE;
    texture->references++;
    return DOE_DX_OK;
}

void doe_dx_texture_release(doe_dx_texture *texture) {
    if (!texture) return;
    texture->references--;
    if (texture->references) return;
    doe_dx_native_texture_destroy(texture->native);
    texture->context->objects--;
    if (texture->surface) doe_dx_surface_texture_release(texture->surface,texture);
    free(texture);
}

doe_dx_status doe_dx_texture_description(const doe_dx_texture *texture, doe_dx_texture_desc *out) {
    if (!texture || !out) return DOE_DX_INVALID_ARGUMENT;
    *out = texture->desc;
    return DOE_DX_OK;
}

doe_dx_status doe_dx_texture_layout_2d(const doe_dx_texture_desc *desc,
    doe_dx_texture_layout layout, doe_dx_texture_footprint *out) {
    if (!desc || !out || !desc->width || !desc->height) return DOE_DX_INVALID_ARGUMENT;
    if (!doe_dx_format_valid(desc->format)) return DOE_DX_UNSUPPORTED;
    if (desc->width > DOE_DX_MAX_TEXTURE_DIMENSION_2D || desc->height > DOE_DX_MAX_TEXTURE_DIMENSION_2D) return DOE_DX_RANGE;
    if (layout.offset % 512 || layout.row_pitch % 256) return DOE_DX_ALIGNMENT;
    if (doe_dx_format_stencil(desc->format)) return DOE_DX_UNSUPPORTED; /* Explicit plane transfers need their own selected span. */
    doe_dx_format_info info;
    if (!doe_dx_format_properties(desc->format,&info)) return DOE_DX_UNSUPPORTED;
    uint32_t row_bytes = (desc->width+info.block_width-1)/info.block_width*info.bytes;
    uint32_t rows=(desc->height+info.block_height-1)/info.block_height;
    uint32_t pitch = layout.row_pitch ? layout.row_pitch : (row_bytes + 255u) / 256u * 256u;
    if (pitch < row_bytes) return DOE_DX_RANGE;
    uint64_t span;
    if (!doe_copy_footprint(row_bytes, rows, rows,
        pitch, 1, &span) || span > SIZE_MAX || layout.offset > SIZE_MAX - span)
        return DOE_DX_RANGE;
    *out = (doe_dx_texture_footprint){layout.offset, span, pitch, row_bytes, rows};
    return DOE_DX_OK;
}

doe_dx_status doe_dx_texture_copy_footprint(const doe_dx_texture *texture,
    doe_dx_texture_layout layout, doe_dx_texture_footprint *out) {
    if (!texture) return DOE_DX_INVALID_ARGUMENT;
    if (texture->samples!=1 || texture->shape.mip_levels!=1 || texture->shape.depth_or_layers!=1) return DOE_DX_UNSUPPORTED;
    return doe_dx_texture_layout_2d(&texture->desc, layout, out);
}

doe_dx_texture_use *doe_dx_find_texture(doe_dx_commands *commands, doe_dx_texture *texture) {
    if (!texture || texture->owner != commands) return NULL;
    size_t index = texture->owner_index;
    if (index >= commands->texture_count || commands->textures[index].texture != texture) abort();
    return &commands->textures[index];
}

doe_dx_status doe_dx_texture_preflight(doe_dx_commands *commands,
    doe_dx_texture *texture, uint32_t usage, bool readable) {
    if (!texture) return DOE_DX_INVALID_ARGUMENT;
    if (!doe_dx_surface_texture_recordable(texture)) return DOE_DX_INVALID_STATE;
    if (texture->context != commands->context) return DOE_DX_DEVICE_MISMATCH;
    if (texture->owner && texture->owner != commands) {
        commands->conflict = texture->owner;
        return DOE_DX_BUSY;
    }
    if ((texture->desc.usage & usage) != usage) return DOE_DX_UNSUPPORTED;
    doe_dx_texture_use *use = doe_dx_find_texture(commands, texture);
    if (!use && texture->references == UINT32_MAX) return DOE_DX_RANGE;
    if (readable && !(use ? use->initialized : texture->initialized)) return DOE_DX_UNINITIALIZED;
    return DOE_DX_OK;
}

void doe_dx_use_texture(doe_dx_commands *commands, doe_dx_texture *texture) {
    if (doe_dx_find_texture(commands, texture)) return;
    if (commands->texture_count >= commands->texture_capacity) abort();
    texture->owner_index = commands->texture_count;
    doe_dx_texture_use *use = &commands->textures[commands->texture_count++];
    use->surface_acquire = false;
    use->texture = texture;
    use->initialized = texture->initialized;
    texture->owner = commands;
    texture->references++;
}
