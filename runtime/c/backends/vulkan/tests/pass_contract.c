#include "../src/render_internal.h"
#include "../src/query_internal.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static bool inside, empty_viewport;
static unsigned begins, ends, draws, barriers, destroyed;
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination,
    VkFlags flags, uint32_t memory_count, const void *memory, uint32_t buffer_count,
    const void *buffers, uint32_t image_count, const void *images) {
    (void)command;
    (void)source;
    (void)destination;
    (void)flags;
    (void)memory;
    (void)buffers;
    (void)images;
    assert(!inside && buffer_count == 0 && memory_count + image_count == 1);
    ++barriers;
}
static VkResult create_pass(VkDevice device, const VkRenderPassCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkRenderPass *out) {
    (void)device;
    (void)allocator;
    assert(!inside && info->attachmentCount == 1 && info->pAttachments[0].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
    *out = 17;
    return 0;
}
static VkResult create_framebuffer(VkDevice device, const VkFramebufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkFramebuffer *out) {
    (void)device;
    (void)allocator;
    assert(!inside && info->renderPass == 17 && info->width == 8 && info->height == 8);
    *out = 18;
    return 0;
}
static void destroy_pass(VkDevice device, VkRenderPass pass, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(!inside && pass == 17);
    ++destroyed;
}
static void destroy_framebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *allocator) {
    (void)device;
    (void)allocator;
    assert(!inside && framebuffer == 18);
}
static void begin(VkCommandBuffer command, const VkRenderPassBeginInfo *info, uint32_t contents) {
    (void)command;
    assert(!inside && contents == VK_SUBPASS_CONTENTS_INLINE && info->renderPass == 17 && info->framebuffer == 18);
    assert(info->clearValueCount == 1 && info->pClearValues->color.float32[2] == 1);
    inside = true;
    ++begins;
}
static void end(VkCommandBuffer command) {
    (void)command;
    assert(inside);
    inside = false;
    ++ends;
}
static void bind(VkCommandBuffer command, int32_t point, VkPipeline pipeline) {
    (void)command;
    assert(inside && point == VK_PIPELINE_BIND_POINT_GRAPHICS && pipeline == 9);
}
static void blend_constants(VkCommandBuffer command, const float values[4]) {
    (void)command;
    assert(inside);
    const float expected[4] = {0.25f, 0.5f, 0.75f, 1};
    for (unsigned i = 0; i < 4; ++i) assert(values[i] == (draws == 1 ? expected[i] : 0));
}
static void viewport(VkCommandBuffer command, uint32_t first, uint32_t count, const VkViewport *view) {
    (void)command;
    assert(inside && first == 0 && count == 1 && view->height == (empty_viewport ? 1 : 8));
}
static void scissor(VkCommandBuffer command, uint32_t first, uint32_t count, const VkRect2D *rect) {
    (void)command;
    assert(inside && first == 0 && count == 1 && rect->extent.width == (empty_viewport ? 0 : 4));
    assert(rect->offset.x == (draws == 0 ? 0 : 4));
}
static void draw(VkCommandBuffer command, uint32_t count, uint32_t instances, uint32_t first, uint32_t first_instance) {
    (void)command;
    assert(inside && count == 3 && instances == 1 && first == 0 && first_instance == 0);
    ++draws;
}
static bool query_active;
static uint32_t query_index;
static unsigned resets, query_begins, query_ends;
static void reset_query(VkCommandBuffer command, VkQueryPool pool, uint32_t first, uint32_t count) {
    (void)command;
    assert(!inside && pool == 91 && first < 3 && count == 1);
    ++resets;
}
static void begin_query(VkCommandBuffer command, VkQueryPool pool, uint32_t index, VkFlags flags) {
    (void)command;
    assert(inside && !query_active && pool == 91 && flags == 0 && index < 3);
    query_active = true;
    query_index = index;
    ++query_begins;
}
static void end_query(VkCommandBuffer command, VkQueryPool pool, uint32_t index) {
    (void)command;
    assert(inside && query_active && pool == 91 && index == query_index);
    query_active = false;
    ++query_ends;
}
static doe_vk_commands *recording(doe_vk_context *context) {
    doe_vk_commands *commands = calloc(1, sizeof(*commands));
    assert(commands != NULL && doe_vk_context_retain(context) == DOE_VK_OK);
    *commands = (doe_vk_commands){.context = context, .state = DOE_VK_RECORDING, .uses_active = true};
    return commands;
}

int main(void) {
    doe_vk_context context = {.references = 1};
    context.caps.queue_flags = VK_QUEUE_GRAPHICS_BIT;
    context.properties.limits.maxFramebufferWidth = 64;
    context.properties.limits.maxFramebufferHeight = 64;
    context.properties.limits.maxViewportDimensions[0] = 64;
    context.properties.limits.maxViewportDimensions[1] = 64;
    context.properties.limits.viewportBoundsRange[1] = 64;
    context.vk = (doe_vk_functions){.vkCmdPipelineBarrier = barrier, .vkCreateRenderPass = create_pass,
        .vkCreateFramebuffer = create_framebuffer, .vkDestroyRenderPass = destroy_pass,
        .vkDestroyFramebuffer = destroy_framebuffer, .vkCmdBeginRenderPass = begin, .vkCmdEndRenderPass = end,
        .vkCmdResetQueryPool = reset_query, .vkCmdBeginQuery = begin_query, .vkCmdEndQuery = end_query,
        .vkCmdSetBlendConstants = blend_constants, .vkCmdBindPipeline = bind, .vkCmdSetViewport = viewport, .vkCmdSetScissor = scissor, .vkCmdDraw = draw};
    uint8_t initialized = 0;
    doe_vk_texture texture = {.context = &context, .references = 1, .subresource_count = 1,
        .initialized = &initialized, .desc = {.dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_RGBA8_UNORM,
            .width = 8, .height = 8, .depth_or_layers = 1, .mip_levels = 1, .usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT}};
    doe_vk_texture_view view = {.texture = &texture, .references = 1, .desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1}};
    doe_vk_pipeline pipeline = {.context = &context, .references = 1, .graphics = true,
        .color_format = DOE_VK_FORMAT_RGBA8_UNORM, .pipeline = 9};
    doe_vk_render_pass_draw items[2] = {{.pipeline = &pipeline, .draw = {.vertex_count = 3,
        .instance_count = 1, .viewport = {0, 0, 8, 8, 0, 1}, .scissor = {0, 0, 4, 8}}}};
    items[1] = items[0];
    items[1].draw.scissor[0] = 9;
    doe_vk_commands *commands = recording(&context);
    const doe_vk_clear_color blue = {.float32 = {0, 0, 1, 1}};
    assert(doe_vk_commands_render_pass(commands, &view, &blue, items, 2) == DOE_VK_INVALID_ARGUMENT);
    assert(commands->dispatches == NULL && commands->textures == NULL && commands->renders == NULL);
    assert(pipeline.references == 1 && view.references == 1 && texture.references == 1);
    assert(begins == 0 && barriers == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    items[1].draw.scissor[0] = 4;
    doe_vk_pipeline bad_pipeline = pipeline;
    bad_pipeline.binding_count = 1;
    bad_pipeline.bindings[0] = (doe_vk_binding_layout){0, 0, DOE_VK_BINDING_UNIFORM};
    const doe_vk_binding_resource bad_binding = {.kind = DOE_VK_BINDING_UNIFORM};
    items[1].pipeline = &bad_pipeline;
    items[1].bindings = &bad_binding;
    items[1].binding_count = 1;
    commands = recording(&context);
    assert(doe_vk_commands_render_pass(commands, &view, &blue, items, 2) == DOE_VK_INVALID_ARGUMENT);
    assert(begins == 0 && ends == 0 && barriers == 1 && commands->renders == NULL);
    assert(pipeline.references == 2 && bad_pipeline.references == 1);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK && pipeline.references == 1);
    items[1].pipeline = &pipeline;
    items[1].bindings = NULL;
    items[1].binding_count = 0;
    items[1].draw.override_blend_constant = true;
    items[1].draw.blend_constant[0] = NAN;
    commands = recording(&context);
    assert(doe_vk_commands_render_pass(commands, &view, &blue, items, 2) == DOE_VK_INVALID_ARGUMENT);
    assert(commands->dispatches == NULL && commands->renders == NULL && begins == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    const float dynamic_blend[4] = {0.25f, 0.5f, 0.75f, 1};
    memcpy(items[1].draw.blend_constant, dynamic_blend, sizeof(dynamic_blend));
    barriers = 0;
    commands = recording(&context);
    assert(doe_vk_commands_render_pass(commands, &view, &blue, items, 2) == DOE_VK_OK);
    assert(begins == 1 && ends == 1 && draws == 2 && !inside && barriers == 4);
    assert(commands->renders->next == NULL && pipeline.references == 3 && view.references == 2 && texture.references == 2);
    assert(initialized == 0 && commands->textures->initialized[0] == 1);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(destroyed == 1 && context.references == 1 && pipeline.references == 1 && texture.references == 1 && initialized == 0);
    commands = recording(&context);
    assert(doe_vk_commands_render_pass(commands, &view, &blue, NULL, 0) == DOE_VK_OK);
    assert(begins == 2 && ends == 2 && draws == 2);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(destroyed == 2 && context.references == 1);
    const doe_vk_buffer_binding unused_vertex = {NULL, 0, 0};
    pipeline.vertex_count = 1;
    items[0].vertices = &unused_vertex;
    items[0].vertex_buffer_count = 1;
    commands = recording(&context);
    draws = 0;
    assert(doe_vk_commands_render_pass(commands, &view, &blue, items, 1) == DOE_VK_OK);
    assert(commands->buffers == NULL && draws == 1);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    pipeline.vertex_attribute_end[0] = 4;
    commands = recording(&context);
    assert(doe_vk_commands_render_pass(commands, &view, &blue, items, 1) == DOE_VK_INVALID_ARGUMENT);
    assert(commands->dispatches == NULL && commands->renders == NULL);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    pipeline.vertex_attribute_end[0] = 0;
    pipeline.vertex_count = 0;
    items[0].vertices = NULL;
    items[0].vertex_buffer_count = 0;
    begins = ends = 2; /* The independent query controls below start here. */
    uint8_t query_states[3] = {0};
    doe_vk_query_pool pool = {.context = &context, .references = 1, .count = 3,
        .kind = DOE_VK_QUERY_OCCLUSION, .states = query_states, .pool = 91};
    doe_vk_render_pass_op ops[5] = {
        {.kind = DOE_VK_RENDER_BEGIN_OCCLUSION, .value.query_index = 0},
        {.kind = DOE_VK_RENDER_DRAW, .value.draw = items[0]},
        {.kind = DOE_VK_RENDER_END_OCCLUSION},
        {.kind = DOE_VK_RENDER_BEGIN_OCCLUSION, .value.query_index = 2},
        {.kind = DOE_VK_RENDER_END_OCCLUSION}
    };
    commands = recording(&context);
    pool.kind = DOE_VK_QUERY_TIMESTAMP;
    assert(doe_vk_commands_render_pass_ops(commands, &view, &blue, &pool, ops, 5) == DOE_VK_INVALID_ARGUMENT);
    assert(commands->queries == NULL && begins == 2 && resets == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    pool.kind = DOE_VK_QUERY_OCCLUSION;
    commands = recording(&context);
    ops[1].kind = DOE_VK_RENDER_BEGIN_OCCLUSION;
    ops[1].value.query_index = 1;
    assert(doe_vk_commands_render_pass_ops(commands, &view, &blue, &pool, ops, 5) == DOE_VK_INVALID_ARGUMENT);
    assert(commands->queries == NULL && begins == 2 && resets == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    commands = recording(&context);
    ops[1].kind = (doe_vk_render_operation)99;
    assert(doe_vk_commands_render_pass_ops(commands, &view, &blue, &pool, ops, 5) == DOE_VK_INVALID_ARGUMENT);
    assert(commands->queries == NULL && begins == 2 && resets == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    ops[1] = (doe_vk_render_pass_op){.kind = DOE_VK_RENDER_DRAW, .value.draw = items[0]};
    commands = recording(&context);
    ops[3].value.query_index = 0;
    assert(doe_vk_commands_render_pass_ops(commands, &view, &blue, &pool, ops, 5) == DOE_VK_INVALID_ARGUMENT);
    assert(commands->queries == NULL && begins == 2 && resets == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    ops[3].value.query_index = 2;
    commands = recording(&context);
    assert(doe_vk_commands_render_pass_ops(commands, &view, &blue, &pool, ops, 4) == DOE_VK_INVALID_ARGUMENT);
    assert(commands->queries == NULL && begins == 2 && resets == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    commands = recording(&context);
    assert(doe_vk_commands_render_pass_ops(commands, &view, &blue, &pool, ops + 2, 3) == DOE_VK_INVALID_ARGUMENT);
    assert(commands->queries == NULL && begins == 2 && resets == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    commands = recording(&context);
    ops[1].value.draw.draw.scissor[0] = 9;
    assert(doe_vk_commands_render_pass_ops(commands, &view, &blue, &pool, ops, 5) == DOE_VK_INVALID_ARGUMENT);
    assert(commands->queries == NULL && begins == 2 && resets == 0);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    ops[1].value.draw = items[0];
    commands = recording(&context);
    draws = 0;
    assert(doe_vk_commands_render_pass_ops(commands, &view, &blue, &pool, ops, 5) == DOE_VK_OK);
    assert(begins == 3 && ends == 3 && draws == 1 && resets == 2 && query_begins == 2 && query_ends == 2);
    assert(!query_active && pool.references == 2 && pool.recording_owner == commands);
    assert(query_states[0] == DOE_VK_QUERY_UNRESET && query_states[2] == DOE_VK_QUERY_UNRESET);
    assert(commands->queries->states[0] == DOE_VK_QUERY_WRITTEN && commands->queries->states[2] == DOE_VK_QUERY_WRITTEN);
    assert(doe_vk_commands_write_timestamp(commands, &pool, 1, DOE_VK_TIMESTAMP_BEGIN) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    assert(pool.references == 1 && pool.recording_owner == NULL && query_states[0] == DOE_VK_QUERY_UNRESET);
    empty_viewport = true;
    for (unsigned axis = 0; axis < 2; ++axis) {
        draws = 0;
        ops[1].value.draw = items[0];
        ops[1].value.draw.draw.viewport[2 + axis] = 0;
        commands = recording(&context);
        assert(doe_vk_commands_render_pass_ops(commands, &view, &blue, &pool, ops, 3) == DOE_VK_OK);
        assert(draws == 1 && !inside && !query_active);
        assert(commands->queries->states[0] == DOE_VK_QUERY_WRITTEN);
        assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    }
    assert(query_begins == 4 && query_ends == 4);
    puts("PASS whole-pass preflight, two draws in one native begin/end, no in-pass barriers, pipeline/default and per-draw blend constants, NaN preflight, empty clear, occlusion type/nesting/duplicate/end rejection, native reset-before-pass and paired query events, zero-extent viewport retains real draw/query calls with empty scissor, exact leases and rollback");
    return 0;
}
