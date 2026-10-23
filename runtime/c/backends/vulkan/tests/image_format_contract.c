/* SPDX-License-Identifier: MIT */
#include "../src/texture_internal.h"
#include <assert.h>
#include <stdio.h>

static VkResult query_result;
static unsigned queries, creations;
static uint32_t expected_samples;
static void format_properties(VkPhysicalDevice device, uint32_t format, VkFormatProperties *out) {
    (void)device;
    *out = (VkFormatProperties){.optimalTilingFeatures =
        format == DOE_VK_FORMAT_RGBA8_UNORM ? VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT :
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT};
}
static VkResult image_properties(VkPhysicalDevice device, uint32_t format, uint32_t type,
    uint32_t tiling, VkFlags usage, VkFlags flags, VkImageFormatProperties *out) {
    (void)device;
    assert(type == DOE_VK_TEXTURE_2D && tiling == VK_IMAGE_TILING_OPTIMAL && flags == 0);
    assert(usage == (format == DOE_VK_FORMAT_RGBA8_UNORM ?
        DOE_VK_TEXTURE_COLOR_ATTACHMENT : DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT));
    ++queries;
    *out = (VkImageFormatProperties){.maxExtent = {64, 64, 1}, .maxMipLevels = 7,
        .maxArrayLayers = 8, .sampleCounts = 5, .maxResourceSize = 65536};
    return query_result;
}
static VkResult create_image(VkDevice device, const VkImageCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkImage *out) {
    (void)device;
    (void)allocator;
    (void)out;
    assert(info->samples == expected_samples);
    ++creations;
    return -1; /* Native allocation failure must preserve the caller output. */
}
static void rejected(doe_vk_context *context, const doe_vk_texture_desc *desc,
    doe_vk_status expected) {
    doe_vk_texture *sentinel = (doe_vk_texture *)context, *out = sentinel;
    unsigned before = creations;
    assert(doe_vk_texture_create(context, desc, &out, NULL) == expected);
    assert(out == sentinel && creations == before && context->references == 1);
}
int main(void) {
    doe_vk_context context = {.references = 1};
    context.vk.vkGetPhysicalDeviceFormatProperties = format_properties;
    context.vk.vkGetPhysicalDeviceImageFormatProperties = image_properties;
    context.vk.vkCreateImage = create_image;
    context.properties.limits.maxImageDimension2D = 128;
    context.properties.limits.maxImageArrayLayers = 16;
    context.properties.limits.framebufferColorSampleCounts = 5;
    context.properties.limits.framebufferDepthSampleCounts = 5;
    context.properties.limits.framebufferStencilSampleCounts = 1;
    doe_vk_format_caps caps = {0};
    assert(doe_vk_texture_format_caps(&context, DOE_VK_FORMAT_RGBA8_UNORM, &caps) == DOE_VK_OK);
    assert(caps.attachment_sample_counts == 5 && caps.color_attachment && !caps.depth_stencil_attachment);
    assert(doe_vk_texture_format_caps(&context, DOE_VK_FORMAT_D32_FLOAT, &caps) == DOE_VK_OK);
    assert(caps.attachment_sample_counts == 5 && caps.depth_stencil_attachment);
    assert(doe_vk_texture_format_caps(&context, DOE_VK_FORMAT_D32_FLOAT_S8_UINT, &caps) == DOE_VK_OK);
    assert(caps.attachment_sample_counts == 1); /* Intersect both framebuffer aspect limits. */
    caps.texel_bytes = 99;
    query_result = -11;
    assert(doe_vk_texture_format_caps(&context, DOE_VK_FORMAT_RGBA8_UNORM, &caps) == DOE_VK_UNSUPPORTED);
    assert(caps.texel_bytes == 99 && caps.attachment_sample_counts == 1);
    query_result = 0;
    doe_vk_texture_desc desc = {.dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_RGBA8_UNORM,
        .width = 8, .height = 8, .depth_or_layers = 1, .mip_levels = 1,
        .usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT, .sample_count = 3};
    rejected(&context, &desc, DOE_VK_INVALID_ARGUMENT);
    desc.sample_count = 8;
    rejected(&context, &desc, DOE_VK_UNSUPPORTED);
    desc.sample_count = 4;
    desc.usage |= DOE_VK_TEXTURE_COPY_SRC;
    rejected(&context, &desc, DOE_VK_UNSUPPORTED);
    desc.usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT;
    desc.depth_or_layers = 2;
    rejected(&context, &desc, DOE_VK_UNSUPPORTED);
    desc.depth_or_layers = 1;
    desc.mip_levels = 2;
    rejected(&context, &desc, DOE_VK_UNSUPPORTED);
    desc.mip_levels = 1;
    desc.width = 65;
    rejected(&context, &desc, DOE_VK_UNSUPPORTED); /* Format-specific bound is below global limit. */
    desc.width = 8;
    expected_samples = 4;
    doe_vk_texture *sentinel = (doe_vk_texture *)&context, *out = sentinel;
    assert(doe_vk_texture_create(&context, &desc, &out, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(out == sentinel && creations == 1 && context.references == 1);
    desc.sample_count = 0;
    expected_samples = 1;
    assert(doe_vk_texture_create(&context, &desc, &out, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(out == sentinel && creations == 2 && context.references == 1 && queries > 6);
    doe_vk_texture texture = {.desc = desc, .texel_bytes = 4};
    texture.desc.sample_count = 4;
    VkBufferImageCopy copy = {.bufferOffset = 99};
    const doe_vk_texture_region region = {.extent = {8, 8, 1}};
    assert(doe_vk_texture_copy_region(&texture, region, 256,
        (doe_vk_texture_buffer_layout){0}, &copy) == DOE_VK_UNSUPPORTED);
    assert(copy.bufferOffset == 99);
    puts("PASS exact native image-format query arguments, actual sample masks and aspect intersections, format-specific limits, MSAA restrictions, allocation rollback, output preservation and copy rejection");
    return 0;
}
