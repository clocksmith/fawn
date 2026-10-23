/* SPDX-License-Identifier: MIT */
#include "../src/render_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static unsigned begins, ends, created, destroyed, views_created, views_destroyed;
static VkResult create_view(VkDevice device, const VkImageViewCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkImageView *out) {
    (void)device; (void)allocator;
    assert(info->subresourceRange.levelCount == 1 && info->subresourceRange.layerCount == 1);
    *out = 22; /* The depth attachment handle the framebuffer mock expects. */
    ++views_created;
    return 0;
}
static void destroy_view(VkDevice device, VkImageView view, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(view == 22);
    ++views_destroyed;
}
static bool inside;
static doe_vk_render_pass_desc expected;
static VkResult create_pass(VkDevice device, const VkRenderPassCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkRenderPass *out) {
    (void)device;
    (void)allocator;
    unsigned depth_index = expected.color != NULL ? 1 : 0;
    assert(!inside && info->attachmentCount == depth_index + 1 + (expected.resolve_target != NULL ? 1u : 0u));
    const VkAttachmentDescription *depth = info->pAttachments + depth_index;
    assert(depth->format == DOE_VK_FORMAT_D32_FLOAT_S8_UINT);
    assert(depth->samples == doe_vk_samples(expected.depth_stencil->texture->desc.sample_count));
    if (expected.resolve_target != NULL) {
        const VkAttachmentDescription *resolve = info->pAttachments + 2;
        assert(info->pAttachments[0].samples == 4 && depth->samples == 4);
        assert(resolve->samples == 1 && resolve->format == DOE_VK_FORMAT_RGBA8_UNORM);
        assert(resolve->loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE && resolve->storeOp == VK_ATTACHMENT_STORE_OP_STORE);
        assert(info->pSubpasses[0].pResolveAttachments->attachment == 2);
    } else assert(info->pSubpasses[0].pResolveAttachments == NULL);
    assert(depth->loadOp == (expected.clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD));
    assert(depth->stencilLoadOp == (expected.clear_stencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD));
    assert(depth->storeOp == (expected.discard_depth ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE));
    assert(depth->stencilStoreOp == (expected.discard_stencil ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE));
    assert(info->pSubpasses[0].pDepthStencilAttachment->attachment == depth_index);
    *out = 17;
    ++created;
    return 0;
}
static VkResult framebuffer(VkDevice device, const VkFramebufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFramebuffer *out) {
    (void)device;
    (void)allocator;
    assert(!inside);
    if (expected.color != NULL) {
        assert(info->attachmentCount == (expected.resolve_target != NULL ? 3u : 2u) &&
            info->pAttachments[0] == 21 && info->pAttachments[1] == 22);
        if (expected.resolve_target != NULL) assert(info->pAttachments[2] == 23);
    } else assert(info->attachmentCount == 1 && info->pAttachments[0] == 22);
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
        assert(image->subresourceRange.aspectMask == (image->image == 12 ? 6u : 1u));
    }
}
static void begin(VkCommandBuffer command, const VkRenderPassBeginInfo *info, uint32_t contents) {
    (void)command;
    assert(!inside && contents == VK_SUBPASS_CONTENTS_INLINE);
    if (expected.clear_depth || expected.clear_stencil) {
        unsigned index = expected.color != NULL ? 1 : 0;
        assert(info->clearValueCount == index + 1);
        assert(info->pClearValues[index].depthStencil.depth == expected.depth_clear_value);
        assert(info->pClearValues[index].depthStencil.stencil == expected.stencil_clear_value);
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
static void stencil_write_summary(void) {
    doe_vk_render_pipeline_desc desc = {.stencil_test = true, .stencil_write_mask = 255,
        .stencil_front = {.pass_operation = 2}};
    assert(doe_vk_render_stencil_writes(&desc));
    desc.cull_mask = 1;
    assert(!doe_vk_render_stencil_writes(&desc));
    desc.cull_mask = 2;
    assert(doe_vk_render_stencil_writes(&desc));
    desc.stencil_front.pass_operation = 0;
    desc.stencil_back.depth_fail_operation = 2;
    assert(!doe_vk_render_stencil_writes(&desc));
    desc.cull_mask = 1;
    assert(doe_vk_render_stencil_writes(&desc));
    desc.cull_mask = 3;
    assert(!doe_vk_render_stencil_writes(&desc));
    desc.cull_mask = 0;
    desc.stencil_write_mask = 0;
    assert(!doe_vk_render_stencil_writes(&desc));
    desc.stencil_write_mask = 255;
    desc.stencil_test = false;
    assert(!doe_vk_render_stencil_writes(&desc));
}
int main(void) {
    stencil_write_summary();
    doe_vk_context context = {.references = 1};
    context.caps.queue_flags = VK_QUEUE_GRAPHICS_BIT;
    context.caps.max_color_attachments = 8;
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
    doe_vk_texture_view color_view = {.texture = &color, .references = 1, .aspects = 1, .format = DOE_VK_FORMAT_RGBA8_UNORM, .view = 21, .desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1, DOE_VK_ASPECT_ALL, 0}};
    doe_vk_texture_view depth_view = {.texture = &depth, .references = 1, .aspects = 6, .format = DOE_VK_FORMAT_D32_FLOAT_S8_UINT, .view = 22, .desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1, DOE_VK_ASPECT_ALL, 0}};
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
        .color_count = 1, .color_formats = {color.desc.format}, .depth_stencil_format = depth.desc.format, .depth_write = true};
    doe_vk_render_pass_op op = {.kind = DOE_VK_RENDER_DRAW,
        .value.draw = {.pipeline = &pipeline, .draw = {.vertex_count = 3, .instance_count = 1,
            .viewport = {0, 0, 8, 8, 0, 1}, .scissor = {0, 0, 8, 8}}}};
    rejected(&context, &desc, &op, DOE_VK_INVALID_ARGUMENT);
    pipeline.depth_write = false;
    pipeline.stencil_write = true;
    rejected(&context, &desc, &op, DOE_VK_INVALID_ARGUMENT);
    expected = (doe_vk_render_pass_desc){.depth_stencil = &depth_view, .clear_depth = true};
    commands = recording(&context);
    assert(doe_vk_commands_initialize_attachment(commands, &depth_view) == DOE_VK_OK);
    assert(depth_state == 4 && begins == 4); /* Real depth-only clear, preserve stencil. */
    doe_vk_textures_complete(commands, DOE_VK_OK);
    assert(depth_state == 6);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    commands = recording(&context);
    assert(doe_vk_commands_initialize_attachment(commands, &depth_view) == DOE_VK_OK);
    assert(commands->renders == NULL && begins == 4);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    depth.desc.format = DOE_VK_FORMAT_D32_FLOAT;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_ARGUMENT); /* No stencil aspect. */
    depth.desc.format = DOE_VK_FORMAT_D32_FLOAT_S8_UINT;
    doe_vk_render_pass_desc depth_only = {.depth_stencil = &depth_view,
        .depth_read_only = true, .stencil_read_only = true};
    rejected(&context, &depth_only, &op, DOE_VK_INVALID_ARGUMENT); /* Color pipeline mismatch. */
    pipeline.color_count = 0;
    pipeline.color_formats[0] = 0;
    pipeline.depth_stencil_format = 0;
    rejected(&context, &depth_only, &op, DOE_VK_INVALID_ARGUMENT);
    pipeline.depth_stencil_format = depth.desc.format;
    pipeline.depth_write = true;
    rejected(&context, &depth_only, &op, DOE_VK_INVALID_ARGUMENT);
    color.desc.sample_count = depth.desc.sample_count = 4;
    uint8_t resolve_state = 0;
    doe_vk_texture resolve = color;
    resolve.image = 13;
    resolve.initialized = &resolve_state;
    resolve.desc.sample_count = 1;
    doe_vk_texture_view resolve_view = {.texture = &resolve, .references = 1, .aspects = 1, .format = DOE_VK_FORMAT_RGBA8_UNORM, .view = 23,
        .desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1, DOE_VK_ASPECT_ALL, 0}};
    desc = (doe_vk_render_pass_desc){.color = &color_view, .clear_color = &blue,
        .discard_color = true, .depth_stencil = &depth_view, .clear_depth = true,
        .clear_stencil = true, .resolve_target = &resolve_view};
    resolve.desc.sample_count = 4;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_ARGUMENT);
    resolve.desc.sample_count = 1;
    resolve.desc.format = resolve_view.format = DOE_VK_FORMAT_BGRA8_UNORM;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_ARGUMENT);
    resolve.desc.format = resolve_view.format = DOE_VK_FORMAT_RGBA8_UNORM;
    resolve.desc.width = 4;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_ARGUMENT);
    resolve.desc.width = 8;
    depth.desc.sample_count = 1;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_ARGUMENT);
    depth.desc.sample_count = 4;
    color.desc.sample_count = 1;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_ARGUMENT);
    color.desc.sample_count = 4;
    doe_vk_context other = {.references = 1};
    resolve.context = &other;
    rejected(&context, &desc, NULL, DOE_VK_INVALID_ARGUMENT);
    resolve.context = &context;
    expected = desc;
    commands = recording(&context);
    assert(doe_vk_commands_render_pass_full(commands, &desc, NULL, NULL, 0) == DOE_VK_OK);
    assert(color_state == 1 && depth_state == 6 && resolve_state == 0);
    assert(color_view.references == 2 && depth_view.references == 2 && resolve_view.references == 2);
    commands->state = DOE_VK_UNCERTAIN;
    assert(doe_vk_commands_release(commands) == DOE_VK_BUSY);
    assert(resolve.references == 2 && resolve.recording_owner == commands);
    doe_vk_textures_complete(commands, DOE_VK_OK);
    commands->state = DOE_VK_COMPLETE;
    assert(color_state == 0 && depth_state == 6 && resolve_state == 1);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(color_view.references == 1 && depth_view.references == 1 && resolve_view.references == 1);
    assert(resolve.references == 1 && resolve.recording_owner == NULL);
    /* A recorded resolve is not published when the command is abandoned. */
    resolve_state = 0;
    commands = recording(&context);
    assert(doe_vk_commands_render_pass_full(commands, &desc, NULL, NULL, 0) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK && resolve_state == 0);
    assert(begins == 6 && ends == 6 && created == destroyed && context.references == 1);
    /* Whole-image initializer for copies/bindings after a discard: clears only
     * the undefined depth aspect through a temporary single-subresource view,
     * then is a no-op; images without attachment usage are never touched. */
    context.vk.vkCreateImageView = create_view;
    context.vk.vkDestroyImageView = destroy_view;
    depth_state = VK_IMAGE_ASPECT_STENCIL_BIT;
    expected = (doe_vk_render_pass_desc){.depth_stencil = &depth_view, .clear_depth = true};
    commands = recording(&context);
    assert(doe_vk_commands_initialize_texture(commands, &depth) == DOE_VK_OK);
    /* The recorded pass retains the temporary view until command release. */
    assert(begins == 7 && views_created == 1 && views_destroyed == 0);
    doe_vk_textures_complete(commands, DOE_VK_OK);
    assert(depth_state == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
    assert(doe_vk_commands_release(commands) == DOE_VK_OK && views_destroyed == 1);
    commands = recording(&context);
    assert(doe_vk_commands_initialize_texture(commands, &depth) == DOE_VK_OK);
    assert(begins == 7 && commands->textures != NULL && commands->renders == NULL);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    doe_vk_texture copy_only = color;
    copy_only.desc.usage = DOE_VK_TEXTURE_COPY_SRC;
    copy_only.initialized = &resolve_state;
    resolve_state = 0;
    commands = recording(&context);
    assert(doe_vk_commands_initialize_texture(commands, &copy_only) == DOE_VK_OK);
    assert(commands->textures == NULL && begins == 7);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK && context.references == 1);
    puts("PASS whole-image initializer after discard: only undefined aspects cleared once through a temporary single-subresource view, then no-op; non-attachment images untouched");
    puts("PASS per-aspect native load/clear/store/discard, full depth/stencil transitions, read-only write/clear rejection, extent/NaN/absent-aspect preflight, speculative initialization, canonical depth-only zero with stencil preservation/no-op, culled-face stencil write summary and exact attachment leases; real MSAA/resolve native records, mismatch rejection, discard-independent resolve availability and uncertain/abandoned ownership");
    return 0;
}
