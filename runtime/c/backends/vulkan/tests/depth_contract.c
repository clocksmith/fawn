#include "../src/render_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static unsigned begins, ends, created, destroyed;
static bool inside;
static doe_vk_render_pass_desc expected;
static VkResult create_pass(VkDevice device, const VkRenderPassCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkRenderPass *out) {
    (void)device;
    (void)allocator;
    assert(!inside && info->attachmentCount == 2);
    const VkAttachmentDescription *depth = info->pAttachments + 1;
    assert(depth->format == DOE_VK_FORMAT_D32_FLOAT_S8_UINT);
    assert(depth->loadOp == (expected.clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD));
    assert(depth->stencilLoadOp == (expected.clear_stencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD));
    assert(depth->storeOp == (expected.discard_depth ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE));
    assert(depth->stencilStoreOp == (expected.discard_stencil ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE));
    assert(info->pSubpasses[0].pDepthStencilAttachment->attachment == 1);
    *out = 17;
    ++created;
    return 0;
}
static VkResult framebuffer(VkDevice device, const VkFramebufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFramebuffer *out) {
    (void)device;
    (void)allocator;
    assert(!inside && info->attachmentCount == 2 && info->pAttachments[0] == 21 && info->pAttachments[1] == 22);
    *out = 18;
    return 0;
}
static void destroy_pass(VkDevice device, VkRenderPass pass, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(!inside && pass == 17);
    ++destroyed;
}
static void destroy_framebuffer(VkDevice device, VkFramebuffer buffer, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(!inside && buffer == 18);
}
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination,
    VkFlags flags, uint32_t memory_count, const void *memory, uint32_t buffer_count,
    const void *buffers, uint32_t image_count, const void *images) {
    (void)command; (void)source; (void)destination; (void)flags;
    (void)memory_count; (void)memory; (void)buffer_count; (void)buffers;
    assert(!inside);
    if (image_count) {
        const VkImageMemoryBarrier *image = images;
        assert(image_count == 1);
        assert(image->subresourceRange.aspectMask == (image->image == 11 ? 1u : 6u));
    }
}
static void begin(VkCommandBuffer command, const VkRenderPassBeginInfo *info, uint32_t contents) {
    (void)command;
    assert(!inside && contents == VK_SUBPASS_CONTENTS_INLINE);
    if (expected.clear_depth || expected.clear_stencil) {
        assert(info->clearValueCount == 2);
        assert(info->pClearValues[1].depthStencil.depth == expected.depth_clear_value);
        assert(info->pClearValues[1].depthStencil.stencil == expected.stencil_clear_value);
    }
    inside = true;
    ++begins;
}
static void end(VkCommandBuffer command) {
    (void)command;
    assert(inside);
    inside = false;
    ++ends;
}
static doe_vk_commands *recording(doe_vk_context *context) {
    doe_vk_commands *commands = calloc(1, sizeof(*commands));
    assert(commands != NULL && doe_vk_context_retain(context) == DOE_VK_OK);
    *commands = (doe_vk_commands){.context = context, .state = DOE_VK_RECORDING, .uses_active = true};
    return commands;
}
static void rejected(doe_vk_context *context, const doe_vk_render_pass_desc *desc,
    const doe_vk_render_pass_op *op, doe_vk_status status) {
    doe_vk_commands *commands = recording(context);
    unsigned before = begins;
    assert(doe_vk_commands_render_pass_full(commands, desc, NULL, op, op != NULL ? 1 : 0) == status);
    assert(before == begins && commands->renders == NULL);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
}
int main(void) {
    doe_vk_context context = {.references = 1};
    context.caps.queue_flags = VK_QUEUE_GRAPHICS_BIT;
    context.properties.limits.maxFramebufferWidth = context.properties.limits.maxFramebufferHeight = 64;
    context.properties.limits.maxViewportDimensions[0] = context.properties.limits.maxViewportDimensions[1] = 64;
    context.properties.limits.viewportBoundsRange[1] = 64;
    context.vk.vkCreateRenderPass = create_pass;
    context.vk.vkCreateFramebuffer = framebuffer;
    context.vk.vkDestroyRenderPass = destroy_pass;
    context.vk.vkDestroyFramebuffer = destroy_framebuffer;
    context.vk.vkCmdPipelineBarrier = barrier;
    context.vk.vkCmdBeginRenderPass = begin;
    context.vk.vkCmdEndRenderPass = end;
    uint8_t color_state = 0, depth_state = 0;
    doe_vk_texture color = {.context = &context, .references = 1, .image = 11,
        .subresource_count = 1, .initialized = &color_state,
        .desc = {.dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_RGBA8_UNORM,
            .width = 8, .height = 8, .depth_or_layers = 1, .mip_levels = 1, .usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT}};
    doe_vk_texture depth = color;
    depth.image = 12;
    depth.initialized = &depth_state;
    depth.desc.format = DOE_VK_FORMAT_D32_FLOAT_S8_UINT;
    depth.desc.usage = DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT;
    doe_vk_texture_view color_view = {.texture = &color, .references = 1, .view = 21, .desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1}};
    doe_vk_texture_view depth_view = {.texture = &depth, .references = 1, .view = 22, .desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1}};
    const doe_vk_clear_color blue = {.float32 = {0, 0, 1, 1}};
    doe_vk_render_pass_desc desc = {.color = &color_view, .clear_color = &blue,
        .depth_stencil = &depth_view, .clear_depth = true, .clear_stencil = true,
        .depth_clear_value = 0.75f, .stencil_clear_value = 37};
    desc.depth_read_only = true;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_ARGUMENT);
    desc.depth_read_only = false;
    desc.depth_clear_value = NAN;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_ARGUMENT);
    desc.depth_clear_value = 0.75f;
    depth.desc.width = 4;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_ARGUMENT);
    depth.desc.width = 8;
    expected = desc;
    doe_vk_commands *commands = recording(&context);
    assert(doe_vk_commands_render_pass_full(commands, &desc, NULL, NULL, 0) == DOE_VK_OK);
    assert(color_state == 0 && depth_state == 0 && depth_view.references == 2 && depth.references == 2);
    doe_vk_textures_complete(commands, DOE_VK_OK);
    assert(color_state == 1 && depth_state == 6);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(depth_view.references == 1 && depth.references == 1);
    desc.clear_color = NULL;
    desc.clear_depth = desc.clear_stencil = false;
    desc.discard_depth = true;
    expected = desc;
    commands = recording(&context);
    assert(doe_vk_commands_render_pass_full(commands, &desc, NULL, NULL, 0) == DOE_VK_OK);
    doe_vk_textures_complete(commands, DOE_VK_OK);
    assert(depth_state == 4 && color_state == 1);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    desc.discard_depth = false;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_STATE);
    desc.clear_depth = true;
    expected = desc;
    commands = recording(&context);
    assert(doe_vk_commands_render_pass_full(commands, &desc, NULL, NULL, 0) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(depth_state == 4); /* Abandoning clear must not publish it. */
    desc.clear_depth = false;
    desc.depth_read_only = desc.stencil_read_only = true;
    doe_vk_pipeline pipeline = {.context = &context, .references = 1, .graphics = true,
        .color_format = color.desc.format, .depth_stencil_format = depth.desc.format, .depth_write = true};
    doe_vk_render_pass_op op = {.kind = DOE_VK_RENDER_DRAW,
        .value.draw = {.pipeline = &pipeline, .draw = {.vertex_count = 3, .instance_count = 1,
            .viewport = {0, 0, 8, 8, 0, 1}, .scissor = {0, 0, 8, 8}}}};
    rejected(&context, &desc, &op, DOE_VK_INVALID_ARGUMENT);
    pipeline.depth_write = false;
    pipeline.stencil_write = true;
    rejected(&context, &desc, &op, DOE_VK_INVALID_ARGUMENT);
    depth.desc.format = DOE_VK_FORMAT_D32_FLOAT;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_ARGUMENT); /* No stencil aspect. */
    assert(begins == 3 && ends == 3 && created == destroyed && context.references == 1);
    puts("PASS per-aspect native load/clear/store/discard, full depth/stencil transitions, read-only write/clear rejection, extent/NaN/absent-aspect preflight, speculative initialization and exact attachment leases");
    return 0;
}
