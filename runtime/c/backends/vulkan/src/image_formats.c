/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"

doe_vk_texture_format doe_vk_srgb_pair(doe_vk_texture_format format) {
    switch (format) {
        case DOE_VK_FORMAT_RGBA8_UNORM: return DOE_VK_FORMAT_RGBA8_SRGB;
        case DOE_VK_FORMAT_RGBA8_SRGB: return DOE_VK_FORMAT_RGBA8_UNORM;
        case DOE_VK_FORMAT_BGRA8_UNORM: return DOE_VK_FORMAT_BGRA8_SRGB;
        case DOE_VK_FORMAT_BGRA8_SRGB: return DOE_VK_FORMAT_BGRA8_UNORM;
        default: return (doe_vk_texture_format)0;
    }
}

doe_vk_status doe_vk_image_format_properties(doe_vk_context *context,
    const doe_vk_texture_desc *desc, VkImageFormatProperties *out) {
    VkImageFormatProperties properties = {0};
    VkFlags flags = desc->cube_compatible ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    for (uint32_t i = 0; i < desc->view_format_count; ++i)
        if (desc->view_formats[i] != desc->format) flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    VkResult result = context->vk.vkGetPhysicalDeviceImageFormatProperties(context->physical,
        (uint32_t)desc->format, (uint32_t)desc->dimension, VK_IMAGE_TILING_OPTIMAL, desc->usage,
        flags, &properties);
    doe_vk_status status = doe_vk_result(context, result, "vkGetPhysicalDeviceImageFormatProperties", NULL);
    if (status != DOE_VK_OK) return status;
    *out = properties;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_attachment_samples(doe_vk_context *context,
    doe_vk_texture_format format, uint32_t *out) {
    uint32_t aspects = doe_vk_format_aspects(format);
    const doe_vk_texture_desc desc = {.dimension = DOE_VK_TEXTURE_2D, .format = format,
        .usage = aspects == VK_IMAGE_ASPECT_COLOR_BIT ?
            DOE_VK_TEXTURE_COLOR_ATTACHMENT : DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT};
    VkImageFormatProperties properties;
    doe_vk_status status = doe_vk_image_format_properties(context, &desc, &properties);
    if (status != DOE_VK_OK) return status;
    uint32_t samples = properties.sampleCounts;
    const VkPhysicalDeviceLimits *limits = &context->properties.limits;
    if (aspects & VK_IMAGE_ASPECT_COLOR_BIT) samples &= limits->framebufferColorSampleCounts;
    if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT) samples &= limits->framebufferDepthSampleCounts;
    if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) samples &= limits->framebufferStencilSampleCounts;
    *out = samples;
    return DOE_VK_OK;
}
