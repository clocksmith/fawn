/* SPDX-License-Identifier: MIT */
#include "../src/draw_indirect_internal.h"
#include <assert.h>
#include <stdio.h>

static unsigned allocations, barriers;
static VkResult fail_buffer(VkDevice device, const VkBufferCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkBuffer *out) {
    (void)device; (void)allocator; (void)out;
    assert(info->size == 32 && info->usage ==
        (DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_COPY_DST | DOE_VK_BUFFER_INDIRECT));
    ++allocations;
    return -2;
}
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags destination,
    VkFlags flags, uint32_t memory_count, const void *memory, uint32_t buffer_count,
    const void *buffers, uint32_t image_count, const void *images) {
    (void)command; (void)source; (void)destination; (void)flags;
    (void)memory; (void)buffers; (void)images;
    assert(memory_count == 1 && buffer_count == 0 && image_count == 0);
    ++barriers;
}
static doe_vk_commands *recording(doe_vk_context *context) {
    doe_vk_commands *commands = calloc(1, sizeof(*commands));
    assert(commands != NULL && doe_vk_context_retain(context) == DOE_VK_OK);
    *commands = (doe_vk_commands){.context = context, .state = DOE_VK_RECORDING, .uses_active = true};
    return commands;
}
static void vertex_limits(doe_vk_context *context) {
    doe_vk_commands commands = {.context = context};
    doe_vk_pipeline pipeline = {.context = context, .vertex_count = 1};
    doe_vk_buffer vertex = {.context = context, .size = UINT64_MAX, .usage = DOE_VK_BUFFER_VERTEX};
    doe_vk_buffer_binding binding = {&vertex, 0, 0};
    uint32_t limits[2];
    for (uint32_t stride = 0; stride < 8; ++stride) {
        pipeline.vertices[0].stride = stride;
        for (uint32_t end = 1; end < 8; ++end) {
            pipeline.vertex_attribute_end[0] = end;
            for (uint32_t size = 0; size < 64; ++size) {
                binding.size = size;
                assert(doe_vk_render_vertex_limits(&commands, &pipeline, &binding, 1, limits) == DOE_VK_OK);
                uint32_t expected = 0;
                if (stride == 0) expected = end <= size ? UINT32_MAX : 0;
                else while ((uint64_t)expected * stride + end <= size) ++expected;
                assert(limits[0] == expected && limits[1] == UINT32_MAX);
            }
        }
    }
    pipeline.vertices[0] = (doe_vk_vertex_layout){4, true};
    pipeline.vertex_attribute_end[0] = 4;
    binding.size = UINT64_MAX;
    assert(doe_vk_render_vertex_limits(&commands, &pipeline, &binding, 1, limits) == DOE_VK_OK);
    assert(limits[0] == UINT32_MAX && limits[1] == UINT32_MAX);
    binding.size = 11;
    assert(doe_vk_render_vertex_limits(&commands, &pipeline, &binding, 1, limits) == DOE_VK_OK);
    assert(limits[0] == UINT32_MAX && limits[1] == 2);
    binding.offset = 1;
    assert(doe_vk_render_vertex_limits(&commands, &pipeline, &binding, 1, limits) == DOE_VK_INVALID_ARGUMENT);
    assert(limits[1] == 2);
    binding = (doe_vk_buffer_binding){NULL, 0, 0};
    assert(doe_vk_render_vertex_limits(&commands, &pipeline, &binding, 1, limits) == DOE_VK_INVALID_ARGUMENT);
    pipeline.vertex_attribute_end[0] = 0;
    assert(doe_vk_render_vertex_limits(&commands, &pipeline, &binding, 1, limits) == DOE_VK_OK);
    assert(limits[0] == UINT32_MAX && limits[1] == UINT32_MAX);
}
int main(int argc, char **argv) {
    char source[DOE_VK_DRAW_INDIRECT_SOURCE_CAPACITY] = {0};
    size_t length = SIZE_MAX;
    assert(doe_vk_draw_validator_source(source, sizeof(source), &length) == DOE_VK_OK);
    assert(length == strlen(source) && strstr(source, "first_instance != 0u") != NULL);
    assert(strstr(source, "vertex_count > vertex_limit - first_vertex") != NULL);
    assert(strstr(source, "record[3] = 0u;") != NULL);
    if (argc == 2) {
        FILE *file = fopen(argv[1], "wb");
        assert(file != NULL && fwrite(source, 1, length, file) == length && fclose(file) == 0);
    } else assert(argc == 1);
    char before[sizeof(source)];
    memcpy(before, source, sizeof(source));
    assert(doe_vk_draw_validator_source(source, length, &length) == DOE_VK_INVALID_ARGUMENT);
    assert(memcmp(before, source, sizeof(source)) == 0 && length == strlen(before));
    assert(doe_vk_draw_validator_source(NULL, sizeof(source), &length) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_draw_validator_source(source, sizeof(source), NULL) == DOE_VK_INVALID_ARGUMENT);
    doe_vk_caps caps = {.max_workgroup_count = {1, 1, 1}, .max_workgroup_size = {1, 1, 1},
        .max_workgroup_invocations = 1, .max_storage_buffer_range = 32, .max_bind_groups = 1,
        .max_bindings_per_group = 1, .max_storage_buffers_per_stage = 1, .queue_flags = VK_QUEUE_GRAPHICS_BIT};
    assert(doe_vk_draw_validator_validate(&caps) == DOE_VK_OK);
    caps.max_storage_buffer_range = 31;
    assert(doe_vk_draw_validator_validate(&caps) == DOE_VK_UNSUPPORTED);
    caps.max_storage_buffer_range = 32;
    caps.queue_flags = 0;
    assert(doe_vk_draw_validator_validate(&caps) == DOE_VK_UNSUPPORTED);
    caps.queue_flags = VK_QUEUE_GRAPHICS_BIT;
    doe_vk_context context = {.references = 1, .caps = caps};
    vertex_limits(&context);
    doe_vk_draw_validator *out = (doe_vk_draw_validator *)&context;
    assert(doe_vk_draw_validator_create(&context, NULL, 0, &out, NULL) == DOE_VK_INVALID_ARGUMENT);
    assert(out == (doe_vk_draw_validator *)&context && context.references == 1);
    doe_vk_pipeline guard = {.context = &context, .references = 1};
    doe_vk_draw_validator validator = {UINT32_MAX, &guard};
    assert(doe_vk_draw_validator_retain(&validator) == DOE_VK_OUT_OF_MEMORY);
    doe_vk_buffer arguments = {.context = &context, .references = 1, .size = 20,
        .usage = DOE_VK_BUFFER_INDIRECT | DOE_VK_BUFFER_COPY_SRC};
    doe_vk_render_indirect indirect = {&validator, &arguments, 4};
    doe_vk_commands *commands = recording(&context);
    assert(doe_vk_draw_indirect_validate(commands, &indirect) == DOE_VK_OK);
    indirect.offset = 8;
    assert(doe_vk_draw_indirect_validate(commands, &indirect) == DOE_VK_INVALID_ARGUMENT);
    indirect.offset = 4;
    arguments.usage = DOE_VK_BUFFER_INDIRECT;
    assert(doe_vk_draw_indirect_validate(commands, &indirect) == DOE_VK_INVALID_ARGUMENT);
    arguments.usage |= DOE_VK_BUFFER_COPY_SRC;
    arguments.content_status = DOE_VK_NATIVE_FAILURE;
    assert(doe_vk_draw_indirect_validate(commands, &indirect) == DOE_VK_NATIVE_FAILURE);
    arguments.content_status = DOE_VK_OK;
    context.vk.vkCreateBuffer = fail_buffer;
    context.vk.vkCmdPipelineBarrier = barrier;
    context.properties.limits.maxFramebufferWidth = context.properties.limits.maxFramebufferHeight = 8;
    context.properties.limits.maxViewportDimensions[0] = context.properties.limits.maxViewportDimensions[1] = 8;
    context.properties.limits.viewportBoundsRange[1] = 8;
    uint8_t initialized = 0;
    doe_vk_texture texture = {.context = &context, .references = 1, .subresource_count = 1,
        .initialized = &initialized, .desc = {.dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_RGBA8_UNORM,
            .width = 8, .height = 8, .depth_or_layers = 1, .mip_levels = 1, .usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT}};
    doe_vk_texture_view view = {.texture = &texture, .references = 1, .desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1}};
    doe_vk_pipeline target = {.context = &context, .references = 1, .graphics = true, .color_format = DOE_VK_FORMAT_RGBA8_UNORM};
    doe_vk_render_pass_draw draw = {.pipeline = &target, .indirect = &indirect,
        .draw = {.viewport = {0, 0, 8, 8, 0, 1}, .scissor = {0, 0, 8, 8}}};
    const doe_vk_clear_color clear = {.float32 = {0, 0, 0, 1}};
    assert(doe_vk_commands_render_pass(commands, &view, &clear, &draw, 1) == DOE_VK_OUT_OF_MEMORY);
    assert(allocations == 1 && barriers == 1 && commands->renders == NULL && commands->buffers == NULL);
    assert(arguments.references == 1 && guard.references == 1 && target.references == 2);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK && context.references == 1 && target.references == 1);
    puts("PASS draw guard source/capacity/feature admission, exact vertex-instance last-byte/stride0/hole bounds, invalid argument preflight, output preservation and scratch OOM before native pass with exact leases");
    return 0;
}
