#include "../src/texture_internal.h"
#include <assert.h>
#include <stdio.h>

static void transfer_only_format(VkPhysicalDevice device, uint32_t format, VkFormatProperties *out) {
    (void)device;
    assert(format == DOE_VK_FORMAT_RGBA8_UNORM);
    out->optimalTilingFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
}

static void format_admission(void) {
    doe_vk_context context = { .references = 1 };
    context.vk.vkGetPhysicalDeviceFormatProperties = transfer_only_format;
    context.properties.limits.maxImageDimension2D = 64;
    context.properties.limits.maxImageArrayLayers = 1;
    doe_vk_format_caps caps;
    assert(doe_vk_texture_format_caps(&context, DOE_VK_FORMAT_RGBA8_UNORM, &caps) == DOE_VK_OK);
    assert(caps.texel_bytes == 4 && caps.copy_src && caps.copy_dst && !caps.sampled && !caps.storage);
    doe_vk_format_caps before = caps;
    assert(doe_vk_texture_format_caps(&context, (doe_vk_texture_format)999, &caps) == DOE_VK_UNSUPPORTED);
    assert(memcmp(&before, &caps, sizeof(caps)) == 0);
    doe_vk_texture_desc desc = { .dimension = DOE_VK_TEXTURE_2D,
        .format = DOE_VK_FORMAT_RGBA8_UNORM, .width = 4, .height = 4,
        .depth_or_layers = 1, .mip_levels = 1, .usage = DOE_VK_TEXTURE_SAMPLED };
    doe_vk_texture *texture = NULL;
    assert(doe_vk_texture_create(&context, &desc, &texture, NULL) == DOE_VK_UNSUPPORTED);
    assert(texture == NULL && context.references == 1);
    desc.usage = DOE_VK_TEXTURE_STORAGE;
    assert(doe_vk_texture_create(&context, &desc, &texture, NULL) == DOE_VK_UNSUPPORTED);
    context.lost = true;
    assert(doe_vk_texture_format_caps(&context, DOE_VK_FORMAT_RGBA8_UNORM, &caps) == DOE_VK_DEVICE_LOST);
    assert(memcmp(&before, &caps, sizeof(caps)) == 0);
}

static void bounds_and_pitch(void) {
    doe_vk_texture texture = {
        .desc = { .dimension = DOE_VK_TEXTURE_2D, .width = 8, .height = 4,
            .depth_or_layers = 3, .mip_levels = 3 },
        .texel_bytes = 4
    };
    doe_vk_texture_region region = { .mip = 1, .origin = {0, 0, 1}, .extent = {4, 2, 2} };
    doe_vk_texture_buffer_layout layout = {16, 256, 3};
    VkBufferImageCopy copy = {0};
    uint64_t minimum = 16 + 256 * 3 + 256 + 16;
    assert(doe_vk_texture_copy_region(&texture, region, minimum, layout, &copy) == DOE_VK_OK);
    assert(copy.bufferRowLength == 64 && copy.bufferImageHeight == 3);
    assert(copy.imageSubresource.baseArrayLayer == 1 && copy.imageSubresource.layerCount == 2);
    assert(copy.imageOffset.z == 0 && copy.imageExtent.depth == 1);
    VkBufferImageCopy before = copy;
    assert(doe_vk_texture_copy_region(&texture, region, minimum - 1, layout, &copy) == DOE_VK_INVALID_ARGUMENT);
    assert(memcmp(&before, &copy, sizeof(copy)) == 0);
    layout.offset = UINT64_MAX - 3;
    assert(doe_vk_texture_copy_region(&texture, region, UINT64_MAX, layout, &copy) == DOE_VK_INVALID_ARGUMENT);
    layout = (doe_vk_texture_buffer_layout){ 0, UINT32_MAX - 3, UINT32_MAX };
    region = (doe_vk_texture_region){ .extent = {8, 4, 3} };
    assert(doe_vk_texture_copy_region(&texture, region, UINT64_MAX, layout, &copy) == DOE_VK_INVALID_ARGUMENT);
    layout = (doe_vk_texture_buffer_layout){0};
    texture.desc.dimension = DOE_VK_TEXTURE_3D;
    texture.desc.depth_or_layers = 8;
    region = (doe_vk_texture_region){ .mip = 1, .origin = {0, 0, 1}, .extent = {4, 2, 2} };
    assert(doe_vk_texture_copy_region(&texture, region, 64, layout, &copy) == DOE_VK_OK);
    assert(copy.imageSubresource.layerCount == 1 && copy.imageSubresource.baseArrayLayer == 0);
    assert(copy.imageOffset.z == 1 && copy.imageExtent.depth == 2);
    region.mip = 32;
    assert(doe_vk_texture_copy_region(&texture, region, UINT64_MAX, layout, &copy) == DOE_VK_INVALID_ARGUMENT);
}

static void queue_transfer_granularity(void) {
    doe_vk_context context = { .transfer_granularity = {4, 4, 1} };
    doe_vk_texture texture = { .desc = { .dimension = DOE_VK_TEXTURE_2D,
        .width = 10, .height = 10, .depth_or_layers = 2, .mip_levels = 1 } };
    doe_vk_texture_region region = { .origin = {4, 4, 1}, .extent = {6, 6, 1} };
    assert(doe_vk_texture_transfer_region(&context, &texture, region) == DOE_VK_OK);
    region.origin[0] = 2;
    assert(doe_vk_texture_transfer_region(&context, &texture, region) == DOE_VK_UNSUPPORTED);
    region.origin[0] = 4;
    region.extent[0] = 2;
    assert(doe_vk_texture_transfer_region(&context, &texture, region) == DOE_VK_UNSUPPORTED);
    context.transfer_granularity = (VkExtent3D){0};
    region = (doe_vk_texture_region){ .extent = {10, 10, 2} };
    assert(doe_vk_texture_transfer_region(&context, &texture, region) == DOE_VK_OK);
    region.extent[1] = 8;
    assert(doe_vk_texture_transfer_region(&context, &texture, region) == DOE_VK_UNSUPPORTED);
}

static void layout_and_initialization_transaction(void) {
    doe_vk_context context = { .references = 1 };
    doe_vk_texture texture = {
        .context = &context,
        .references = 1,
        .desc = { .dimension = DOE_VK_TEXTURE_2D, .width = 4, .height = 4,
            .depth_or_layers = 2, .mip_levels = 2 },
        .subresource_count = 4
    };
    uint8_t initialized[4] = {0};
    texture.initialized = initialized;
    doe_vk_commands first = { .context = &context };
    doe_vk_commands second = { .context = &context };
    doe_vk_texture_use *use = NULL;
    assert(doe_vk_use_texture(&first, &texture, &use) == DOE_VK_OK);
    use->layout = VK_IMAGE_LAYOUT_GENERAL;
    use->initialized[1] = 1;
    assert(texture.layout == VK_IMAGE_LAYOUT_UNDEFINED && initialized[1] == 0);
    doe_vk_texture_use *other = NULL;
    assert(doe_vk_use_texture(&second, &texture, &other) == DOE_VK_BUSY);
    doe_vk_textures_release(&first);
    assert(texture.references == 1 && texture.recording_owner == NULL);
    assert(texture.layout == VK_IMAGE_LAYOUT_UNDEFINED && initialized[1] == 0);
    assert(doe_vk_use_texture(&first, &texture, &use) == DOE_VK_OK);
    use->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    use->initialized[2] = 1;
    doe_vk_textures_complete(&first, DOE_VK_OK);
    assert(texture.layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && initialized[2] == 1);
    assert(doe_vk_use_texture(&second, &texture, &other) == DOE_VK_OK);
    assert(other->layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && other->initialized[2] == 1);
    doe_vk_textures_release(&first);
    assert(texture.recording_owner == &second);
    doe_vk_textures_complete(&second, DOE_VK_OUT_OF_MEMORY);
    assert(texture.content_status == DOE_VK_OUT_OF_MEMORY);
    doe_vk_textures_release(&second);
    assert(doe_vk_use_texture(&first, &texture, &use) == DOE_VK_OUT_OF_MEMORY);
    assert(texture.references == 1 && texture.recording_owner == NULL);
}

int main(void) {
    format_admission();
    bounds_and_pitch();
    queue_transfer_granularity();
    layout_and_initialization_transaction();
    puts("PASS texture last-byte/mip/layer/overflow checks, speculative layout/initialization rollback, concurrent owner exclusion and failed output");
    return 0;
}
