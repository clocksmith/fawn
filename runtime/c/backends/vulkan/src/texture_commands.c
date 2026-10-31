/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"
#include "../../../common/include/doe_copy_footprint.h"

static uint32_t mip_extent(uint32_t size, uint32_t mip) {
    uint32_t value = size >> mip;
    return value != 0 ? value : 1;
}

/* Multisampled images copy only image-to-image (equal sample counts, whole
 * subresource, checked by the copy); buffer copies need one sample. */
static doe_vk_status region_valid(const doe_vk_texture *texture, doe_vk_texture_region region) {
    if (region.mip >= texture->desc.mip_levels || region.mip >= 32 ||
        region.extent[0] == 0 || region.extent[1] == 0 || region.extent[2] == 0)
        return DOE_VK_INVALID_ARGUMENT;
    uint32_t dimensions[3] = {
        mip_extent(texture->desc.width, region.mip),
        mip_extent(texture->desc.height, region.mip),
        texture->desc.dimension == DOE_VK_TEXTURE_3D ?
            mip_extent(texture->desc.depth_or_layers, region.mip) : texture->desc.depth_or_layers
    };
    for (uint32_t i = 0; i < 3; ++i) {
        if (region.origin[i] > INT32_MAX || !doe_vk_range(dimensions[i], region.origin[i], region.extent[i]))
            return DOE_VK_INVALID_ARGUMENT;
    }
    /* Block formats: block-aligned origin; extent whole blocks or reaching the edge. */
    for (uint32_t i = 0; i < 2; ++i) {
        uint32_t block = doe_vk_format_block_extent(texture->desc.format, i);
        if (region.origin[i] % block != 0 ||
            (region.extent[i] % block != 0 && region.origin[i] + region.extent[i] != dimensions[i]))
            return DOE_VK_INVALID_ARGUMENT;
    }
    return DOE_VK_OK;
}

/* A WebGPU block-format copy names the physical (block-rounded) extent; the
 * native region stops at the subresource edge. Uncompressed regions pass. */
static doe_vk_texture_region native_region(const doe_vk_texture *texture, doe_vk_texture_region region) {
    if (region.mip >= 32) return region;
    const uint32_t sizes[2] = {mip_extent(texture->desc.width, region.mip), mip_extent(texture->desc.height, region.mip)};
    for (uint32_t i = 0; i < 2; ++i) {
        uint32_t block = doe_vk_format_block_extent(texture->desc.format, i);
        if (region.origin[i] < sizes[i] && region.extent[i] > sizes[i] - region.origin[i] &&
            region.extent[i] - (sizes[i] - region.origin[i]) < block)
            region.extent[i] = sizes[i] - region.origin[i];
    }
    return region;
}

doe_vk_status doe_vk_texture_transfer_region(const doe_vk_context *context,
    const doe_vk_texture *texture, doe_vk_texture_region region) {
    doe_vk_status status = region_valid(texture, region);
    if (status != DOE_VK_OK) return status;
    const uint32_t granularity[3] = { context->transfer_granularity.width,
        context->transfer_granularity.height, context->transfer_granularity.depth };
    const uint32_t sizes[3] = { mip_extent(texture->desc.width, region.mip),
        mip_extent(texture->desc.height, region.mip),
        texture->desc.dimension == DOE_VK_TEXTURE_3D ?
            mip_extent(texture->desc.depth_or_layers, region.mip) : 1 };
    for (uint32_t axis = 0; axis < 3; ++axis) {
        uint32_t origin = axis == 2 && texture->desc.dimension != DOE_VK_TEXTURE_3D ? 0 : region.origin[axis];
        uint32_t extent = axis == 2 && texture->desc.dimension != DOE_VK_TEXTURE_3D ? 1 : region.extent[axis];
        if (granularity[axis] == 0) {
            if (origin != 0 || extent != sizes[axis]) return DOE_VK_UNSUPPORTED;
        } else if (origin % granularity[axis] != 0 ||
            (extent % granularity[axis] != 0 && extent != sizes[axis] - origin))
            return DOE_VK_UNSUPPORTED;
    }
    return DOE_VK_OK;
}

/* Resolves a region aspect to native bits. Buffer copies additionally need one
 * aspect with a defined linear texel size: the packed D24 depth layout and a
 * combined two-aspect selection have none. Public per-format copy direction
 * rules (for example, no depth32float upload) remain with the resource owner. */
static doe_vk_status region_aspect(const doe_vk_texture *texture, doe_vk_texture_aspect aspect,
    bool buffer, uint32_t *mask, uint32_t *bytes) {
    uint32_t selected = 0;
    doe_vk_status status = doe_vk_texture_aspects(texture, aspect, &selected);
    if (status != DOE_VK_OK) return status;
    uint32_t size = texture->texel_bytes;
    if (buffer && selected == VK_IMAGE_ASPECT_STENCIL_BIT) size = 1;
    else if (buffer && selected == VK_IMAGE_ASPECT_DEPTH_BIT) {
        if (texture->desc.format == DOE_VK_FORMAT_D16_UNORM) size = 2;
        else if (texture->desc.format == DOE_VK_FORMAT_D32_FLOAT ||
            texture->desc.format == DOE_VK_FORMAT_D32_FLOAT_S8_UINT) size = 4;
        else return DOE_VK_UNSUPPORTED;
    } else if (buffer && selected != VK_IMAGE_ASPECT_COLOR_BIT) return DOE_VK_INVALID_ARGUMENT;
    *mask = selected;
    *bytes = size;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_texture_copy_region(const doe_vk_texture *texture,
    doe_vk_texture_region region, uint64_t buffer_size,
    doe_vk_texture_buffer_layout layout, VkBufferImageCopy *out) {
    if (texture == NULL || out == NULL || texture->texel_bytes == 0)
        return DOE_VK_INVALID_ARGUMENT;
    if (doe_vk_samples(texture->desc.sample_count) != 1) return DOE_VK_UNSUPPORTED;
    doe_vk_status status = region_valid(texture, region);
    if (status != DOE_VK_OK) return status;
    uint32_t aspect = 0, texel = 0;
    status = region_aspect(texture, region.aspect, true, &aspect, &texel);
    if (status != DOE_VK_OK) return status;
    /* Buffer rows count texel blocks: rows_per_image is in block rows. */
    const uint32_t block_width = doe_vk_format_block_extent(texture->desc.format, 0);
    const uint32_t block_height = doe_vk_format_block_extent(texture->desc.format, 1);
    const uint64_t block_columns = ((uint64_t)region.extent[0] + block_width - 1) / block_width;
    const uint64_t block_rows = ((uint64_t)region.extent[1] + block_height - 1) / block_height;
    uint64_t row_bytes = block_columns * texel;
    uint64_t pitch = layout.bytes_per_row != 0 ? layout.bytes_per_row : row_bytes;
    uint64_t rows = layout.rows_per_image != 0 ? layout.rows_per_image : block_rows;
    /* Vulkan (graphics queue) needs only a texel-block-aligned buffer offset;
     * depth/stencil aspects also need a multiple of 4, as WebGPU does. */
    const bool depth_stencil = (aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0;
    if (pitch < row_bytes || pitch % texel != 0 || rows < block_rows ||
        (depth_stencil && (layout.offset & 3) != 0) || layout.offset % texel != 0 ||
        pitch / texel * block_width > UINT32_MAX || rows * block_height > UINT32_MAX) return DOE_VK_INVALID_ARGUMENT;
    uint64_t bytes;
    if (!doe_copy_footprint(row_bytes, block_rows, rows, pitch, region.extent[2], &bytes) ||
        !doe_vk_range(buffer_size, layout.offset, bytes)) return DOE_VK_INVALID_ARGUMENT;
    bool volume = texture->desc.dimension == DOE_VK_TEXTURE_3D;
    VkBufferImageCopy copy = {
        .bufferOffset = layout.offset,
        .bufferRowLength = layout.bytes_per_row != 0 ? (uint32_t)(pitch / texel * block_width) : 0,
        .bufferImageHeight = layout.rows_per_image * block_height,
        .imageSubresource = { aspect, region.mip,
            volume ? 0 : region.origin[2], volume ? 1 : region.extent[2] },
        .imageOffset = { (int32_t)region.origin[0], (int32_t)region.origin[1], volume ? (int32_t)region.origin[2] : 0 },
        .imageExtent = { region.extent[0], region.extent[1], volume ? region.extent[2] : 1 }
    };
    *out = copy;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_use_texture(doe_vk_commands *commands, doe_vk_texture *texture,
    doe_vk_texture_use **out) {
    if (texture == NULL || texture->context != commands->context) return DOE_VK_INVALID_ARGUMENT;
    if (texture->borrowed_image && !texture->external_alive) return DOE_VK_INVALID_STATE;
    if (texture->evicted) return DOE_VK_INVALID_STATE;
    if (texture->imported && !texture->external_acquired) return DOE_VK_INVALID_STATE;
    if (texture->external_owner != NULL && texture->external_owner != commands) return DOE_VK_BUSY;
    if (texture->content_status != DOE_VK_OK) return texture->content_status;
    if (texture->recording_owner != NULL && texture->recording_owner != commands) return DOE_VK_BUSY;
    for (doe_vk_texture_use *use = commands->textures; use != NULL; use = use->next) {
        if (use->texture == texture) {
            *out = use;
            return DOE_VK_OK;
        }
    }
    doe_vk_texture_use *use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_VK_OUT_OF_MEMORY;
    use->initialized = malloc(texture->subresource_count);
    if (use->initialized == NULL) {
        free(use);
        return DOE_VK_OUT_OF_MEMORY;
    }
    doe_vk_status status = doe_vk_texture_retain(texture);
    if (status != DOE_VK_OK) {
        free(use->initialized);
        free(use);
        return status;
    }
    if (texture->command_uses == UINT32_MAX) {
        doe_vk_texture_release(texture);
        free(use->initialized);
        free(use);
        return DOE_VK_OUT_OF_MEMORY;
    }
    ++texture->command_uses;
    use->texture = texture;
    use->layout = texture->layout;
    memcpy(use->initialized, texture->initialized, texture->subresource_count);
    use->next = commands->textures;
    commands->textures = use;
    texture->recording_owner = commands;
    *out = use;
    return DOE_VK_OK;
}

void doe_vk_texture_transition(doe_vk_commands *commands, doe_vk_texture_use *use,
    uint32_t layout) {
    const doe_vk_texture *texture = use->texture;
    const VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = use->layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 :
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .oldLayout = use->layout,
        .newLayout = layout,
        .srcQueueFamilyIndex = UINT32_MAX,
        .dstQueueFamilyIndex = UINT32_MAX,
        .image = texture->image,
        .subresourceRange = { doe_vk_format_aspects(texture->desc.format), 0, texture->desc.mip_levels,
            0, texture->desc.dimension == DOE_VK_TEXTURE_3D ? 1 : texture->desc.depth_or_layers }
    };
    commands->context->vk.vkCmdPipelineBarrier(commands->command,
        use->layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    use->layout = layout;
}

/* Only full writes initialize previously undefined subresources. This prevents
 * reading allocator contents while retaining partial updates of initialized data.
 * Initialization bytes are the existing per-aspect masks; only the copied
 * aspects must be initialized for reads, and only they become initialized. */
static doe_vk_status initialize_region(doe_vk_texture_use *use,
    doe_vk_texture_region region, uint32_t aspects, bool writing) {
    doe_vk_texture *texture = use->texture;
    bool volume = texture->desc.dimension == DOE_VK_TEXTURE_3D;
    uint32_t layers = volume ? 1 : texture->desc.depth_or_layers;
    uint32_t first = volume ? 0 : region.origin[2];
    uint32_t count = volume ? 1 : region.extent[2];
    bool full = region.origin[0] == 0 && region.origin[1] == 0 &&
        region.extent[0] == mip_extent(texture->desc.width, region.mip) &&
        region.extent[1] == mip_extent(texture->desc.height, region.mip);
    if (volume) full = full && region.origin[2] == 0 &&
        region.extent[2] == mip_extent(texture->desc.depth_or_layers, region.mip);
    for (uint32_t layer = first; layer < first + count; ++layer) {
        size_t index = (size_t)region.mip * layers + layer;
        if ((use->initialized[index] & aspects) != aspects && !(writing && full)) return DOE_VK_INVALID_STATE;
    }
    if (writing && full) {
        for (uint32_t layer = first; layer < first + count; ++layer)
            use->initialized[(size_t)region.mip * layers + layer] |= (uint8_t)aspects;
    }
    return DOE_VK_OK;
}

static doe_vk_status buffer_texture_copy(doe_vk_commands *commands,
    doe_vk_buffer *buffer, doe_vk_texture_buffer_layout layout,
    doe_vk_texture *texture, doe_vk_texture_region region, bool upload) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (buffer == NULL || texture == NULL || buffer->context != commands->context ||
        texture->context != commands->context ||
        !(buffer->usage & (upload ? DOE_VK_BUFFER_COPY_SRC : DOE_VK_BUFFER_COPY_DST)) ||
        !(texture->desc.usage & (upload ? DOE_VK_TEXTURE_COPY_DST : DOE_VK_TEXTURE_COPY_SRC)))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    region = native_region(texture, region);
    status = doe_vk_texture_transfer_region(commands->context, texture, region);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    VkBufferImageCopy copy;
    status = doe_vk_texture_copy_region(texture, region, buffer->size, layout, &copy);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    doe_vk_texture_use *use = NULL;
    status = doe_vk_use_texture(commands, texture, &use);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    status = initialize_region(use, region, copy.imageSubresource.aspectMask, upload);
    if (status == DOE_VK_OK) status = doe_vk_use_buffer(commands, buffer);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    doe_vk_barrier(commands);
    uint32_t image_layout = upload ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    doe_vk_texture_transition(commands, use, image_layout);
    if (upload) commands->context->vk.vkCmdCopyBufferToImage(commands->command,
        buffer->buffer, texture->image, image_layout, 1, &copy);
    else commands->context->vk.vkCmdCopyImageToBuffer(commands->command,
        texture->image, image_layout, buffer->buffer, 1, &copy);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_commands_upload_texture(doe_vk_commands *commands,
    doe_vk_buffer *source, doe_vk_texture_buffer_layout layout,
    doe_vk_texture *destination, doe_vk_texture_region region) {
    return buffer_texture_copy(commands, source, layout, destination, region, true);
}

doe_vk_status doe_vk_commands_read_texture(doe_vk_commands *commands,
    doe_vk_texture *source, doe_vk_texture_region region,
    doe_vk_buffer *destination, doe_vk_texture_buffer_layout layout) {
    return buffer_texture_copy(commands, destination, layout, source, region, false);
}

/* https://gpuweb.github.io/gpuweb/#copy-compatible: equal formats, or formats
 * that differ only in srgb-ness. Vulkan copies such size-compatible pairs
 * directly. */
static bool copy_formats_compatible(doe_vk_texture_format source, doe_vk_texture_format destination) {
    return source == destination || (destination != 0 && doe_vk_srgb_pair(source) == destination);
}

doe_vk_status doe_vk_commands_copy_texture(doe_vk_commands *commands,
    doe_vk_texture *source, doe_vk_texture_region source_region,
    doe_vk_texture *destination, doe_vk_texture_region destination_region) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (source == NULL || destination == NULL || source->context != commands->context ||
        destination->context != commands->context || !(source->desc.usage & DOE_VK_TEXTURE_COPY_SRC) ||
        !(destination->desc.usage & DOE_VK_TEXTURE_COPY_DST) ||
        !copy_formats_compatible(source->desc.format, destination->desc.format) ||
        source->desc.dimension != destination->desc.dimension ||
        memcmp(source_region.extent, destination_region.extent, sizeof(source_region.extent)) != 0 ||
        doe_vk_samples(source->desc.sample_count) != doe_vk_samples(destination->desc.sample_count))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    /* One texture copies only between disjoint subresources: another mip, or
     * disjoint array layers of a 2D texture (WebGPU copyTextureToTexture). */
    const bool same = source == destination;
    if (same && source_region.mip == destination_region.mip &&
        (source->desc.dimension == DOE_VK_TEXTURE_3D ||
        ((uint64_t)source_region.origin[2] < (uint64_t)destination_region.origin[2] + destination_region.extent[2] &&
        (uint64_t)destination_region.origin[2] < (uint64_t)source_region.origin[2] + source_region.extent[2])))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    /* Multisampled image copies cover the whole single subresource (WebGPU). */
    if (doe_vk_samples(source->desc.sample_count) != 1) {
        const doe_vk_texture_region *regions[2] = {&source_region, &destination_region};
        for (uint32_t i = 0; i < 2; ++i)
            if (regions[i]->mip != 0 || regions[i]->origin[0] != 0 || regions[i]->origin[1] != 0 ||
                regions[i]->origin[2] != 0 || regions[i]->extent[0] != source->desc.width ||
                regions[i]->extent[1] != source->desc.height || regions[i]->extent[2] != 1)
                return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    }
    uint32_t source_aspects = 0, destination_aspects = 0, unused = 0;
    status = region_aspect(source, source_region.aspect, false, &source_aspects, &unused);
    if (status == DOE_VK_OK)
        status = region_aspect(destination, destination_region.aspect, false, &destination_aspects, &unused);
    if (status == DOE_VK_OK && source_aspects != destination_aspects) status = DOE_VK_INVALID_ARGUMENT;
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    /* Each side's block-rounded extent stops at its own subresource edge. */
    source_region = native_region(source, source_region);
    destination_region = native_region(destination, destination_region);
    status = doe_vk_texture_transfer_region(commands->context, source, source_region);
    if (status == DOE_VK_OK) status = doe_vk_texture_transfer_region(commands->context, destination, destination_region);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    /* Clamped block extents that differ (one side at its edge, the other not)
     * have no valid VkImageCopy region; such copies go through a staging buffer. */
    const bool staged = memcmp(source_region.extent, destination_region.extent, sizeof(source_region.extent)) != 0;
    doe_vk_buffer *staging = NULL;
    const uint32_t block_width = doe_vk_format_block_extent(source->desc.format, 0);
    const uint32_t block_height = doe_vk_format_block_extent(source->desc.format, 1);
    const uint64_t columns = ((uint64_t)source_region.extent[0] + block_width - 1) / block_width;
    const uint64_t rows = ((uint64_t)source_region.extent[1] + block_height - 1) / block_height;
    if (staged) {
        status = doe_vk_buffer_create(commands->context, columns * rows * source_region.extent[2] * source->texel_bytes,
            DOE_VK_BUFFER_COPY_SRC | DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_DEVICE, &staging, NULL);
        if (status == DOE_VK_OK) status = doe_vk_use_buffer(commands, staging);
        doe_vk_buffer_release(staging); /* The command owner retains its use. */
        if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    }
    doe_vk_texture_use *source_use = NULL;
    doe_vk_texture_use *destination_use = NULL;
    status = doe_vk_use_texture(commands, source, &source_use);
    if (status == DOE_VK_OK) status = doe_vk_use_texture(commands, destination, &destination_use);
    if (status == DOE_VK_OK) status = initialize_region(source_use, source_region, source_aspects, false);
    if (status == DOE_VK_OK) status = initialize_region(destination_use, destination_region, destination_aspects, true);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    bool volume = source->desc.dimension == DOE_VK_TEXTURE_3D;
    const VkImageSubresourceLayers from = { source_aspects, source_region.mip,
        volume ? 0 : source_region.origin[2], volume ? 1 : source_region.extent[2] };
    const VkImageSubresourceLayers to = { destination_aspects, destination_region.mip,
        volume ? 0 : destination_region.origin[2], volume ? 1 : destination_region.extent[2] };
    const VkOffset3D from_offset = { (int32_t)source_region.origin[0], (int32_t)source_region.origin[1],
        volume ? (int32_t)source_region.origin[2] : 0 };
    const VkOffset3D to_offset = { (int32_t)destination_region.origin[0], (int32_t)destination_region.origin[1],
        volume ? (int32_t)destination_region.origin[2] : 0 };
    /* Layouts track the whole image, so a texture that is both sides of the
     * copy uses GENERAL, which Vulkan admits for source and destination. */
    const uint32_t source_layout = same ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const uint32_t destination_layout = same ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    doe_vk_barrier(commands);
    doe_vk_texture_transition(commands, source_use, source_layout);
    if (!same) doe_vk_texture_transition(commands, destination_use, destination_layout);
    if (!staged) {
        const VkImageCopy copy = { .srcSubresource = from, .srcOffset = from_offset, .dstSubresource = to,
            .dstOffset = to_offset,
            .extent = { source_region.extent[0], source_region.extent[1], volume ? source_region.extent[2] : 1 } };
        commands->context->vk.vkCmdCopyImage(commands->command, source->image,
            source_layout, destination->image, destination_layout, 1, &copy);
        return DOE_VK_OK;
    }
    /* Both sides cover the same whole blocks; tight block rows in the buffer. */
    const VkBufferImageCopy out = { .bufferOffset = 0, .bufferRowLength = (uint32_t)(columns * block_width),
        .bufferImageHeight = (uint32_t)(rows * block_height), .imageSubresource = from, .imageOffset = from_offset,
        .imageExtent = { source_region.extent[0], source_region.extent[1], volume ? source_region.extent[2] : 1 } };
    const VkBufferImageCopy in = { .bufferOffset = 0, .bufferRowLength = (uint32_t)(columns * block_width),
        .bufferImageHeight = (uint32_t)(rows * block_height), .imageSubresource = to, .imageOffset = to_offset,
        .imageExtent = { destination_region.extent[0], destination_region.extent[1],
            volume ? destination_region.extent[2] : 1 } };
    commands->context->vk.vkCmdCopyImageToBuffer(commands->command, source->image,
        source_layout, staging->buffer, 1, &out);
    doe_vk_barrier(commands);
    commands->context->vk.vkCmdCopyBufferToImage(commands->command, staging->buffer, destination->image,
        destination_layout, 1, &in);
    return DOE_VK_OK;
}

/* Publishes the recording's final layouts and initialization, or poisons the
 * touched images. Submission publishes once vkQueueSubmit accepts the work:
 * one queue executes in submission order, so the published state is every
 * later recording's starting state (owner authorized 2026-10-24: async submit
 * A+B, D2, fence-wait completion; futures later; reconfirmed after the section 7 risk review). The image leases stay with
 * the recording until it is proven terminal. */
void doe_vk_textures_complete(doe_vk_commands *commands, doe_vk_status status) {
    for (doe_vk_texture_use *use = commands->textures; use != NULL; use = use->next) {
        doe_vk_texture *texture = use->texture;
        if (status == DOE_VK_OK) {
            if (commands->committed) continue;
            texture->layout = use->layout;
            memcpy(texture->initialized, use->initialized, texture->subresource_count);
        } else if (texture->content_status == DOE_VK_OK) texture->content_status = status;
        if (texture->recording_owner == commands) texture->recording_owner = NULL;
    }
}

void doe_vk_textures_release(doe_vk_commands *commands) {
    while (commands->textures != NULL) {
        doe_vk_texture_use *use = commands->textures;
        commands->textures = use->next;
        if (use->texture->recording_owner == commands) use->texture->recording_owner = NULL;
        doe_vk_texture_use_released(use->texture);
        doe_vk_texture_release(use->texture);
        free(use->initialized);
        free(use);
    }
}
