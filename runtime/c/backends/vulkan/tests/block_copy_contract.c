/* SPDX-License-Identifier: MIT
 * Host contract for block-compressed texture-to-texture copies. Equal clamped
 * extents use one VkImageCopy; a block whose extent reaches one side's edge but
 * not the other's has no valid VkImageCopy region (each side clamps to its own
 * edge), so it is staged through a device buffer: image-to-buffer with the
 * source extent, then buffer-to-image with the destination extent, over the
 * same whole-block rows. Mocked native calls are packet evidence only. */
#include "../src/texture_internal.h"
#include <assert.h>
#include <stdio.h>

static unsigned image_copies, reads, uploads, buffers_created, buffers_destroyed;
static VkBufferImageCopy last_read, last_upload;
static VkImageCopy last_copy;
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination, VkFlags flags,
    uint32_t memory_count, const void *memory, uint32_t buffer_count, const void *buffers,
    uint32_t image_count, const void *images) {
    (void)command; (void)source; (void)destination; (void)flags; (void)memory_count;
    (void)memory; (void)buffer_count; (void)buffers; (void)image_count; (void)images;
}
static void copy_image(VkCommandBuffer command, VkImage source, uint32_t source_layout,
    VkImage destination, uint32_t destination_layout, uint32_t count, const VkImageCopy *copy) {
    (void)command; (void)source; (void)destination; (void)source_layout; (void)destination_layout;
    assert(count == 1);
    last_copy = *copy;
    ++image_copies;
}
static void readback(VkCommandBuffer command, VkImage image, uint32_t layout, VkBuffer buffer,
    uint32_t count, const VkBufferImageCopy *copy) {
    (void)command; (void)image;
    assert(layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && count == 1 && buffer == 31);
    last_read = *copy;
    ++reads;
}
static void upload(VkCommandBuffer command, VkBuffer buffer, VkImage image, uint32_t layout,
    uint32_t count, const VkBufferImageCopy *copy) {
    (void)command; (void)image;
    assert(layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && count == 1 && buffer == 31);
    last_upload = *copy;
    ++uploads;
}
static VkResult create_buffer(VkDevice device, const VkBufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkBuffer *out) {
    (void)device; (void)allocator;
    assert(info->size == 8); /* one BC1 block */
    *out = 31;
    ++buffers_created;
    return 0;
}
static void destroy_buffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(buffer == 31);
    ++buffers_destroyed;
}
static void requirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *out) {
    (void)device; (void)buffer;
    *out = (VkMemoryRequirements){256, 64, 1};
}
static VkResult allocate_memory(VkDevice device, const VkMemoryAllocateInfo *info,
    const VkAllocationCallbacks *allocator, VkDeviceMemory *out) {
    (void)device; (void)info; (void)allocator;
    *out = 32;
    return 0;
}
static void free_memory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(memory == 32);
}
static VkResult bind_memory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    (void)device; (void)buffer; (void)memory; (void)offset;
    return 0;
}
static doe_vk_texture texture(doe_vk_context *context, uint32_t size, uint32_t mips, uint8_t *initialized) {
    return (doe_vk_texture){.context = context, .references = 1, .texel_bytes = 8,
        .subresource_count = mips, .initialized = initialized,
        .desc = {.dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_BC1_RGBA_UNORM, .width = size,
            .height = size, .depth_or_layers = 1, .mip_levels = mips,
            .usage = DOE_VK_TEXTURE_COPY_SRC | DOE_VK_TEXTURE_COPY_DST | DOE_VK_TEXTURE_SAMPLED, .sample_count = 1}};
}
static doe_vk_commands *recording(doe_vk_context *context) {
    doe_vk_commands *commands = calloc(1, sizeof(*commands));
    assert(commands != NULL && doe_vk_context_retain(context) == DOE_VK_OK);
    *commands = (doe_vk_commands){.context = context, .state = DOE_VK_RECORDING, .uses_active = true};
    return commands;
}
int main(void) {
    doe_vk_context context = {.references = 1};
    context.transfer_granularity = (VkExtent3D){1, 1, 1};
    context.memory.memoryTypeCount = 1;
    context.memory.memoryHeapCount = 1;
    context.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    context.vk = (doe_vk_functions){.vkCmdPipelineBarrier = barrier, .vkCmdCopyImage = copy_image,
        .vkCmdCopyImageToBuffer = readback, .vkCmdCopyBufferToImage = upload, .vkCreateBuffer = create_buffer,
        .vkDestroyBuffer = destroy_buffer, .vkGetBufferMemoryRequirements = requirements,
        .vkAllocateMemory = allocate_memory, .vkFreeMemory = free_memory, .vkBindBufferMemory = bind_memory};
    uint8_t edge_state[2] = {1, 1}, wide_state[1] = {1};
    doe_vk_texture edge = texture(&context, 12, 2, edge_state); /* mip 1: 6x6, physical 8x8 */
    doe_vk_texture wide = texture(&context, 16, 1, wide_state);

    /* Mip 1 block (1,0) is 2 texels wide at its edge; wide block (1,0) is 4. */
    doe_vk_commands *commands = recording(&context);
    const doe_vk_texture_region from_edge = {.mip = 1, .origin = {4, 0, 0}, .extent = {4, 4, 1}};
    const doe_vk_texture_region into_wide = {.origin = {4, 0, 0}, .extent = {4, 4, 1}};
    assert(doe_vk_commands_copy_texture(commands, &edge, from_edge, &wide, into_wide) == DOE_VK_OK);
    assert(image_copies == 0 && reads == 1 && uploads == 1 && buffers_created == 1);
    assert(last_read.imageExtent.width == 2 && last_read.imageExtent.height == 4 && last_read.imageOffset.x == 4 &&
        last_read.imageSubresource.mipLevel == 1 && last_read.bufferRowLength == 4 && last_read.bufferImageHeight == 4);
    assert(last_upload.imageExtent.width == 4 && last_upload.imageExtent.height == 4 && last_upload.imageOffset.x == 4 &&
        last_upload.imageSubresource.mipLevel == 0 && last_upload.bufferRowLength == 4 && last_upload.bufferImageHeight == 4);
    /* Middle to edge: the reverse staging, clamped on the destination side. */
    const doe_vk_texture_region from_wide = {.origin = {8, 8, 0}, .extent = {4, 4, 1}};
    const doe_vk_texture_region into_edge = {.mip = 1, .origin = {4, 4, 0}, .extent = {4, 4, 1}};
    assert(doe_vk_commands_copy_texture(commands, &wide, from_wide, &edge, into_edge) == DOE_VK_OK);
    assert(image_copies == 0 && reads == 2 && uploads == 2 && buffers_created == 2);
    assert(last_read.imageExtent.width == 4 && last_read.imageExtent.height == 4 &&
        last_upload.imageExtent.width == 2 && last_upload.imageExtent.height == 2 && last_upload.imageOffset.y == 4);
    /* Equal clamped extents (both at their edges, or both whole blocks) copy directly. */
    const doe_vk_texture_region whole_block = {.extent = {4, 4, 1}};
    assert(doe_vk_commands_copy_texture(commands, &edge, whole_block, &wide, whole_block) == DOE_VK_OK);
    assert(image_copies == 1 && buffers_created == 2 && last_copy.extent.width == 4);
    const doe_vk_texture_region edge_corner = {.mip = 1, .origin = {4, 4, 0}, .extent = {4, 4, 1}};
    doe_vk_texture other = texture(&context, 12, 2, (uint8_t[2]){1, 1});
    assert(doe_vk_commands_copy_texture(commands, &edge, edge_corner, &other, edge_corner) == DOE_VK_OK);
    assert(image_copies == 2 && buffers_created == 2 && last_copy.extent.width == 2 && last_copy.extent.height == 2);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(buffers_destroyed == 2 && context.references == 1);
    puts("PASS block copy staging: edge-to-middle and middle-to-edge BC1 blocks stage through one block of device "
        "buffer with per-side clamped extents; equal clamped extents copy directly; host only");
    return 0;
}
