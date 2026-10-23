/* SPDX-License-Identifier: MIT
 * Host contract for indexed native draws: exact index range admission, native
 * bind/draw packets inside one pass, preflight rollback and lease ownership.
 * Mocked native calls are packet evidence only, not GPU execution. */
#include "../src/render_internal.h"
#include <assert.h>
#include <stdio.h>

static bool inside;
static unsigned begins, binds, indexed_draws, plain_draws, destroyed;
static VkBuffer expected_buffer;
static VkDeviceSize expected_offset;
static uint32_t expected_type, expected_count, expected_first, expected_instances;
static int32_t expected_base;

static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination,
    VkFlags flags, uint32_t memory_count, const void *memory, uint32_t buffer_count,
    const void *buffers, uint32_t image_count, const void *images) {
    (void)command; (void)source; (void)destination; (void)flags;
    (void)memory_count; (void)memory; (void)buffer_count; (void)buffers; (void)image_count; (void)images;
    assert(!inside);
}
static VkResult create_pass(VkDevice device, const VkRenderPassCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkRenderPass *out) {
    (void)device; (void)allocator; (void)info;
    assert(!inside);
    *out = 17;
    return 0;
}
static VkResult create_framebuffer(VkDevice device, const VkFramebufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFramebuffer *out) {
    (void)device; (void)allocator; (void)info;
    assert(!inside);
    *out = 18;
    return 0;
}
static void destroy_pass(VkDevice device, VkRenderPass pass, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(pass == 17);
    ++destroyed;
}
static void destroy_framebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert(framebuffer == 18);
}
static void begin(VkCommandBuffer command, const VkRenderPassBeginInfo *info, uint32_t contents) {
    (void)command; (void)info; (void)contents;
    assert(!inside);
    inside = true;
    ++begins;
}
static void end(VkCommandBuffer command) {
    (void)command;
    assert(inside);
    inside = false;
}
static void bind_pipeline(VkCommandBuffer command, int32_t point, VkPipeline pipeline) {
    (void)command; (void)point; (void)pipeline;
    assert(inside);
}
static void blend_constants(VkCommandBuffer command, const float values[4]) {
    (void)command; (void)values;
    assert(inside);
}
static void viewport(VkCommandBuffer command, uint32_t first, uint32_t count, const VkViewport *view) {
    (void)command; (void)first; (void)count; (void)view;
    assert(inside);
}
static void scissor(VkCommandBuffer command, uint32_t first, uint32_t count, const VkRect2D *rect) {
    (void)command; (void)first; (void)count; (void)rect;
    assert(inside);
}
static void bind_index(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset, uint32_t type) {
    (void)command;
    assert(inside && buffer == expected_buffer && offset == expected_offset && type == expected_type);
    ++binds;
}
static void draw_indexed(VkCommandBuffer command, uint32_t count, uint32_t instances, uint32_t first,
    int32_t base, uint32_t first_instance) {
    (void)command;
    /* The bind must precede its draw within the same open pass. */
    assert(inside && binds == indexed_draws + 1);
    assert(count == expected_count && instances == expected_instances && first == expected_first &&
        base == expected_base && first_instance == 0);
    ++indexed_draws;
}
static void draw(VkCommandBuffer command, uint32_t count, uint32_t instances, uint32_t first, uint32_t first_instance) {
    (void)command; (void)count; (void)instances; (void)first; (void)first_instance;
    ++plain_draws;
}
static doe_vk_commands *recording(doe_vk_context *context) {
    doe_vk_commands *commands = calloc(1, sizeof(*commands));
    assert(commands != NULL && doe_vk_context_retain(context) == DOE_VK_OK);
    *commands = (doe_vk_commands){.context = context, .state = DOE_VK_RECORDING, .uses_active = true};
    return commands;
}
static void limit(doe_vk_context *context, doe_vk_render_indices indices, doe_vk_status expected, uint32_t value) {
    doe_vk_commands commands = {.context = context};
    uint32_t out = 0xa5a5a5a5;
    assert(doe_vk_render_index_limit(&commands, &indices, &out) == expected);
    assert(out == (expected == DOE_VK_OK ? value : 0xa5a5a5a5));
}
static void rejected(doe_vk_context *context, doe_vk_texture_view *view, const doe_vk_render_pass_draw *item,
    doe_vk_status expected, const doe_vk_buffer *indices) {
    doe_vk_commands *commands = recording(context);
    const doe_vk_clear_color clear = {.float32 = {0, 0, 0, 1}};
    unsigned before = begins;
    assert(doe_vk_commands_render_pass(commands, view, &clear, item, 1) == expected);
    assert(begins == before && commands->renders == NULL && commands->buffers == NULL);
    assert(indices->references == 1 && indices->recorded_uses == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
}

int main(void) {
    doe_vk_context context = {.references = 1};
    context.caps.queue_flags = VK_QUEUE_GRAPHICS_BIT;
    context.caps.max_color_attachments = 8;
    context.properties.limits.maxFramebufferWidth = context.properties.limits.maxFramebufferHeight = 64;
    context.properties.limits.maxViewportDimensions[0] = context.properties.limits.maxViewportDimensions[1] = 64;
    context.properties.limits.viewportBoundsRange[1] = 64;
    context.vk = (doe_vk_functions){.vkCmdPipelineBarrier = barrier, .vkCreateRenderPass = create_pass,
        .vkCreateFramebuffer = create_framebuffer, .vkDestroyRenderPass = destroy_pass,
        .vkDestroyFramebuffer = destroy_framebuffer, .vkCmdBeginRenderPass = begin, .vkCmdEndRenderPass = end,
        .vkCmdSetBlendConstants = blend_constants, .vkCmdBindPipeline = bind_pipeline,
        .vkCmdSetViewport = viewport, .vkCmdSetScissor = scissor, .vkCmdDraw = draw,
        .vkCmdBindIndexBuffer = bind_index, .vkCmdDrawIndexed = draw_indexed};
    doe_vk_buffer index_buffer = {.context = &context, .references = 1, .usage = DOE_VK_BUFFER_INDEX,
        .size = 64, .buffer = 77};

    /* Exact element capacity from the bound range, never the whole buffer. */
    limit(&context, (doe_vk_render_indices){&index_buffer, 0, 7, DOE_VK_INDEX_UINT16, 0, 0, 0}, DOE_VK_OK, 3);
    limit(&context, (doe_vk_render_indices){&index_buffer, 6, 58, DOE_VK_INDEX_UINT16, 0, 0, 0}, DOE_VK_OK, 29);
    limit(&context, (doe_vk_render_indices){&index_buffer, 4, 12, DOE_VK_INDEX_UINT32, 0, 0, 0}, DOE_VK_UNSUPPORTED, 0);
    context.caps.full_draw_index_uint32 = true;
    limit(&context, (doe_vk_render_indices){&index_buffer, 4, 12, DOE_VK_INDEX_UINT32, 0, 0, 0}, DOE_VK_OK, 3);
    limit(&context, (doe_vk_render_indices){&index_buffer, 4, 15, DOE_VK_INDEX_UINT32, 0, 0, 0}, DOE_VK_OK, 3);
    /* Wrong-but-typed controls: format alignment, usage, range, enum, identity, poison. */
    limit(&context, (doe_vk_render_indices){&index_buffer, 2, 12, DOE_VK_INDEX_UINT32, 0, 0, 0}, DOE_VK_INVALID_ARGUMENT, 0);
    limit(&context, (doe_vk_render_indices){&index_buffer, 1, 12, DOE_VK_INDEX_UINT16, 0, 0, 0}, DOE_VK_INVALID_ARGUMENT, 0);
    limit(&context, (doe_vk_render_indices){&index_buffer, 0, 65, DOE_VK_INDEX_UINT16, 0, 0, 0}, DOE_VK_INVALID_ARGUMENT, 0);
    limit(&context, (doe_vk_render_indices){&index_buffer, 66, 0, DOE_VK_INDEX_UINT16, 0, 0, 0}, DOE_VK_INVALID_ARGUMENT, 0);
    limit(&context, (doe_vk_render_indices){&index_buffer, 0, 8, (doe_vk_index_format)2, 0, 0, 0}, DOE_VK_INVALID_ARGUMENT, 0);
    limit(&context, (doe_vk_render_indices){NULL, 0, 8, DOE_VK_INDEX_UINT16, 0, 0, 0}, DOE_VK_INVALID_ARGUMENT, 0);
    index_buffer.usage = DOE_VK_BUFFER_VERTEX;
    limit(&context, (doe_vk_render_indices){&index_buffer, 0, 8, DOE_VK_INDEX_UINT16, 0, 0, 0}, DOE_VK_INVALID_ARGUMENT, 0);
    index_buffer.usage = DOE_VK_BUFFER_INDEX;
    doe_vk_context other = {.references = 1};
    index_buffer.context = &other;
    limit(&context, (doe_vk_render_indices){&index_buffer, 0, 8, DOE_VK_INDEX_UINT16, 0, 0, 0}, DOE_VK_INVALID_ARGUMENT, 0);
    index_buffer.context = &context;
    index_buffer.content_status = DOE_VK_NATIVE_FAILURE;
    limit(&context, (doe_vk_render_indices){&index_buffer, 0, 8, DOE_VK_INDEX_UINT16, 0, 0, 0}, DOE_VK_NATIVE_FAILURE, 0);
    index_buffer.content_status = DOE_VK_OK;
    doe_vk_buffer huge = {.context = &context, .references = 1, .usage = DOE_VK_BUFFER_INDEX, .size = UINT64_MAX};
    limit(&context, (doe_vk_render_indices){&huge, 0, UINT64_MAX, DOE_VK_INDEX_UINT16, 0, 0, 0}, DOE_VK_OK, UINT32_MAX);

    uint8_t initialized = 1;
    doe_vk_texture texture = {.context = &context, .references = 1, .subresource_count = 1,
        .initialized = &initialized, .desc = {.dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_RGBA8_UNORM,
            .width = 8, .height = 8, .depth_or_layers = 1, .mip_levels = 1, .usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT}};
    doe_vk_texture_view view = {.texture = &texture, .references = 1, .aspects = 1, .desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1, DOE_VK_ASPECT_ALL}};
    doe_vk_pipeline pipeline = {.context = &context, .references = 1, .graphics = true,
        .color_count = 1, .color_formats = {DOE_VK_FORMAT_RGBA8_UNORM}, .pipeline = 9};
    doe_vk_render_indices indices = {&index_buffer, 6, 12, DOE_VK_INDEX_UINT16, 3, 3, -2};
    doe_vk_render_pass_draw item = {.pipeline = &pipeline, .indices = &indices,
        .draw = {.instance_count = 2, .viewport = {0, 0, 8, 8, 0, 1}, .scissor = {0, 0, 8, 8}}};
    /* firstIndex+indexCount = 6 of a 6-element range is admitted; one more is not. */
    indices.index_count = 4;
    rejected(&context, &view, &item, DOE_VK_INVALID_ARGUMENT, &index_buffer);
    indices.index_count = 3;
    indices.first_index = UINT32_MAX;
    rejected(&context, &view, &item, DOE_VK_INVALID_ARGUMENT, &index_buffer);
    indices.first_index = 3;
    item.draw.vertex_count = 3; /* Indexed counts live only in the index record. */
    rejected(&context, &view, &item, DOE_VK_INVALID_ARGUMENT, &index_buffer);
    item.draw.vertex_count = 0;
    item.draw.first_vertex = 1;
    rejected(&context, &view, &item, DOE_VK_INVALID_ARGUMENT, &index_buffer);
    item.draw.first_vertex = 0;
    item.draw.first_instance = UINT32_MAX;
    rejected(&context, &view, &item, DOE_VK_INVALID_ARGUMENT, &index_buffer);
    item.draw.first_instance = 0;
    indices.offset = 5;
    rejected(&context, &view, &item, DOE_VK_INVALID_ARGUMENT, &index_buffer);
    indices.offset = 6;

    /* Admitted draw: exact native bind then signed-base draw inside one pass. */
    expected_buffer = 77;
    expected_offset = 6;
    expected_type = VK_INDEX_TYPE_UINT16;
    expected_count = 3;
    expected_first = 3;
    expected_instances = 2;
    expected_base = -2;
    doe_vk_commands *commands = recording(&context);
    const doe_vk_clear_color clear = {.float32 = {0, 0, 0, 1}};
    assert(doe_vk_commands_render_pass(commands, &view, &clear, &item, 1) == DOE_VK_OK);
    assert(begins == 1 && !inside && binds == 1 && indexed_draws == 1 && plain_draws == 0);
    /* The command owner retains the index buffer until retirement. */
    assert(index_buffer.references == 2 && index_buffer.recorded_uses == 1);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(index_buffer.references == 1 && destroyed == 1);

    /* Zero-count indexed draws still record their native no-op draw. */
    indices = (doe_vk_render_indices){&index_buffer, 0, 64, DOE_VK_INDEX_UINT32, 0, 16, 7};
    expected_offset = 0;
    expected_type = VK_INDEX_TYPE_UINT32;
    expected_count = 0;
    expected_first = 16;
    expected_base = 7;
    commands = recording(&context);
    assert(doe_vk_commands_render_pass(commands, &view, &clear, &item, 1) == DOE_VK_OK);
    assert(begins == 2 && binds == 2 && indexed_draws == 2 && plain_draws == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK && index_buffer.references == 1);
    indices.first_index = 17;
    rejected(&context, &view, &item, DOE_VK_INVALID_ARGUMENT, &index_buffer);
    context.caps.full_draw_index_uint32 = false;
    indices.first_index = 0;
    rejected(&context, &view, &item, DOE_VK_UNSUPPORTED, &index_buffer);
    assert(context.references == 1 && pipeline.references == 1 && view.references == 1);
    puts("PASS indexed native admission: exact bound-range element capacity, uint32 full-index feature gate, alignment/usage/range/enum/identity/poison controls, first+count overflow and boundary, zero vertex fields, ordered bind-then-drawIndexed with signed base vertex in one pass, zero-count draw, preflight rollback and command-owned index lease");
    return 0;
}
