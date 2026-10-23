/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>

/* Real eager initialization, using the same native queue obligation owner as
 * public submissions. Never publish allocator contents or infer completion. */
doe_vk_status native_initialize_texture(native_device *device, doe_vk_texture *texture,
    const doe_vk_texture_desc *desc, uint32_t texel_bytes)
{
    if ((desc->usage & DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT) || desc->sample_count > 1) {
        doe_vk_texture_view *view = NULL;
        doe_vk_commands *commands = NULL;
        doe_vk_texture_view_desc span = {.dimension=DOE_VK_VIEW_2D_ARRAY,
            .mip_count=desc->mip_levels,.layer_count=desc->depth_or_layers};
        doe_vk_status status = doe_vk_texture_view_create(texture, &span, &view, NULL);
        if (status == DOE_VK_OK) status = doe_vk_commands_create(device->backend.value.vulkan.context, &commands, NULL);
        if (status == DOE_VK_OK) status = doe_vk_commands_initialize_attachment(commands, view);
        if (status == DOE_VK_OK) status = native_submit_commands(device, commands);
        else if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
        doe_vk_texture_view_release(view);
        return status;
    }
    uint64_t size = desc->width;
    if (size > SIZE_MAX / desc->height) return DOE_VK_OUT_OF_MEMORY;
    size *= desc->height;
    if (size > SIZE_MAX / desc->depth_or_layers) return DOE_VK_OUT_OF_MEMORY;
    size *= desc->depth_or_layers;
    if (size > SIZE_MAX / texel_bytes) return DOE_VK_OUT_OF_MEMORY;
    size *= texel_bytes;
    doe_vk_buffer *zero = NULL;
    doe_vk_commands *commands = NULL;
    doe_vk_error error = {0};
    doe_vk_status status = doe_vk_buffer_create(device->backend.value.vulkan.context, size,
        DOE_VK_BUFFER_COPY_SRC, DOE_VK_MEMORY_HOST, &zero, &error);
    if (status == DOE_VK_OK) status = doe_vk_commands_create(device->backend.value.vulkan.context, &commands, &error);
    for (uint32_t mip = 0; status == DOE_VK_OK && mip < desc->mip_levels; ++mip) {
        uint32_t width = desc->width >> mip;
        uint32_t height = desc->height >> mip;
        uint32_t depth = desc->dimension == DOE_VK_TEXTURE_3D ? desc->depth_or_layers >> mip : desc->depth_or_layers;
        doe_vk_texture_region region = {.mip=mip,
            .extent={width ? width : 1, height ? height : 1, depth ? depth : 1}};
        status = doe_vk_commands_upload_texture(commands, zero,
            (doe_vk_texture_buffer_layout){0}, texture, region);
    }
    if (status == DOE_VK_OK) status = native_submit_commands(device, commands);
    else if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
    doe_vk_buffer_release(zero);
    return status;
}
