/* SPDX-License-Identifier: MIT */
#include "../src/texture_internal.h"
#include <assert.h>
#include <stdio.h>

static VkResult query_result;
static unsigned queries, creations;
static uint32_t expected_samples;
static VkFlags expected_flags; /* Query and creation flags of the next image. */
static unsigned mutable_queries, volume_queries;
static void format_properties(VkPhysicalDevice device, uint32_t format, VkFormatProperties *out) {
    (void)device;
    *out = (VkFormatProperties){.optimalTilingFeatures =
        format == DOE_VK_FORMAT_RGBA8_UNORM ? VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT :
        format >= DOE_VK_FORMAT_BC1_RGBA_UNORM ? VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT :
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT};
}
static VkResult image_properties(VkPhysicalDevice device, uint32_t format, uint32_t type,
    uint32_t tiling, VkFlags usage, VkFlags flags, VkImageFormatProperties *out) {
    (void)device;
    /* Format caps probe sampled+transfer 3D support (sliced 3D for block formats). */
    if (type == DOE_VK_TEXTURE_3D) {
        assert(tiling == VK_IMAGE_TILING_OPTIMAL && flags == 0 &&
            usage == (DOE_VK_TEXTURE_SAMPLED | DOE_VK_TEXTURE_COPY_SRC | DOE_VK_TEXTURE_COPY_DST));
        ++volume_queries;
        *out = (VkImageFormatProperties){.maxExtent = {64, 64, 64}, .maxMipLevels = 7, .maxArrayLayers = 1, .sampleCounts = 1};
        return query_result;
    }
    /* Attachment sample-mask queries stay flagless; the image query carries the flags. */
    assert(type == DOE_VK_TEXTURE_2D && tiling == VK_IMAGE_TILING_OPTIMAL && (flags == 0 || flags == expected_flags));
    if (flags == VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) ++mutable_queries;
    /* Attachment usage, optionally with SAMPLED (multisampled shader reads) and
     * transfer (multisampled image-to-image copies). */
    assert((usage & ~(VkFlags)(DOE_VK_TEXTURE_SAMPLED | DOE_VK_TEXTURE_COPY_SRC | DOE_VK_TEXTURE_COPY_DST)) == (format == DOE_VK_FORMAT_RGBA8_UNORM ?
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
    assert(info->samples == expected_samples && info->flags == expected_flags);
    if (expected_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
        /* Mutable images carry the exact image-first format list. */
        const VkImageFormatListCreateInfo *list = info->pNext;
        assert(list != NULL && list->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO && list->pNext == NULL);
        assert(list->viewFormatCount == 2 && list->pViewFormats[0] == DOE_VK_FORMAT_RGBA8_UNORM &&
            list->pViewFormats[1] == DOE_VK_FORMAT_RGBA8_SRGB);
    } else assert(info->pNext == NULL);
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
    assert(caps.block_extent == 1 && caps.volume && volume_queries == 1);
    /* BC formats report caps only with enabled textureCompressionBC: 4x4 blocks. */
    assert(doe_vk_texture_format_caps(&context, DOE_VK_FORMAT_BC1_RGBA_UNORM, &caps) == DOE_VK_UNSUPPORTED);
    context.caps.texture_compression_bc = true;
    assert(doe_vk_texture_format_caps(&context, DOE_VK_FORMAT_BC7_RGBA_SRGB, &caps) == DOE_VK_OK);
    assert(caps.block_extent == 4 && caps.texel_bytes == 16);
    assert(doe_vk_texture_format_caps(&context, DOE_VK_FORMAT_BC4_R_SNORM, &caps) == DOE_VK_OK);
    assert(caps.block_extent == 4 && caps.texel_bytes == 8);
    context.caps.texture_compression_bc = false;
    assert(doe_vk_texture_format_caps(&context, DOE_VK_FORMAT_RGBA8_UNORM, &caps) == DOE_VK_OK);
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
    desc.usage |= DOE_VK_TEXTURE_STORAGE;
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
    /* Sampled and copyable multisample images reach native creation; storage does not. */
    desc.usage |= DOE_VK_TEXTURE_SAMPLED;
    assert(doe_vk_texture_create(&context, &desc, &out, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(out == sentinel && creations == 2);
    desc.usage |= DOE_VK_TEXTURE_COPY_SRC | DOE_VK_TEXTURE_COPY_DST;
    assert(doe_vk_texture_create(&context, &desc, &out, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(out == sentinel && creations == 3);
    desc.usage |= DOE_VK_TEXTURE_STORAGE;
    rejected(&context, &desc, DOE_VK_UNSUPPORTED);
    desc.usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT;
    desc.sample_count = 0;
    expected_samples = 1;
    assert(doe_vk_texture_create(&context, &desc, &out, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(out == sentinel && creations == 4 && context.references == 1 && queries > 6);
    /* A listed srgb pair makes the same image mutable with a format list; a list
     * naming only the image format stays immutable. */
    const doe_vk_texture_format pair[2] = {DOE_VK_FORMAT_RGBA8_UNORM, DOE_VK_FORMAT_RGBA8_SRGB};
    desc.view_formats = pair;
    desc.view_format_count = 1;
    assert(doe_vk_texture_create(&context, &desc, &out, NULL) == DOE_VK_OUT_OF_MEMORY);
    desc.view_format_count = 2;
    expected_flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    assert(doe_vk_texture_create(&context, &desc, &out, NULL) == DOE_VK_OUT_OF_MEMORY);
    assert(out == sentinel && creations == 6 && context.references == 1 && mutable_queries == 1);
    expected_flags = 0;
    desc.view_formats = NULL;
    desc.view_format_count = 0;
    doe_vk_texture texture = {.desc = desc, .texel_bytes = 4};
    texture.desc.sample_count = 4;
    VkBufferImageCopy copy = {.bufferOffset = 99};
    const doe_vk_texture_region region = {.extent = {8, 8, 1}};
    assert(doe_vk_texture_copy_region(&texture, region, 256,
        (doe_vk_texture_buffer_layout){0}, &copy) == DOE_VK_UNSUPPORTED);
    assert(copy.bufferOffset == 99);
    /* BC buffer copies count block rows and columns: a 6x6 mip of a 12x12 BC1
     * image is 2x2 blocks; native row length and image height are in texels. */
    texture.desc = (doe_vk_texture_desc){.dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_BC1_RGBA_UNORM,
        .width = 12, .height = 12, .depth_or_layers = 1, .mip_levels = 2, .usage = DOE_VK_TEXTURE_COPY_DST,
        .sample_count = 1};
    texture.texel_bytes = 8;
    const doe_vk_texture_region block = {.mip = 1, .extent = {6, 6, 1}};
    assert(doe_vk_texture_copy_region(&texture, block, 512, (doe_vk_texture_buffer_layout){0, 256, 2}, &copy) == DOE_VK_OK);
    assert(copy.bufferRowLength == 128 && copy.bufferImageHeight == 8 && copy.imageExtent.width == 6);
    assert(doe_vk_texture_copy_region(&texture, block, 256 + 15, (doe_vk_texture_buffer_layout){0, 256, 2}, &copy) ==
        DOE_VK_INVALID_ARGUMENT); /* two block rows need 256 + 16 bytes */
    const doe_vk_texture_region unaligned = {.mip = 0, .origin = {2, 0, 0}, .extent = {4, 4, 1}};
    assert(doe_vk_texture_copy_region(&texture, unaligned, 4096, (doe_vk_texture_buffer_layout){0, 256, 0}, &copy) ==
        DOE_VK_INVALID_ARGUMENT);
    puts("PASS exact native image-format query arguments, actual sample masks and aspect intersections, format-specific limits, BC block caps behind textureCompressionBC, 3D volume probes, MSAA restrictions with sampled/copyable-attachment admission, mutable srgb format-list packets, allocation rollback, output preservation, MSAA buffer-copy rejection and BC block copy regions");
    return 0;
}
