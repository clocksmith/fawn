#include "texture_internal.h"
#include "../../../common/include/doe_copy_footprint.h"

static uint32_t mip_extent(uint32_t size, uint32_t mip) {
    uint32_t value = size >> mip;
    return value != 0 ? value : 1;
}

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
    return DOE_VK_OK;
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

doe_vk_status doe_vk_texture_copy_region(const doe_vk_texture *texture,
    doe_vk_texture_region region, uint64_t buffer_size,
    doe_vk_texture_buffer_layout layout, VkBufferImageCopy *out) {
    if (texture == NULL || out == NULL || texture->texel_bytes == 0)
        return DOE_VK_INVALID_ARGUMENT;
    doe_vk_status status = region_valid(texture, region);
    if (status != DOE_VK_OK) return status;
    uint64_t row_bytes = (uint64_t)region.extent[0] * texture->texel_bytes;
    uint64_t pitch = layout.bytes_per_row != 0 ? layout.bytes_per_row : row_bytes;
    uint64_t rows = layout.rows_per_image != 0 ? layout.rows_per_image : region.extent[1];
    if (pitch < row_bytes || pitch % texture->texel_bytes != 0 || rows < region.extent[1] ||
        (layout.offset & 3) != 0 || layout.offset % texture->texel_bytes != 0 ||
        pitch / texture->texel_bytes > UINT32_MAX) return DOE_VK_INVALID_ARGUMENT;
    uint64_t bytes;
    if (!doe_copy_footprint(row_bytes, region.extent[1], rows, pitch, region.extent[2], &bytes) ||
        !doe_vk_range(buffer_size, layout.offset, bytes)) return DOE_VK_INVALID_ARGUMENT;
    bool volume = texture->desc.dimension == DOE_VK_TEXTURE_3D;
    VkBufferImageCopy copy = {
        .bufferOffset = layout.offset,
        .bufferRowLength = layout.bytes_per_row != 0 ? (uint32_t)(pitch / texture->texel_bytes) : 0,
        .bufferImageHeight = layout.rows_per_image,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, region.mip,
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
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->desc.mip_levels,
            0, texture->desc.dimension == DOE_VK_TEXTURE_3D ? 1 : texture->desc.depth_or_layers }
    };
    commands->context->vk.vkCmdPipelineBarrier(commands->command,
        use->layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    use->layout = layout;
}

/* Only full writes initialize previously undefined subresources. This prevents
 * reading allocator contents while retaining partial updates of initialized data. */
static doe_vk_status initialize_region(doe_vk_texture_use *use,
    doe_vk_texture_region region, bool writing) {
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
        if (!use->initialized[index] && !(writing && full)) return DOE_VK_INVALID_STATE;
    }
    if (writing && full) {
        for (uint32_t layer = first; layer < first + count; ++layer)
            use->initialized[(size_t)region.mip * layers + layer] = 1;
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
    status = doe_vk_texture_transfer_region(commands->context, texture, region);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    VkBufferImageCopy copy;
    status = doe_vk_texture_copy_region(texture, region, buffer->size, layout, &copy);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    doe_vk_texture_use *use = NULL;
    status = doe_vk_use_texture(commands, texture, &use);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    status = initialize_region(use, region, upload);
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

doe_vk_status doe_vk_commands_copy_texture(doe_vk_commands *commands,
    doe_vk_texture *source, doe_vk_texture_region source_region,
    doe_vk_texture *destination, doe_vk_texture_region destination_region) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (source == NULL || destination == NULL || source->context != commands->context ||
        destination->context != commands->context || !(source->desc.usage & DOE_VK_TEXTURE_COPY_SRC) ||
        !(destination->desc.usage & DOE_VK_TEXTURE_COPY_DST) || source == destination ||
        source->desc.format != destination->desc.format || source->desc.dimension != destination->desc.dimension ||
        memcmp(source_region.extent, destination_region.extent, sizeof(source_region.extent)) != 0)
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    status = doe_vk_texture_transfer_region(commands->context, source, source_region);
    if (status == DOE_VK_OK) status = doe_vk_texture_transfer_region(commands->context, destination, destination_region);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    doe_vk_texture_use *source_use = NULL;
    doe_vk_texture_use *destination_use = NULL;
    status = doe_vk_use_texture(commands, source, &source_use);
    if (status == DOE_VK_OK) status = doe_vk_use_texture(commands, destination, &destination_use);
    if (status == DOE_VK_OK) status = initialize_region(source_use, source_region, false);
    if (status == DOE_VK_OK) status = initialize_region(destination_use, destination_region, true);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    bool volume = source->desc.dimension == DOE_VK_TEXTURE_3D;
    const VkImageCopy copy = {
        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, source_region.mip,
            volume ? 0 : source_region.origin[2], volume ? 1 : source_region.extent[2] },
        .srcOffset = { (int32_t)source_region.origin[0], (int32_t)source_region.origin[1], volume ? (int32_t)source_region.origin[2] : 0 },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, destination_region.mip,
            volume ? 0 : destination_region.origin[2], volume ? 1 : destination_region.extent[2] },
        .dstOffset = { (int32_t)destination_region.origin[0], (int32_t)destination_region.origin[1], volume ? (int32_t)destination_region.origin[2] : 0 },
        .extent = { source_region.extent[0], source_region.extent[1], volume ? source_region.extent[2] : 1 }
    };
    doe_vk_barrier(commands);
    doe_vk_texture_transition(commands, source_use, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    doe_vk_texture_transition(commands, destination_use, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    commands->context->vk.vkCmdCopyImage(commands->command, source->image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    return DOE_VK_OK;
}

void doe_vk_textures_complete(doe_vk_commands *commands, doe_vk_status status) {
    for (doe_vk_texture_use *use = commands->textures; use != NULL; use = use->next) {
        doe_vk_texture *texture = use->texture;
        if (status == DOE_VK_OK) {
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
        doe_vk_texture_release(use->texture);
        free(use->initialized);
        free(use);
    }
}
