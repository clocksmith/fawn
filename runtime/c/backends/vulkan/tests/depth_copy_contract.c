/* SPDX-License-Identifier: MIT
 * Host contract for depth/stencil transfers: aspect selection, per-aspect linear
 * byte layouts, creation usage admission and per-aspect initialization through
 * the existing texture transfer owner. Mocked native calls are packet evidence,
 * not GPU execution. */
#include "../src/texture_internal.h"
#include <assert.h>
#include <stdio.h>

static uint32_t last_aspect;
static unsigned uploads, reads, image_copies;
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination,
    VkFlags flags, uint32_t memory_count, const void *memory, uint32_t buffer_count,
    const void *buffers, uint32_t image_count, const void *images) {
    (void)command; (void)source; (void)destination; (void)flags; (void)memory_count;
    (void)memory; (void)buffer_count; (void)buffers; (void)image_count;
    if (image_count == 1) {
        /* Layout transitions always cover every aspect of a combined image. */
        const VkImageMemoryBarrier *barrier = images;
        assert(barrier->subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
    }
}
static void upload(VkCommandBuffer command, VkBuffer buffer, VkImage image, uint32_t layout,
    uint32_t count, const VkBufferImageCopy *copy) {
    (void)command; (void)buffer; (void)image;
    assert(layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && count == 1);
    last_aspect = copy->imageSubresource.aspectMask;
    ++uploads;
}
static void readback(VkCommandBuffer command, VkImage image, uint32_t layout, VkBuffer buffer,
    uint32_t count, const VkBufferImageCopy *copy) {
    (void)command; (void)buffer; (void)image;
    assert(layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && count == 1);
    last_aspect = copy->imageSubresource.aspectMask;
    ++reads;
}
static void copy_image(VkCommandBuffer command, VkImage source, uint32_t source_layout,
    VkImage destination, uint32_t destination_layout, uint32_t count, const VkImageCopy *copy) {
    (void)command; (void)source; (void)destination;
    assert(source_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
        destination_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && count == 1);
    assert(copy->srcSubresource.aspectMask == copy->dstSubresource.aspectMask);
    last_aspect = copy->srcSubresource.aspectMask;
    ++image_copies;
}
static doe_vk_texture texture(doe_vk_context *context, doe_vk_texture_format format, uint32_t bytes, uint8_t *initialized) {
    return (doe_vk_texture){.context = context, .references = 1, .texel_bytes = bytes,
        .subresource_count = 1, .initialized = initialized,
        .desc = {.dimension = DOE_VK_TEXTURE_2D, .format = format, .width = 4, .height = 2,
            .depth_or_layers = 1, .mip_levels = 1, .usage = DOE_VK_TEXTURE_COPY_SRC | DOE_VK_TEXTURE_COPY_DST,
            .sample_count = 1}};
}
static void layout(const doe_vk_texture *image, doe_vk_texture_aspect aspect, doe_vk_status expected,
    uint32_t mask, uint32_t row_length, uint64_t offset)
{
    const doe_vk_texture_region region = {.extent = {4, 2, 1}, .aspect = aspect};
    VkBufferImageCopy copy = {.bufferOffset = 99};
    assert(doe_vk_texture_copy_region(image, region, 1024,
        (doe_vk_texture_buffer_layout){offset, 256, 0}, &copy) == expected);
    if (expected != DOE_VK_OK) {
        assert(copy.bufferOffset == 99);
        return;
    }
    assert(copy.imageSubresource.aspectMask == mask && copy.bufferRowLength == row_length &&
        copy.bufferOffset == offset);
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
    context.vk = (doe_vk_functions){.vkCmdPipelineBarrier = barrier, .vkCmdCopyBufferToImage = upload,
        .vkCmdCopyImageToBuffer = readback, .vkCmdCopyImage = copy_image};
    uint8_t state[6] = {0};
    doe_vk_texture d16 = texture(&context, DOE_VK_FORMAT_D16_UNORM, 2, &state[0]);
    doe_vk_texture d32 = texture(&context, DOE_VK_FORMAT_D32_FLOAT, 4, &state[1]);
    doe_vk_texture s8 = texture(&context, DOE_VK_FORMAT_S8_UINT, 1, &state[2]);
    doe_vk_texture d32s8 = texture(&context, DOE_VK_FORMAT_D32_FLOAT_S8_UINT, 8, &state[3]);
    doe_vk_texture d24s8 = texture(&context, DOE_VK_FORMAT_D24_UNORM_S8_UINT, 4, &state[4]);
    doe_vk_texture color = texture(&context, DOE_VK_FORMAT_RGBA8_UNORM, 4, &state[5]);
    /* Per-aspect linear bytes become the native row length in texels. */
    layout(&d16, DOE_VK_ASPECT_ALL, DOE_VK_OK, VK_IMAGE_ASPECT_DEPTH_BIT, 128, 0);
    layout(&d16, DOE_VK_ASPECT_DEPTH, DOE_VK_OK, VK_IMAGE_ASPECT_DEPTH_BIT, 128, 4);
    layout(&d16, DOE_VK_ASPECT_DEPTH, DOE_VK_INVALID_ARGUMENT, 0, 0, 2); /* depth/stencil offsets are 4-aligned */
    layout(&d16, DOE_VK_ASPECT_STENCIL, DOE_VK_INVALID_ARGUMENT, 0, 0, 0);
    layout(&d32, DOE_VK_ASPECT_DEPTH, DOE_VK_OK, VK_IMAGE_ASPECT_DEPTH_BIT, 64, 0);
    layout(&s8, DOE_VK_ASPECT_ALL, DOE_VK_OK, VK_IMAGE_ASPECT_STENCIL_BIT, 256, 4);
    layout(&s8, DOE_VK_ASPECT_DEPTH, DOE_VK_INVALID_ARGUMENT, 0, 0, 0);
    layout(&d32s8, DOE_VK_ASPECT_ALL, DOE_VK_INVALID_ARGUMENT, 0, 0, 0); /* one aspect per buffer copy */
    layout(&d32s8, DOE_VK_ASPECT_DEPTH, DOE_VK_OK, VK_IMAGE_ASPECT_DEPTH_BIT, 64, 0);
    layout(&d32s8, DOE_VK_ASPECT_STENCIL, DOE_VK_OK, VK_IMAGE_ASPECT_STENCIL_BIT, 256, 0);
    layout(&d24s8, DOE_VK_ASPECT_DEPTH, DOE_VK_UNSUPPORTED, 0, 0, 0); /* packed D24 has no linear layout here */
    layout(&d24s8, DOE_VK_ASPECT_STENCIL, DOE_VK_OK, VK_IMAGE_ASPECT_STENCIL_BIT, 256, 0);
    layout(&color, DOE_VK_ASPECT_ALL, DOE_VK_OK, VK_IMAGE_ASPECT_COLOR_BIT, 64, 0);
    layout(&color, DOE_VK_ASPECT_DEPTH, DOE_VK_INVALID_ARGUMENT, 0, 0, 0);
    layout(&color, (doe_vk_texture_aspect)3, DOE_VK_INVALID_ARGUMENT, 0, 0, 0);

    /* Creation admission rejects depth sampling/storage and non-2D shapes before native calls. */
    doe_vk_texture *out = (doe_vk_texture *)&context;
    doe_vk_texture_desc desc = d32.desc;
    desc.usage |= DOE_VK_TEXTURE_SAMPLED;
    assert(doe_vk_texture_create(&context, &desc, &out, NULL) == DOE_VK_UNSUPPORTED && out == (doe_vk_texture *)&context);
    desc.usage = DOE_VK_TEXTURE_COPY_SRC;
    desc.dimension = DOE_VK_TEXTURE_3D;
    assert(doe_vk_texture_create(&context, &desc, &out, NULL) == DOE_VK_UNSUPPORTED && out == (doe_vk_texture *)&context);

    /* Initialization is per aspect: a full stencil upload initializes only stencil. */
    doe_vk_buffer buffer = {.context = &context, .references = 1,
        .usage = DOE_VK_BUFFER_COPY_SRC | DOE_VK_BUFFER_COPY_DST, .size = 1024};
    const doe_vk_texture_region whole = {.extent = {4, 2, 1}, .aspect = DOE_VK_ASPECT_STENCIL};
    doe_vk_commands *commands = recording(&context);
    assert(doe_vk_commands_upload_texture(commands, &buffer, (doe_vk_texture_buffer_layout){0}, &d32s8, whole) == DOE_VK_OK);
    assert(uploads == 1 && last_aspect == VK_IMAGE_ASPECT_STENCIL_BIT);
    assert(doe_vk_commands_read_texture(commands, &d32s8, whole, &buffer, (doe_vk_texture_buffer_layout){0}) == DOE_VK_OK);
    assert(reads == 1 && last_aspect == VK_IMAGE_ASPECT_STENCIL_BIT);
    /* Speculative until completion; success publishes only the stencil bit. */
    assert(commands->textures->initialized[0] == VK_IMAGE_ASPECT_STENCIL_BIT && state[3] == 0);
    doe_vk_textures_complete(commands, DOE_VK_OK);
    assert(state[3] == VK_IMAGE_ASPECT_STENCIL_BIT);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    doe_vk_texture_region depth = whole;
    depth.aspect = DOE_VK_ASPECT_DEPTH;
    /* Undefined depth stays unreadable beside initialized stencil; the failure poisons. */
    commands = recording(&context);
    assert(doe_vk_commands_read_texture(commands, &d32s8, depth, &buffer, (doe_vk_texture_buffer_layout){0}) == DOE_VK_INVALID_STATE);
    assert(reads == 1 && commands->state == DOE_VK_POISONED && state[3] == VK_IMAGE_ASPECT_STENCIL_BIT);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);

    /* A partial stencil upload cannot initialize undefined contents. */
    state[2] = 0;
    commands = recording(&context);
    doe_vk_texture_region partial = {.extent = {2, 2, 1}};
    assert(doe_vk_commands_upload_texture(commands, &buffer, (doe_vk_texture_buffer_layout){0}, &s8, partial) == DOE_VK_INVALID_STATE);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);

    /* Texture-to-texture copies use every aspect; a single aspect mismatch rejects. */
    doe_vk_texture d32s8_copy = texture(&context, DOE_VK_FORMAT_D32_FLOAT_S8_UINT, 8, &state[4]);
    state[3] = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    state[4] = 0;
    commands = recording(&context);
    const doe_vk_texture_region all = {.extent = {4, 2, 1}};
    assert(doe_vk_commands_copy_texture(commands, &d32s8, all, &d32s8_copy, all) == DOE_VK_OK);
    assert(image_copies == 1 && last_aspect == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
    doe_vk_textures_complete(commands, DOE_VK_OK);
    assert(state[4] == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    commands = recording(&context);
    assert(doe_vk_commands_copy_texture(commands, &d32s8, all, &d32s8_copy, depth) == DOE_VK_INVALID_ARGUMENT);
    assert(image_copies == 1 && commands->textures == NULL);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(d32s8.references == 1 && d32s8_copy.references == 1 && buffer.references == 1 && context.references == 1);
    puts("PASS depth/stencil transfer packets: per-aspect D16/D32/D32S8/S8 linear bytes and native aspect masks, 4-byte offsets, single-aspect buffer copies, unsupported packed D24 depth, depth sampling/3D creation rejection, per-aspect initialization publication, partial-write refusal and all-aspect texture copies");
    return 0;
}
