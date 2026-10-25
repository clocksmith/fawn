/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>

/* Records real zero initialization of every mip/layer of texture into
 * commands. Attachment-only images without internal CopyDst (depth/stencil,
 * multisample and borrowed surface frames) are cleared by the canonical
 * attachment owner; others upload zero texel blocks from host staging that
 * the recording retains. Never publishes allocator contents. */
static doe_vk_status record_initialization(native_device *device, doe_vk_commands *commands,
    doe_vk_texture *texture, const doe_vk_texture_desc *desc, const doe_vk_format_caps *caps, doe_vk_error *error)
{
    const uint32_t texel_bytes = caps->texel_bytes; /* Bytes of one texel block. */
    if ((desc->usage & DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT) || desc->sample_count > 1 ||
        !(desc->usage & DOE_VK_TEXTURE_COPY_DST)) {
        doe_vk_texture_view *view = NULL;
        doe_vk_texture_view_desc span = {.dimension=DOE_VK_VIEW_2D_ARRAY,
            .mip_count=desc->mip_levels,.layer_count=desc->depth_or_layers};
        doe_vk_status status = doe_vk_texture_view_create(texture, &span, &view, error);
        if (status == DOE_VK_OK) status = doe_vk_commands_initialize_attachment(commands, view);
        doe_vk_texture_view_release(view); /* The recorded pass retains its view. */
        return status;
    }
    /* Zero staging holds mip 0's texel blocks (whole 4x4 blocks for BC). */
    const uint64_t block = caps->block_extent ? caps->block_extent : 1;
    uint64_t size = (desc->width + block - 1) / block;
    const uint64_t rows = (desc->height + block - 1) / block;
    if (size > SIZE_MAX / rows) return DOE_VK_OUT_OF_MEMORY;
    size *= rows;
    if (size > SIZE_MAX / desc->depth_or_layers) return DOE_VK_OUT_OF_MEMORY;
    size *= desc->depth_or_layers;
    if (size > SIZE_MAX / texel_bytes) return DOE_VK_OUT_OF_MEMORY;
    size *= texel_bytes;
    doe_vk_buffer *zero = NULL;
    doe_vk_status status = doe_vk_buffer_create(device->backend.value.vulkan.context, size,
        DOE_VK_BUFFER_COPY_SRC, DOE_VK_MEMORY_HOST, &zero, error);
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
    doe_vk_buffer_release(zero); /* The recorded uploads retain the staging. */
    return status;
}

/* Eager initialization through its own private submission, waited before the
 * image is published (borrowed surface frames, whose presentation waits). */
native_failure native_initialize_texture(native_device *device, doe_vk_texture *texture,
    const doe_vk_texture_desc *desc, const doe_vk_format_caps *caps)
{
    doe_vk_commands *commands = NULL;
    doe_vk_error error = {0};
    doe_vk_status status = doe_vk_commands_create(device->backend.value.vulkan.context, &commands, &error);
    if (status == DOE_VK_OK) status = record_initialization(device, commands, texture, desc, caps, &error);
    native_failure failure = native_failure_vulkan(status, &error);
    if (status == DOE_VK_OK) failure = native_submit_commands(device, commands);
    else if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
    return failure;
}

/* Public textures record their initialization into one device-owned batch
 * that the next submission flushes ahead of its own work (owner authorized
 * 2026-10-24: async submit A+B, D2, fence-wait completion; futures later;
 * reconfirmed after the section 7 risk review). Every access to a texture's
 * contents is a queue-ordered submission, so zeros are in place before any
 * first use. A failed recording abandons the batch, poisoning every texture
 * it held rather than exposing allocator contents. */
native_failure native_queue_texture_initialization(native_device *device, doe_vk_texture *texture,
    const doe_vk_texture_desc *desc, const doe_vk_format_caps *caps)
{
    doe_vk_error error = {0};
    doe_vk_status status = DOE_VK_OK;
    if (!device->init_batch)
        status = doe_vk_commands_create(device->backend.value.vulkan.context, &device->init_batch, &error);
    if (status == DOE_VK_OK) status = record_initialization(device, device->init_batch, texture, desc, caps, &error);
    if (status != DOE_VK_OK && device->init_batch) {
        doe_vk_commands_abandon(device->init_batch, status);
        if (doe_vk_commands_release(device->init_batch) != DOE_VK_OK) abort();
        device->init_batch = NULL;
    }
    return native_failure_vulkan(status, &error);
}

void native_release_initialization(native_device *device)
{
    if (!device->init_batch) return;
    /* Never submitted: its textures are unobservable without a submission. */
    if (doe_vk_commands_release(device->init_batch) != DOE_VK_OK) abort();
    device->init_batch = NULL;
}
