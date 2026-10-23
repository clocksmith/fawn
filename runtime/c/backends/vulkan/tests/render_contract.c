#include "../src/render_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static unsigned destroyed_passes;
static VkResult create_pass(VkDevice device, const VkRenderPassCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkRenderPass *out) {
    (void)device;
    (void)allocator;
    assert(info->attachmentCount == 1 && info->pAttachments[0].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
    *out = 17;
    return 0;
}
static void destroy_pass(VkDevice device, VkRenderPass pass, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(pass == 17);
    ++destroyed_passes;
}
static VkResult fail_framebuffer(VkDevice device, const VkFramebufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFramebuffer *out) {
    (void)device;
    (void)allocator;
    (void)out;
    assert(info->renderPass == 17 && info->width == 8 && info->height == 8);
    return -1;
}
static doe_vk_commands recording(doe_vk_context *context) {
    return (doe_vk_commands){ .context = context, .state = DOE_VK_RECORDING, .uses_active = true };
}

int main(void) {
    doe_vk_context context = { .references = 1 };
    context.caps.queue_flags = VK_QUEUE_GRAPHICS_BIT;
    context.properties.limits.maxFramebufferWidth = 64;
    context.properties.limits.maxFramebufferHeight = 64;
    context.properties.limits.maxViewportDimensions[0] = 64;
    context.properties.limits.maxViewportDimensions[1] = 64;
    context.properties.limits.viewportBoundsRange[1] = 64;
    context.vk.vkCreateRenderPass = create_pass;
    context.vk.vkDestroyRenderPass = destroy_pass;
    context.vk.vkCreateFramebuffer = fail_framebuffer;
    uint8_t initialized = 1;
    doe_vk_texture texture = { .context = &context, .references = 1,
        .subresource_count = 1, .initialized = &initialized,
        .desc = {.dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_RGBA8_UNORM,
            .width = 8, .height = 8, .depth_or_layers = 1, .mip_levels = 1, .usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT} };
    doe_vk_texture_view view = { .texture = &texture, .references = 1, .desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1} };
    doe_vk_pipeline pipeline = {.context = &context, .references = 1, .graphics = true,
        .color_format = DOE_VK_FORMAT_RGBA8_UNORM, .vertex_count = 1};
    pipeline.vertices[0] = (doe_vk_vertex_layout){16, false};
    pipeline.vertex_attribute_end[0] = 16;
    doe_vk_buffer buffer = {.context = &context, .references = 1, .usage = DOE_VK_BUFFER_VERTEX, .size = 48};
    doe_vk_buffer_binding vertex = {&buffer, 0, 47};
    doe_vk_render_draw draw = {.vertex_count = 3, .instance_count = 1,
        .viewport = {0, 0, 8, 8, 0, 1}, .scissor = {0, 0, 8, 8}};
    doe_vk_commands commands = recording(&context);
    assert(doe_vk_commands_draw(&commands, &pipeline, &view, NULL, 0, &vertex, 1, &draw) == DOE_VK_INVALID_ARGUMENT);
    assert(commands.dispatches == NULL && commands.renders == NULL && commands.textures == NULL);
    vertex.size = 48;
    draw.viewport[2] = NAN;
    commands = recording(&context);
    assert(doe_vk_commands_draw(&commands, &pipeline, &view, NULL, 0, &vertex, 1, &draw) == DOE_VK_INVALID_ARGUMENT);
    draw.viewport[2] = 8;
    draw.scissor[0] = UINT32_MAX;
    commands = recording(&context);
    assert(doe_vk_commands_draw(&commands, &pipeline, &view, NULL, 0, &vertex, 1, &draw) == DOE_VK_INVALID_ARGUMENT);
    draw.scissor[0] = 0;
    draw.first_instance = UINT32_MAX;
    commands = recording(&context);
    assert(doe_vk_commands_draw(&commands, &pipeline, &view, NULL, 0, &vertex, 1, &draw) == DOE_VK_INVALID_ARGUMENT);
    commands = recording(&context);
    assert(doe_vk_commands_dispatch_resources(&commands, &pipeline, NULL, 0, 1, 1, 1) == DOE_VK_INVALID_ARGUMENT);
    pipeline.graphics = false;
    commands = recording(&context);
    assert(doe_vk_commands_draw(&commands, &pipeline, &view, NULL, 0, &vertex, 1, &draw) == DOE_VK_INVALID_ARGUMENT);
    commands = recording(&context);
    initialized = 0;
    const doe_vk_clear_color clear = {.float32 = {0, 0, 0, 1}};
    assert(doe_vk_commands_clear_color(&commands, &view, clear) == DOE_VK_OUT_OF_MEMORY);
    assert(commands.renders != NULL && commands.renders->pass == 17 && commands.renders->framebuffer == 0);
    assert(view.references == 2 && texture.references == 2 && texture.recording_owner == &commands);
    assert(initialized == 0 && commands.textures->initialized[0] == 0);
    doe_vk_render_release(&commands);
    doe_vk_textures_release(&commands);
    assert(destroyed_passes == 1 && view.references == 1 && texture.references == 1 && texture.recording_owner == NULL);
    context.caps.queue_flags = VK_QUEUE_COMPUTE_BIT;
    commands = recording(&context);
    assert(doe_vk_commands_clear_color(&commands, &view, clear) == DOE_VK_UNSUPPORTED);
    assert(commands.renders == NULL && commands.textures == NULL);
    puts("PASS render vertex last-byte, NaN viewport, scissor/count overflow, compute/graphics kind mismatch, graphics-family admission and framebuffer rollback without initialized-output publication");
    return 0;
}
