/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <limits.h>
#include "../../common/include/doe_copy_footprint.h"

static uint64_t ceil_blocks(uint32_t value, uint32_t block)
{
    return ((uint64_t)value + block - 1) / block;
}

/* Buffer copies need one sample; texture copies check equal counts and, for
 * multisampled textures, the whole subresource (as for depth/stencil). */
static doe_resource_status copy_bounds(const doe_resource_texture_desc *texture,
    const doe_resource_texture_copy *copy, doe_resource_copy_alignment alignment,
    bool multisample, uint32_t physical_extent[3])
{
    if (copy->mip >= texture->mip_levels || copy->mip >= 32 || (!multisample && texture->samples != 1))
        return DOE_RESOURCE_RANGE;
    uint32_t bytes, block_width, block_height;
    doe_resource_status status = resource_format_block(texture->format, &bytes, &block_width, &block_height);
    if (status != DOE_RESOURCE_OK) return status;
    uint32_t virtual_extent[3] = {
        texture->width >> copy->mip, texture->height >> copy->mip,
        texture->dimension == WGPUTextureDimension_3D ? texture->depth_or_layers >> copy->mip : texture->depth_or_layers
    };
    for (size_t i = 0; i < 3; i++) if (!virtual_extent[i]) virtual_extent[i] = 1;
    uint64_t extent[3] = {
        ceil_blocks(virtual_extent[0], block_width) * block_width,
        ceil_blocks(virtual_extent[1], block_height) * block_height,
        virtual_extent[2]
    };
    uint32_t sizes[3] = {copy->width, copy->height, copy->depth_or_layers};
    for (size_t i = 0; i < 3; i++) {
        if (extent[i] > UINT32_MAX || copy->origin[i] > INT32_MAX ||
            copy->origin[i] > extent[i] || sizes[i] > extent[i] - copy->origin[i])
            return DOE_RESOURCE_RANGE;
    }
    uint32_t remaining_width = virtual_extent[0] > copy->origin[0] ? virtual_extent[0] - copy->origin[0] : 0;
    uint32_t remaining_height = virtual_extent[1] > copy->origin[1] ? virtual_extent[1] - copy->origin[1] : 0;
    if (copy->origin[0] % block_width || copy->origin[1] % block_height ||
        (copy->width % block_width && (alignment != DOE_RESOURCE_COPY_NATIVE || copy->width != remaining_width)) ||
        (copy->height % block_height && (alignment != DOE_RESOURCE_COPY_NATIVE || copy->height != remaining_height)))
        return DOE_RESOURCE_LAYOUT;
    for (size_t i = 0; i < 3; i++) physical_extent[i] = (uint32_t)extent[i];
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_texture_copy_validate(uint64_t buffer_size,
    const doe_resource_texture_desc *texture, const doe_resource_texture_copy *copy,
    doe_resource_copy_direction direction, doe_resource_copy_alignment alignment,
    doe_resource_copy_region *out)
{
    if (!texture || !copy || !out ||
        (direction != DOE_RESOURCE_BUFFER_TO_TEXTURE && direction != DOE_RESOURCE_TEXTURE_TO_BUFFER) ||
        (alignment != DOE_RESOURCE_COPY_NATIVE && alignment != DOE_RESOURCE_COPY_WEBGPU &&
        alignment != DOE_RESOURCE_COPY_QUEUE))
        return DOE_RESOURCE_INVALID_ARGUMENT;
    uint32_t extent[3];
    doe_resource_status status = copy_bounds(texture, copy, alignment, false, extent);
    if (status != DOE_RESOURCE_OK) return status;
    uint32_t bytes, block_width, block_height;
    status = resource_format_block(texture->format, &bytes, &block_width, &block_height);
    if (status != DOE_RESOURCE_OK) return status;
    WGPUTextureAspect aspect = copy->aspect ? copy->aspect : WGPUTextureAspect_All;
    if (!resource_format_aspect(texture->format, aspect)) return DOE_RESOURCE_ASPECT;
    bool depth = resource_format_depth(texture->format);
    bool combined = texture->format == WGPUTextureFormat_Depth24PlusStencil8 ||
        texture->format == WGPUTextureFormat_Depth32FloatStencil8;
    if (depth) {
        if (combined && aspect == WGPUTextureAspect_All) return DOE_RESOURCE_ASPECT;
        if (aspect == WGPUTextureAspect_StencilOnly || texture->format == WGPUTextureFormat_Stencil8) {
            aspect = WGPUTextureAspect_StencilOnly;
            bytes = 1;
        } else {
            aspect = WGPUTextureAspect_DepthOnly;
            if (texture->format == WGPUTextureFormat_Depth24Plus ||
                texture->format == WGPUTextureFormat_Depth24PlusStencil8) return DOE_RESOURCE_UNSUPPORTED;
            if (direction == DOE_RESOURCE_BUFFER_TO_TEXTURE &&
                (texture->format == WGPUTextureFormat_Depth32Float ||
                texture->format == WGPUTextureFormat_Depth32FloatStencil8)) return DOE_RESOURCE_ASPECT;
            if (texture->format == WGPUTextureFormat_Depth32FloatStencil8) bytes = 4;
        }
        if (copy->width != extent[0] || copy->height != extent[1]) return DOE_RESOURCE_RANGE;
    }
    /* writeTexture (QUEUE) data has no offset or bytesPerRow block alignment;
     * the caller repacks it into an aligned staging layout. */
    if (alignment != DOE_RESOURCE_COPY_QUEUE && copy->offset % (depth ? 4 : bytes)) return DOE_RESOURCE_LAYOUT;
    uint64_t row_bytes = ceil_blocks(copy->width, block_width) * bytes;
    uint64_t rows = ceil_blocks(copy->height, block_height);
    bool pitch_missing = copy->bytes_per_row == UINT32_MAX ||
        (alignment == DOE_RESOURCE_COPY_NATIVE && !copy->bytes_per_row);
    bool image_missing = copy->rows_per_image == UINT32_MAX ||
        (alignment == DOE_RESOURCE_COPY_NATIVE && !copy->rows_per_image);
    if (alignment != DOE_RESOURCE_COPY_NATIVE) {
        if (pitch_missing && (rows > 1 || copy->depth_or_layers > 1)) return DOE_RESOURCE_LAYOUT;
        if (image_missing && copy->depth_or_layers > 1) return DOE_RESOURCE_LAYOUT;
        if (alignment == DOE_RESOURCE_COPY_WEBGPU && !pitch_missing && copy->bytes_per_row % 256)
            return DOE_RESOURCE_LAYOUT;
    }
    uint64_t pitch = pitch_missing ? row_bytes : copy->bytes_per_row;
    uint64_t image_rows = image_missing ? rows : copy->rows_per_image;
    if (pitch < row_bytes || (alignment != DOE_RESOURCE_COPY_QUEUE && pitch % bytes) || image_rows < rows)
        return DOE_RESOURCE_LAYOUT;
    uint64_t required;
    if (!doe_copy_footprint(row_bytes, rows, image_rows, pitch, copy->depth_or_layers, &required))
        return DOE_RESOURCE_RANGE;
    if (copy->offset > buffer_size || required > buffer_size - copy->offset) return DOE_RESOURCE_RANGE;
    uint64_t row_length = pitch / bytes * block_width;
    uint64_t image_height = image_rows * block_height;
    if (pitch > UINT32_MAX || image_rows > UINT32_MAX || row_length > UINT32_MAX || image_height > UINT32_MAX)
        return DOE_RESOURCE_RANGE;
    *out = (doe_resource_copy_region){.aspect=aspect, .pitch=(uint32_t)pitch, .image_rows=(uint32_t)image_rows,
        .row_length=(uint32_t)row_length, .image_height=(uint32_t)image_height, .required_bytes=required,
        .row_bytes=(uint32_t)row_bytes, .rows=(uint32_t)rows};
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_texture_blit_validate(const void *device,
    const doe_resource_texture_blit *copy, doe_resource_copy_alignment alignment,
    doe_resource_texture_blit *normalized)
{
    if (!device || !copy || !normalized ||
        (alignment != DOE_RESOURCE_COPY_NATIVE && alignment != DOE_RESOURCE_COPY_WEBGPU))
        return DOE_RESOURCE_INVALID_ARGUMENT;
    const doe_resource_texture *source = copy->source.texture;
    const doe_resource_texture *destination = copy->destination.texture;
    if (!resource_texture_available(source) || !resource_texture_available(destination))
        return DOE_RESOURCE_UNAVAILABLE;
    if (source->base.config.device.identity != device || destination->base.config.device.identity != device)
        return DOE_RESOURCE_DEVICE_MISMATCH;
    if (!(source->desc.usage & WGPUTextureUsage_CopySrc) ||
        !(destination->desc.usage & WGPUTextureUsage_CopyDst)) return DOE_RESOURCE_USAGE;
    if (!resource_format_compatible(source->desc.format, destination->desc.format) ||
        source->desc.dimension != destination->desc.dimension ||
        source->desc.samples != destination->desc.samples) return DOE_RESOURCE_LAYOUT;
    doe_resource_texture_blit result = *copy;
    doe_resource_image_copy *endpoints[2] = {&result.source, &result.destination};
    for (size_t i = 0; i < 2; i++) {
        doe_resource_image_copy *endpoint = endpoints[i];
        /* Every aspect of the format moves: 'all', or the one aspect of a
         * depth-only or stencil-only format, normalized to 'all'. */
        unsigned aspects = resource_format_aspects(endpoint->texture->desc.format);
        if ((endpoint->aspect == WGPUTextureAspect_DepthOnly && aspects == 2) ||
            (endpoint->aspect == WGPUTextureAspect_StencilOnly && aspects == 4) || !endpoint->aspect)
            endpoint->aspect = WGPUTextureAspect_All;
        if (endpoint->aspect != WGPUTextureAspect_All) return DOE_RESOURCE_ASPECT;
        doe_resource_texture_copy region = {
            .mip = endpoint->mip, .origin = {endpoint->origin[0], endpoint->origin[1], endpoint->origin[2]},
            .width = copy->width, .height = copy->height, .depth_or_layers = copy->depth_or_layers
        };
        uint32_t extent[3];
        doe_resource_status status = copy_bounds(&endpoint->texture->desc, &region, alignment, true, extent);
        if (status != DOE_RESOURCE_OK) return status;
        if ((resource_format_depth(endpoint->texture->desc.format) || endpoint->texture->desc.samples > 1) &&
            (copy->width != extent[0] || copy->height != extent[1])) return DOE_RESOURCE_RANGE;
    }
    if (source == destination && copy->source.mip == copy->destination.mip) {
        if (source->desc.dimension == WGPUTextureDimension_3D ||
            ((uint64_t)copy->source.origin[2] < (uint64_t)copy->destination.origin[2] + copy->depth_or_layers &&
            (uint64_t)copy->destination.origin[2] < (uint64_t)copy->source.origin[2] + copy->depth_or_layers))
            return DOE_RESOURCE_ALIAS;
    }
    *normalized = result;
    return DOE_RESOURCE_OK;
}
