/* SPDX-License-Identifier: MIT
 * Real GPU-produced indirect draws through resource snapshots and one pass.
 * Compute producer/guard use C ABI9; graphics artifacts are retained Tint. */
#include "vulkan_compute_fixture.h"
#include "doe_vk_render.h"
#include "doe_vk_indirect.h"

enum { DRAW_COUNT = 8, ARGUMENT_WORDS = 30 };

static doe_vk_status lower_pass(doe_vk_commands *commands, doe_vk_draw_validator *validator,
    const doe_resource_command *records, size_t count, size_t *consumed)
{
    if (!count || records[0].kind != DOE_COMMAND_RENDER) return DOE_VK_UNSUPPORTED;
    const doe_resource_render_packet *begin = doe_resource_render_record_borrow(records[0].value.render);
    if (!begin->boundary_only || !begin->first_in_pass || begin->last_in_pass || !begin->targets.color ||
        begin->targets.depth_stencil || begin->targets.resolve_target ||
        begin->targets.color_load != WGPULoadOp_Clear || begin->targets.color_store != WGPUStoreOp_Store)
        return DOE_VK_UNSUPPORTED;
    doe_resource_query_set *queries = begin->targets.occlusion_queries;
    if (!queries || doe_resource_query_set_type(queries) != WGPUQueryType_Occlusion) return DOE_VK_UNSUPPORTED;
    doe_vk_render_pass_op ops[DRAW_COUNT * 3];
    doe_vk_binding_resource bindings[DRAW_COUNT];
    doe_vk_buffer_binding vertices[DRAW_COUNT][3];
    doe_vk_render_indirect indirect[DRAW_COUNT];
    size_t draw_count = 0, op_count = 0;
    for (size_t i = 1; i < count; i++) {
        const doe_resource_command *record = &records[i];
        if (record->kind == DOE_COMMAND_BEGIN_OCCLUSION || record->kind == DOE_COMMAND_END_OCCLUSION) {
            if (record->value.timestamp.set != queries || op_count == DRAW_COUNT * 3) return DOE_VK_UNSUPPORTED;
            ops[op_count++] = (doe_vk_render_pass_op){.kind = record->kind == DOE_COMMAND_BEGIN_OCCLUSION ?
                DOE_VK_RENDER_BEGIN_OCCLUSION : DOE_VK_RENDER_END_OCCLUSION,
                .value.query_index = record->value.timestamp.index};
            continue;
        }
        if (record->kind != DOE_COMMAND_RENDER) return DOE_VK_UNSUPPORTED;
        const doe_resource_render_packet *packet = doe_resource_render_record_borrow(record->value.render);
        if (packet->first_in_pass || packet->targets.color != begin->targets.color ||
            packet->targets.occlusion_queries != queries || packet->targets.depth_stencil || packet->targets.resolve_target)
            return DOE_VK_UNSUPPORTED;
        if (packet->boundary_only) {
            if (!packet->last_in_pass) return DOE_VK_UNSUPPORTED;
            doe_vk_clear_color clear;
            for (size_t component = 0; component < 4; component++)
                clear.float32[component] = (float)begin->targets.clear_color[component];
            doe_vk_render_pass_desc desc = {.color = doe_resource_view_native(begin->targets.color), .clear_color = &clear};
            doe_vk_status status = doe_vk_commands_render_pass_full(commands, &desc,
                doe_resource_query_set_native(queries), ops, op_count);
            if (status == DOE_VK_OK) *consumed = i + 1;
            return status;
        }
        if (packet->last_in_pass || packet->indexed || !packet->indirect ||
            draw_count == DRAW_COUNT || op_count == DRAW_COUNT * 3 || !packet->groups[0] ||
            packet->vertices[0].buffer || !packet->vertices[1].buffer || !packet->vertices[2].buffer)
            return DOE_VK_UNSUPPORTED;
        for (size_t slot = 1; slot < DOE_RESOURCE_MAX_GROUPS; slot++)
            if (packet->groups[slot]) return DOE_VK_UNSUPPORTED;
        for (size_t slot = 3; slot < DOE_RESOURCE_MAX_VERTEX_BUFFERS; slot++)
            if (packet->vertices[slot].buffer) return DOE_VK_UNSUPPORTED;
        for (size_t component = 0; component < 4; component++)
            if (packet->state.blend[component] != 0) return DOE_VK_UNSUPPORTED;
        doe_resource_binding resolved;
        if (doe_resource_bind_group_resolve(packet->groups[0], packet->offsets.values[0],
            packet->offsets.counts[0], &resolved, 1) != DOE_RESOURCE_OK ||
            resolved.kind != DOE_BIND_BUFFER || resolved.binding != 0) return DOE_VK_UNSUPPORTED;
        bindings[draw_count] = (doe_vk_binding_resource){.kind = DOE_VK_BINDING_UNIFORM, .value.buffer = {
            doe_resource_buffer_native(resolved.resource.buffer.buffer), resolved.resource.buffer.offset, resolved.resource.buffer.size}};
        vertices[draw_count][0] = (doe_vk_buffer_binding){0};
        for (size_t slot = 1; slot < 3; slot++) vertices[draw_count][slot] = (doe_vk_buffer_binding){
            doe_resource_buffer_native(packet->vertices[slot].buffer), packet->vertices[slot].offset, packet->vertices[slot].size};
        indirect[draw_count] = (doe_vk_render_indirect){validator,
            doe_resource_buffer_native(packet->indirect), packet->indirect_offset};
        doe_vk_render_pass_draw *draw = &ops[op_count].value.draw;
        *draw = (doe_vk_render_pass_draw){.pipeline = doe_resource_render_pipeline_native(packet->pipeline),
            .bindings = &bindings[draw_count], .binding_count = 1, .vertices = vertices[draw_count],
            .vertex_buffer_count = 3, .indirect = &indirect[draw_count],
            .draw = {.viewport_convention = DOE_VK_VIEWPORT_WEBGPU}};
        memcpy(draw->draw.viewport, packet->state.viewport, sizeof(draw->draw.viewport));
        memcpy(draw->draw.scissor, packet->state.scissor, sizeof(draw->draw.scissor));
        ops[op_count++].kind = DOE_VK_RENDER_DRAW;
        draw_count++;
    }
    return DOE_VK_UNSUPPORTED;
}

static void bind_draw(doe_resource_render_pass *pass, doe_resource_render_pipeline *pipeline,
    doe_resource_bind_group *group, doe_resource_buffer *vertices, doe_resource_buffer *instances, uint64_t vertex_size)
{
    doe_resource_dispatch_group bound = {group, NULL, 0};
    assert(doe_resource_render_pass_set_pipeline(pass, pipeline) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_set_group(pass, 0, &bound) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_set_vertex(pass, 1, vertices, 16, vertex_size) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_set_vertex(pass, 2, instances, 8, 8) == DOE_RESOURCE_OK);
}

static doe_resource_render_pipeline *graphics_pipeline(const doe_resource_config *config, lifecycle *life,
    doe_resource_pipeline_layout *layout, doe_vk_shader vertex, doe_vk_shader fragment, bool constant_instance)
{
    doe_vk_vertex_layout layouts[] = {{0, false}, {28, false}, {constant_instance ? 0 : 8, true}};
    doe_vk_vertex_attribute attributes[] = {{0, 1, 0, DOE_VK_VERTEX_FLOAT32X3},
        {1, 1, 12, DOE_VK_VERTEX_FLOAT32X4}, {2, 2, 0, DOE_VK_VERTEX_FLOAT32X2}};
    const doe_vk_binding_layout binding = {0, 0, DOE_VK_BINDING_UNIFORM};
    doe_vk_render_pipeline_desc desc = {.vertex = vertex, .fragment = fragment,
        .color_format = DOE_VK_FORMAT_RGBA8_UNORM, .topology = DOE_VK_TRIANGLES, .color_write_mask = 15,
        .vertex_buffer_count = 3, .vertex_buffers = layouts, .attribute_count = 3, .attributes = attributes,
        .binding_count = 1, .bindings = &binding};
    doe_vk_pipeline *native;
    require(doe_vk_render_pipeline_create(config->device.identity, &desc, &native, NULL), "graphics pipeline");
    doe_resource_render_pipeline_desc resource_desc = {.color_format = WGPUTextureFormat_RGBA8Unorm,
        .samples = 1, .vertex_count = 3, .vertices = {{0, 0, WGPUVertexStepMode_Undefined},
            {28, 28, WGPUVertexStepMode_Vertex}, {constant_instance ? 0 : 8, 8, WGPUVertexStepMode_Instance}}};
    doe_resource_render_pipeline *result;
    assert(doe_resource_render_pipeline_create(config, layout, &resource_desc,
        (doe_resource_native){native, life, release_pipeline}, &result) == DOE_RESOURCE_OK);
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 6 || !argv[4][0] || strcmp(argv[5], "--allow-software")) return 2;
    doe_vk_options options = {.allow_cpu_adapter = true, .require_graphics_queue = true};
    doe_vk_context *context;
    require(doe_vk_context_create(&options, &context, NULL), "software graphics context");
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    if (!caps.software_adapter) { doe_vk_context_release(context); return 77; }
    printf("Resource indirect draw consumer: %s; C ABI9 compute, retained Tint graphics; software only\n", caps.device_name);
    lifecycle life = {0};
    doe_resource_config config = {.allocator = {&life, NULL, allocate, deallocate}, .device = {context, alive, retain, release}};
    char guard_source[DOE_VK_DRAW_INDIRECT_SOURCE_CAPACITY];
    size_t guard_length;
    require(doe_vk_draw_validator_source(guard_source, sizeof(guard_source), &guard_length), "draw guard source");
    doe_shader *shader = compile(&life, guard_source, guard_length, argv[4]);
    doe_shader_view shader_view;
    assert(doe_shader_borrow(shader, &shader_view) == DOE_SHADER_OK);
    doe_vk_draw_validator *validator;
    require(doe_vk_draw_validator_create(context, shader_view.words, shader_view.counts.words, &validator, NULL), "draw guard");
    doe_shader_release(shader);
    doe_resource_buffer *arguments = buffer(&config, &life, ARGUMENT_WORDS * 4,
        WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect,
        DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_INDIRECT | DOE_VK_BUFFER_COPY_SRC);
    doe_resource_buffer *uniform = buffer(&config, &life, 96, WGPUBufferUsage_Uniform, DOE_VK_BUFFER_UNIFORM);
    doe_resource_buffer *vertices = buffer(&config, &life, 200, WGPUBufferUsage_Vertex, DOE_VK_BUFFER_VERTEX);
    doe_resource_buffer *instances = buffer(&config, &life, 32, WGPUBufferUsage_Vertex, DOE_VK_BUFFER_VERTEX);
    doe_resource_buffer *readback = buffer(&config, &life, 256, WGPUBufferUsage_CopyDst, DOE_VK_BUFFER_COPY_DST);
    doe_resource_buffer *query_result = buffer(&config, &life, 512,
        WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopyDst, DOE_VK_BUFFER_COPY_DST);
    doe_vk_buffer *native_arguments = doe_resource_buffer_native(arguments);
    doe_vk_buffer *native_readback = doe_resource_buffer_native(readback);
    doe_vk_buffer *native_query_result = doe_resource_buffer_native(query_result);
    float parameters[24] = {0};
    parameters[0] = parameters[5] = parameters[10] = parameters[15] = 1;
    const float triangle[6][7] = {{-1, -1, 0, 1, 0, 1, 1}, {3, -1, 0, 1, 0, 1, 1}, {-1, 3, 0, 1, 0, 1, 1},
        {-1, -1, 0, 1, 0, 1, 1}, {3, -1, 0, 1, 0, 1, 1}, {-1, 3, 0, 1, 0, 1, 1}};
    require(doe_vk_buffer_write(doe_resource_buffer_native(uniform), 0, parameters, sizeof(parameters)), "uniform");
    require(doe_vk_buffer_write(doe_resource_buffer_native(vertices), 16, triangle, sizeof(triangle)), "vertex subrange");
    /* The argument buffer receives no host write; the compute producer fills it. */
    const doe_resource_binding_limits binding_limits = {caps.uniform_offset_alignment, caps.storage_offset_alignment,
        caps.max_uniform_buffer_range, caps.max_storage_buffer_range};
    doe_resource_layout_entry entry = {.kind = DOE_BIND_BUFFER, .visibility = WGPUShaderStage_Compute,
        .buffer_type = WGPUBufferBindingType_Storage, .minimum_size = 4};
    doe_resource_layout *layout;
    doe_resource_pipeline_layout *pipeline_layout;
    assert(doe_resource_layout_create(&config, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    assert(doe_resource_pipeline_layout_create(&config, &layout, 1, 0, &pipeline_layout) == DOE_RESOURCE_OK);
    doe_resource_pipeline *producer = pipeline(&config, &life, pipeline_layout, argv[3], argv[4]);
    doe_resource_bind_group *producer_group = group(layout, arguments, &binding_limits);
    doe_resource_pipeline_layout_release(pipeline_layout);
    doe_resource_layout_release(layout);
    entry.visibility = WGPUShaderStage_Vertex;
    entry.buffer_type = WGPUBufferBindingType_Uniform;
    entry.minimum_size = 96;
    assert(doe_resource_layout_create(&config, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    assert(doe_resource_pipeline_layout_create(&config, &layout, 1, 0, &pipeline_layout) == DOE_RESOURCE_OK);
    doe_resource_bind_group *uniform_group = group(layout, uniform, &binding_limits);
    doe_vk_shader vertex_shader = load_shader(argv[1], "vs_main"), fragment_shader = load_shader(argv[2], "fs_main");
    doe_resource_render_pipeline *graphics = graphics_pipeline(&config, &life, pipeline_layout, vertex_shader, fragment_shader, false);
    doe_resource_render_pipeline *constant = graphics_pipeline(&config, &life, pipeline_layout, vertex_shader, fragment_shader, true);
    free((void *)vertex_shader.words);
    free((void *)fragment_shader.words);
    doe_resource_pipeline_layout_release(pipeline_layout);
    doe_resource_layout_release(layout);
    doe_vk_texture_desc image_desc = {.dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_RGBA8_UNORM,
        .width = 8, .height = 8, .depth_or_layers = 1, .mip_levels = 1,
        .usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT | DOE_VK_TEXTURE_COPY_SRC};
    doe_vk_texture *native_image;
    doe_vk_texture_view *native_view;
    require(doe_vk_texture_create(context, &image_desc, &native_image, NULL), "target");
    const doe_vk_texture_view_desc view_desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1, DOE_VK_ASPECT_ALL};
    require(doe_vk_texture_view_create(native_image, &view_desc, &native_view, NULL), "target view");
    const doe_resource_texture_desc texture_desc = {.dimension = WGPUTextureDimension_2D,
        .format = WGPUTextureFormat_RGBA8Unorm, .width = 8, .height = 8, .depth_or_layers = 1,
        .mip_levels = 1, .samples = 1, .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc};
    const doe_resource_texture_limits limits = {caps.max_texture_dimension_1d, caps.max_texture_dimension_2d,
        caps.max_texture_dimension_3d, caps.max_texture_array_layers};
    doe_resource_texture *texture;
    doe_resource_view *view;
    assert(doe_resource_texture_create(&config, &texture_desc, &limits,
        (doe_resource_native){native_image, &life, release_texture}, &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &(doe_resource_view_desc){0},
        (doe_resource_native){native_view, &life, release_view}, &view) == DOE_RESOURCE_OK);
    doe_vk_query_pool *native_pool;
    require(doe_vk_occlusion_pool_create(context, DRAW_COUNT, &native_pool, NULL), "queries");
    doe_resource_query_set *queries;
    assert(doe_resource_query_set_create(&config, WGPUQueryType_Occlusion, DRAW_COUNT,
        (doe_resource_native){native_pool, &life, release_queries}, &queries) == DOE_RESOURCE_OK);
    doe_resource_bundle_encoder *bundle_encoder;
    doe_resource_render_bundle_desc bundle_desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 1};
    assert(doe_resource_bundle_encoder_create(&config, &bundle_desc, &bundle_encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_pipeline(bundle_encoder, graphics) == DOE_RESOURCE_OK);
    doe_resource_dispatch_group uniform_input = {uniform_group, NULL, 0};
    assert(doe_resource_bundle_encoder_set_group(bundle_encoder, 0, &uniform_input) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_vertex(bundle_encoder, 1, vertices, 16, 168) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_vertex(bundle_encoder, 2, instances, 8, 8) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_draw_indirect(bundle_encoder, arguments, 4, false) == DOE_RESOURCE_OK);
    doe_resource_render_bundle *bundle;
    assert(doe_resource_bundle_encoder_finish(bundle_encoder, &bundle) == DOE_RESOURCE_OK);
    doe_resource_bundle_encoder_release(bundle_encoder);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&config, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_dispatch(encoder, producer, &producer_group, 1, 1, 1, 1) == DOE_RESOURCE_OK);
    const doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear,
        .color_store = WGPUStoreOp_Store, .clear_color = {0, 0, 1, 1}, .occlusion_queries = queries};
    doe_resource_render_pass *pass;
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    doe_resource_render_state state = *doe_resource_render_pass_state(pass);
    const uint64_t offsets[DRAW_COUNT] = {4, 24, 44, 64, 84, 104, 44, 104};
    for (uint32_t i = 0; i < DRAW_COUNT; i++) {
        state.scissor[0] = i == 0 || i == 6 ? 0 : 4;
        state.scissor[1] = i == 5 || i == 0 || i == 6 ? 0 : 4;
        state.scissor[2] = 4;
        state.scissor[3] = i == 0 || i == 6 ? 8 : 4;
        assert(doe_resource_render_pass_set_state(pass, &state) == DOE_RESOURCE_OK);
        assert(doe_resource_render_pass_begin_occlusion(pass, i) == DOE_RESOURCE_OK);
        if (i == 0) {
            assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_OK);
        } else {
            bind_draw(pass, i == 6 ? constant : graphics, uniform_group, vertices, instances, i == 7 ? 167 : 168);
            assert(doe_resource_render_pass_draw_indirect(pass, arguments, offsets[i], false) == DOE_RESOURCE_OK);
        }
        assert(doe_resource_render_pass_end_occlusion(pass) == DOE_RESOURCE_OK);
    }
    assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_render_pass_release(pass);
    assert(doe_resource_encoder_resolve_queries(encoder, queries, 0, DRAW_COUNT, query_result, 256) == DOE_RESOURCE_OK);
    const doe_resource_texture_copy copy = {.bytes_per_row = 32, .rows_per_image = 8, .width = 8, .height = 8, .depth_or_layers = 1};
    assert(doe_resource_encoder_copy_texture(encoder, readback, texture, &copy,
        DOE_RESOURCE_TEXTURE_TO_BUFFER, DOE_RESOURCE_COPY_NATIVE) == DOE_RESOURCE_OK);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_render_bundle_release(bundle);
    doe_resource_pipeline_release(producer);
    doe_resource_render_pipeline_release(graphics);
    doe_resource_render_pipeline_release(constant);
    doe_resource_bind_group_release(producer_group);
    doe_resource_bind_group_release(uniform_group);
    doe_resource_buffer_release(arguments);
    doe_resource_buffer_release(uniform);
    doe_resource_buffer_release(vertices);
    doe_resource_buffer_release(instances);
    doe_resource_buffer_release(readback);
    doe_resource_buffer_release(query_result);
    doe_resource_query_set_release(queries);
    doe_resource_view_release(view);
    doe_resource_texture_release(texture);
    assert(!life.native_releases);
    doe_vk_commands *native_commands;
    require(doe_vk_commands_create(context, &native_commands, NULL), "commands");
    doe_vk_context_release(context);
    size_t count;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    assert(count == 29 && records[0].kind == DOE_COMMAND_DISPATCH);
    doe_resource_binding resolved;
    assert(doe_resource_bind_group_resolve(records[0].value.dispatch.groups[0], NULL, 0, &resolved, 1) == DOE_RESOURCE_OK);
    const doe_vk_buffer_binding producer_binding = {doe_resource_buffer_native(resolved.resource.buffer.buffer),
        resolved.resource.buffer.offset, resolved.resource.buffer.size};
    require(doe_vk_commands_dispatch(native_commands, doe_resource_pipeline_native(records[0].value.dispatch.pipeline),
        &producer_binding, 1, 1, 1, 1), "GPU argument producer");
    size_t consumed = 99;
    assert(lower_pass(native_commands, validator, records + 1, 3, &consumed) == DOE_VK_UNSUPPORTED && consumed == 99);
    require(lower_pass(native_commands, validator, records + 1, count - 1, &consumed), "one guarded native pass");
    assert(consumed == 26);
    require(doe_vk_commands_resolve_queries(native_commands, doe_resource_query_set_native(records[27].value.resolve.set),
        0, DRAW_COUNT, doe_resource_buffer_native(records[27].value.resolve.destination), 256), "actual occlusion resolve");
    require(doe_vk_commands_read_texture(native_commands, doe_resource_texture_native(records[28].value.texture.texture),
        (doe_vk_texture_region){0, {0, 0, 0}, {8, 8, 1}}, doe_resource_buffer_native(records[28].value.texture.buffer),
        (doe_vk_texture_buffer_layout){0, 32, 8}), "actual pixels");
    doe_vk_draw_validator_release(validator);
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    require(doe_vk_commands_submit(native_commands, NULL), "submit guarded draws");
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(doe_vk_commands_release(native_commands) == DOE_VK_BUSY);
    uint8_t pixels[8][8][4];
    assert(doe_vk_buffer_read(native_readback, 0, pixels, sizeof(pixels)) == DOE_VK_BUSY);
    require(doe_vk_commands_wait(native_commands, UINT64_C(5000000000), NULL), "wait guarded draws");
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(life.native_releases == 12 && !life.allocations);
    require(doe_vk_buffer_read(native_readback, 0, pixels, sizeof(pixels)), "pixels after resource finalization");
    for (size_t y = 0; y < 8; y++) for (size_t x = 0; x < 8; x++) {
        const uint8_t expected[] = {(x < 4 || y < 4) ? 255 : 0, 0, 255, 255};
        assert(!memcmp(pixels[y][x], expected, sizeof(expected)));
    }
    uint64_t results[DRAW_COUNT];
    require(doe_vk_buffer_read(native_query_result, 256, results, sizeof(results)), "guarded draw visibility");
    for (size_t i = 0; i < DRAW_COUNT; i++) assert((results[i] != 0) == (i == 0 || i == 5 || i == 6));
    const uint32_t expected_arguments[ARGUMENT_WORDS] = {
        0xa5a5a5a5, 3, 1, 0, 0, 0xa5a5a5a5, 3, 1, UINT32_MAX, 0, 0xa5a5a5a5, 3, 2, 0, 0,
        0xa5a5a5a5, 3, 1, 0, 1, 0xa5a5a5a5, 0, 1, 0, 0, 0xa5a5a5a5, 6, 1, 0, 0};
    uint32_t produced[ARGUMENT_WORDS];
    require(doe_vk_buffer_read(native_arguments, 0, produced, sizeof(produced)), "unmodified GPU source arguments");
    assert(!memcmp(produced, expected_arguments, sizeof(produced)));
    require(doe_vk_commands_release(native_commands), "retire native draw leases");
    puts("PASS GPU-produced indirect draw bundle/vertex-overflow/instance-overrun/firstInstance/zero/valid/stride-zero/last-byte controls, exact 64 pixels/eight occlusion results, unchanged source/padding, pending guards and twelve finalizers");
    return 0;
}
