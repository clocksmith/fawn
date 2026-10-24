/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>

/* Real eager initialization, using the same native queue obligation owner as
 * public submissions. Never publish allocator contents or infer completion.
 * Attachment-only images without internal CopyDst (depth/stencil, multisample
 * and borrowed surface frames) are cleared by the canonical attachment owner. */
native_failure native_initialize_texture(native_device *device, doe_vk_texture *texture,
    const doe_vk_texture_desc *desc, const doe_vk_format_caps *caps)
{
    const uint32_t texel_bytes = caps->texel_bytes; /* Bytes of one texel block. */
    if ((desc->usage & DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT) || desc->sample_count > 1 ||
        !(desc->usage & DOE_VK_TEXTURE_COPY_DST)) {
        doe_vk_texture_view *view = NULL;
        doe_vk_commands *commands = NULL;
        doe_vk_texture_view_desc span = {.dimension=DOE_VK_VIEW_2D_ARRAY,
            .mip_count=desc->mip_levels,.layer_count=desc->depth_or_layers};
        doe_vk_error error = {0};
        doe_vk_status status = doe_vk_texture_view_create(texture, &span, &view, &error);
        if (status == DOE_VK_OK) status = doe_vk_commands_create(device->backend.value.vulkan.context, &commands, &error);
        if (status == DOE_VK_OK) status = doe_vk_commands_initialize_attachment(commands, view);
        native_failure failure = native_failure_vulkan(status, &error);
        if (status == DOE_VK_OK) failure = native_submit_commands(device, commands);
        else if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
        doe_vk_texture_view_release(view);
        return failure;
    }
    /* Zero staging holds mip 0's texel blocks (whole 4x4 blocks for BC). */
    const uint64_t block = caps->block_extent ? caps->block_extent : 1;
    uint64_t size = (desc->width + block - 1) / block;
    const uint64_t rows = (desc->height + block - 1) / block;
    if (size > SIZE_MAX / rows) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    size *= rows;
    if (size > SIZE_MAX / desc->depth_or_layers) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    size *= desc->depth_or_layers;
    if (size > SIZE_MAX / texel_bytes) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    size *= texel_bytes;
    doe_vk_buffer *zero = NULL;
    doe_vk_commands *commands = NULL;
    doe_vk_error error = {0};
    doe_vk_status status = doe_vk_buffer_create(device->backend.value.vulkan.context, size,
        DOE_VK_BUFFER_COPY_SRC, DOE_VK_MEMORY_HOST, &zero, &error);
    if (status == DOE_VK_OK) status = doe_vk_commands_create(device->backend.value.vulkan.context, &commands, &error);
    /* Copy-only combined depth/stencil images upload each aspect separately; the
     * zero staging size covers the largest packed texel. Single-aspect and color
     * images use their whole format. */
    const bool combined = caps->aspects == (DOE_VK_ASPECT_DEPTH | DOE_VK_ASPECT_STENCIL);
    const doe_vk_texture_aspect aspects[2] = {combined ? DOE_VK_ASPECT_DEPTH : DOE_VK_ASPECT_ALL,
        DOE_VK_ASPECT_STENCIL};
    for (uint32_t mip = 0; status == DOE_VK_OK && mip < desc->mip_levels; ++mip) {
        uint32_t width = desc->width >> mip;
        uint32_t height = desc->height >> mip;
        uint32_t depth = desc->dimension == DOE_VK_TEXTURE_3D ? desc->depth_or_layers >> mip : desc->depth_or_layers;
        for (size_t aspect = 0; status == DOE_VK_OK && aspect < (combined ? 2u : 1u); ++aspect) {
            doe_vk_texture_region region = {.mip=mip,
                .extent={width ? width : 1, height ? height : 1, depth ? depth : 1}, .aspect=aspects[aspect]};
            status = doe_vk_commands_upload_texture(commands, zero,
                (doe_vk_texture_buffer_layout){0}, texture, region);
        }
    }
    native_failure failure = native_failure_vulkan(status, &error);
    if (status == DOE_VK_OK) failure = native_submit_commands(device, commands);
    else if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
    doe_vk_buffer_release(zero);
    return failure;
}
