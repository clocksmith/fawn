/* SPDX-License-Identifier: MIT
 * Resource/bundle ownership consumed by real Vulkan, software ICD only.
 * Shader bytes are retained Tint fixtures, not the C graphics compiler. */
#include "vulkan_fixture.h"
#include "doe_vk_render.h"

static doe_vk_shader load_shader(const char *path, const char *entry)
{
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size >= 20 && size % 4 == 0 && size <= 16 * 1024 * 1024);
    assert(fseek(file, 0, SEEK_SET) == 0);
    uint32_t *words = malloc((size_t)size);
    assert(words && fread(words, 1, (size_t)size, file) == (size_t)size);
    assert(fclose(file) == 0);
    return (doe_vk_shader){words, (size_t)size / 4, entry};
}

/* This consumer deliberately admits only its exercised graphics signature.
 * A bounded borrowed span lowers into ONE native pass, never a second recorder.
 * Unsupported events/state reject before any native pass preparation. */
static doe_vk_status validate_render_packet(const doe_resource_render_packet *packet)
{
    if (!packet || packet->targets.depth_stencil || packet->targets.resolve_target ||
        packet->targets.color_store != WGPUStoreOp_Store || packet->indexed || packet->indirect ||
        packet->state.stencil_reference) return DOE_VK_UNSUPPORTED;
    for (size_t i = 0; i < 4; i++) if (packet->state.blend[i] != 0) return DOE_VK_UNSUPPORTED;
    return DOE_VK_OK;
}

static doe_vk_status prepare_draw(const doe_resource_render_packet *packet,
    doe_vk_render_pass_draw *draw, doe_vk_binding_resource *binding, doe_vk_buffer_binding *vertex)
{
    if (!packet->groups[0] || !packet->vertices[0].buffer) return DOE_VK_UNSUPPORTED;
    for (size_t i = 1; i < DOE_RESOURCE_MAX_GROUPS; i++)
        if (packet->groups[i]) return DOE_VK_UNSUPPORTED;
    for (size_t i = 1; i < DOE_RESOURCE_MAX_VERTEX_BUFFERS; i++)
        if (packet->vertices[i].buffer) return DOE_VK_UNSUPPORTED;
    size_t count;
    const doe_resource_binding *bindings = doe_resource_bind_group_bindings(packet->groups[0], &count);
    if (!bindings || count != 1 || bindings[0].kind != DOE_BIND_BUFFER || bindings[0].binding != 0)
        return DOE_VK_UNSUPPORTED;
    doe_resource_binding resolved;
    if (doe_resource_bind_group_resolve(packet->groups[0], packet->offsets.values[0],
        packet->offsets.counts[0], &resolved, 1) != DOE_RESOURCE_OK) return DOE_VK_INVALID_ARGUMENT;
    *binding = (doe_vk_binding_resource){.kind = DOE_VK_BINDING_UNIFORM,
        .value.buffer = {doe_resource_buffer_native(resolved.resource.buffer.buffer),
            resolved.resource.buffer.offset, resolved.resource.buffer.size}};
    *vertex = (doe_vk_buffer_binding){doe_resource_buffer_native(packet->vertices[0].buffer),
        packet->vertices[0].offset, packet->vertices[0].size};
    *draw = (doe_vk_render_pass_draw){.pipeline = doe_resource_render_pipeline_native(packet->pipeline),
        .bindings = binding, .binding_count = 1, .vertices = vertex, .vertex_buffer_count = 1,
        .draw = {.vertex_count = packet->count, .instance_count = packet->instances,
            .first_vertex = packet->first, .first_instance = packet->first_instance,
            .viewport_convention = DOE_VK_VIEWPORT_WEBGPU}};
    memcpy(draw->draw.viewport, packet->state.viewport, sizeof(draw->draw.viewport));
    memcpy(draw->draw.scissor, packet->state.scissor, sizeof(draw->draw.scissor));
    return DOE_VK_OK;
}

static doe_vk_status render_pass(doe_vk_commands *commands,
    const doe_resource_command *records, size_t count, size_t *consumed)
{
    if (!count || records[0].kind != DOE_COMMAND_RENDER) return DOE_VK_UNSUPPORTED;
    const doe_resource_render_packet *begin = doe_resource_render_record_borrow(records[0].value.render);
    if (validate_render_packet(begin) != DOE_VK_OK || !begin->boundary_only ||
        !begin->first_in_pass || begin->last_in_pass) return DOE_VK_UNSUPPORTED;
    doe_resource_query_set *queries = begin->targets.occlusion_queries;
    if (queries && doe_resource_query_set_type(queries) != WGPUQueryType_Occlusion)
        return DOE_VK_UNSUPPORTED;
    doe_vk_clear_color clear = {0};
    const doe_vk_clear_color *clear_pointer = NULL;
    if (begin->targets.color_load == WGPULoadOp_Clear) {
        for (size_t i = 0; i < 4; i++) clear.float32[i] = (float)begin->targets.clear_color[i];
        clear_pointer = &clear;
    } else if (begin->targets.color_load != WGPULoadOp_Load) return DOE_VK_UNSUPPORTED;
    enum { MAX_CONSUMER_DRAWS = 3, MAX_CONSUMER_OPS = 9 };
    doe_vk_render_pass_op ops[MAX_CONSUMER_OPS];
    doe_vk_binding_resource bindings[MAX_CONSUMER_DRAWS];
    doe_vk_buffer_binding vertices[MAX_CONSUMER_DRAWS];
    size_t draw_count = 0, op_count = 0;
    for (size_t i = 1; i < count; i++) {
        const doe_resource_command *record = &records[i];
        if (record->kind == DOE_COMMAND_BEGIN_OCCLUSION || record->kind == DOE_COMMAND_END_OCCLUSION) {
            if (!queries || record->value.timestamp.set != queries || op_count == MAX_CONSUMER_OPS)
                return DOE_VK_UNSUPPORTED;
            ops[op_count++] = (doe_vk_render_pass_op){
                .kind = record->kind == DOE_COMMAND_BEGIN_OCCLUSION ?
                    DOE_VK_RENDER_BEGIN_OCCLUSION : DOE_VK_RENDER_END_OCCLUSION,
                .value.query_index = record->value.timestamp.index};
            continue;
        }
        if (record->kind != DOE_COMMAND_RENDER) return DOE_VK_UNSUPPORTED;
        const doe_resource_render_packet *packet = doe_resource_render_record_borrow(record->value.render);
        if (validate_render_packet(packet) != DOE_VK_OK || packet->first_in_pass ||
            packet->targets.color != begin->targets.color || packet->targets.occlusion_queries != queries)
            return DOE_VK_UNSUPPORTED;
        if (packet->boundary_only) {
            if (!packet->last_in_pass) return DOE_VK_UNSUPPORTED;
            /* The native owner validates query nesting/reset/availability. This
             * adapter preserves event order and identity, not a second state map. */
            doe_vk_status status = doe_vk_commands_render_pass_ops(commands,
                doe_resource_view_native(begin->targets.color), clear_pointer,
                doe_resource_query_set_native(queries), ops, op_count);
            if (status == DOE_VK_OK) *consumed = i + 1;
            return status;
        }
        if (packet->last_in_pass || draw_count == MAX_CONSUMER_DRAWS || op_count == MAX_CONSUMER_OPS)
            return DOE_VK_UNSUPPORTED;
        ops[op_count].kind = DOE_VK_RENDER_DRAW;
        doe_vk_status status = prepare_draw(packet, &ops[op_count].value.draw,
            &bindings[draw_count], &vertices[draw_count]);
        if (status != DOE_VK_OK) return status;
        draw_count++;
        op_count++;
    }
    return DOE_VK_UNSUPPORTED; /* Missing end must not emit a partial pass. */
}

static doe_resource_buffer *adopt_buffer(const doe_resource_config *config, lifecycle *life,
    doe_vk_context *context, uint64_t size, uint32_t native_usage, WGPUBufferUsage usage)
{
    doe_vk_error error = {0};
    doe_vk_buffer *native;
    require(doe_vk_buffer_create(context, size, native_usage, DOE_VK_MEMORY_HOST, &native, &error), "buffer create");
    doe_resource_buffer_desc desc = {size, usage, false};
    doe_resource_buffer *buffer;
    assert(doe_resource_buffer_create(config, &desc,
        (doe_resource_native){native, life, release_buffer}, &buffer) == DOE_RESOURCE_OK);
    return buffer;
}

int main(int argc, char **argv)
{
    if (argc != 4 || strcmp(argv[3], "--allow-software")) return 2;
    doe_vk_options options = {.allow_cpu_adapter = true, .require_graphics_queue = true};
    doe_vk_context *context;
    doe_vk_error error = {0};
    require(doe_vk_context_create(&options, &context, &error), "create graphics context");
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    if (!caps.software_adapter) {
        doe_vk_context_release(context);
        fputs("Refusing non-software adapter in software-only developer check\n", stderr);
        return 77;
    }
    printf("Resource bundle Vulkan consumer: %s; retained Tint shaders, not C graphics compiler or physical GPU qualification\n",
        caps.device_name);
    lifecycle life = {0};
    doe_resource_config config = {.allocator = {&life, NULL, allocate, deallocate},
        .device = {context, alive, retain, release}};
    doe_resource_buffer *uniform = adopt_buffer(&config, &life, context, 256,
        DOE_VK_BUFFER_UNIFORM, WGPUBufferUsage_Uniform);
    doe_resource_buffer *vertices = adopt_buffer(&config, &life, context, 108,
        DOE_VK_BUFFER_VERTEX, WGPUBufferUsage_Vertex);
    doe_resource_buffer *readback = adopt_buffer(&config, &life, context, 256,
        DOE_VK_BUFFER_COPY_DST, WGPUBufferUsage_CopyDst);
    doe_vk_buffer *native_readback = doe_resource_buffer_native(readback);
    doe_resource_buffer *query_result = adopt_buffer(&config, &life, context, 512,
        DOE_VK_BUFFER_COPY_DST, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopyDst);
    doe_vk_buffer *native_query_result = doe_resource_buffer_native(query_result);
    uint8_t sentinel[512];
    memset(sentinel, 0xa5, sizeof(sentinel));
    require(doe_vk_buffer_write(native_query_result, 0, sentinel, sizeof(sentinel)), "query sentinel");
    doe_vk_query_pool *native_pool;
    require(doe_vk_occlusion_pool_create(context, 4, &native_pool, &error), "occlusion pool");
    doe_resource_query_set *queries;
    assert(doe_resource_query_set_create(&config, WGPUQueryType_Occlusion, 4,
        (doe_resource_native){native_pool, &life, release_queries}, &queries) == DOE_RESOURCE_OK);
    float parameters[24] = {0};
    parameters[0] = parameters[5] = parameters[10] = parameters[15] = 1;
    const float triangle[3][9] = {
        {-1, -1, 0, 1, 0, 1, 1, 0, 0}, {3, -1, 0, 1, 0, 1, 1, 0, 0}, {-1, 3, 0, 1, 0, 1, 1, 0, 0}
    };
    require(doe_vk_buffer_write(doe_resource_buffer_native(uniform), 0, parameters, sizeof(parameters)), "parameters");
    require(doe_vk_buffer_write(doe_resource_buffer_native(vertices), 0, triangle, sizeof(triangle)), "triangle");
    doe_vk_texture_desc native_desc = {.dimension = DOE_VK_TEXTURE_2D, .format = DOE_VK_FORMAT_RGBA8_UNORM,
        .width = 8, .height = 8, .depth_or_layers = 1, .mip_levels = 1,
        .usage = DOE_VK_TEXTURE_COLOR_ATTACHMENT | DOE_VK_TEXTURE_COPY_SRC};
    doe_vk_texture *native_image;
    doe_vk_texture_view *native_view;
    require(doe_vk_texture_create(context, &native_desc, &native_image, &error), "image");
    doe_vk_texture_view_desc native_view_desc = {DOE_VK_VIEW_2D, 0, 1, 0, 1};
    require(doe_vk_texture_view_create(native_image, &native_view_desc, &native_view, &error), "view");
    doe_resource_texture_desc image_desc = {.dimension = WGPUTextureDimension_2D,
        .format = WGPUTextureFormat_RGBA8Unorm, .width = 8, .height = 8, .depth_or_layers = 1,
        .mip_levels = 1, .samples = 1, .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc};
    doe_resource_texture_limits image_limits = {caps.max_texture_dimension_1d, caps.max_texture_dimension_2d,
        caps.max_texture_dimension_3d, caps.max_texture_array_layers};
    doe_resource_texture *image;
    doe_resource_view *view;
    assert(doe_resource_texture_create(&config, &image_desc, &image_limits,
        (doe_resource_native){native_image, &life, release_texture}, &image) == DOE_RESOURCE_OK);
    doe_resource_view_desc view_desc = {0};
    assert(doe_resource_view_create(image, &view_desc,
        (doe_resource_native){native_view, &life, release_view}, &view) == DOE_RESOURCE_OK);
    doe_resource_layout_entry entry = {.kind = DOE_BIND_BUFFER, .visibility = WGPUShaderStage_Vertex,
        .buffer_type = WGPUBufferBindingType_Uniform, .minimum_size = 96};
    doe_resource_layout *layout;
    doe_resource_pipeline_layout *pipeline_layout;
    assert(doe_resource_layout_create(&config, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    assert(doe_resource_pipeline_layout_create(&config, &layout, 1, 0, &pipeline_layout) == DOE_RESOURCE_OK);
    doe_resource_binding binding = {.kind = DOE_BIND_BUFFER, .resource.buffer = {uniform, 0, 96}};
    doe_resource_binding_limits binding_limits = {caps.uniform_offset_alignment,
        caps.storage_offset_alignment, caps.max_uniform_buffer_range, caps.max_storage_buffer_range};
    doe_resource_bind_group *group;
    assert(doe_resource_bind_group_create(layout, &binding, 1, &binding_limits, &group) == DOE_RESOURCE_OK);
    doe_vk_shader vertex_shader = load_shader(argv[1], "vs_main");
    doe_vk_shader fragment_shader = load_shader(argv[2], "fs_main");
    doe_vk_vertex_layout vertex_layout = {36, false};
    doe_vk_vertex_attribute attributes[] = {{0, 0, 0, DOE_VK_VERTEX_FLOAT32X3},
        {1, 0, 12, DOE_VK_VERTEX_FLOAT32X4}, {2, 0, 28, DOE_VK_VERTEX_FLOAT32X2}};
    doe_vk_binding_layout native_layout = {0, 0, DOE_VK_BINDING_UNIFORM};
    doe_vk_render_pipeline_desc native_pipeline_desc = {.vertex = vertex_shader, .fragment = fragment_shader,
        .color_format = DOE_VK_FORMAT_RGBA8_UNORM, .topology = DOE_VK_TRIANGLES, .color_write_mask = 15,
        .vertex_buffer_count = 1, .attribute_count = 3, .vertex_buffers = &vertex_layout, .attributes = attributes,
        .binding_count = 1, .bindings = &native_layout};
    doe_vk_pipeline *native_pipeline;
    require(doe_vk_render_pipeline_create(context, &native_pipeline_desc, &native_pipeline, &error), "pipeline");
    free((void *)vertex_shader.words);
    free((void *)fragment_shader.words);
    doe_resource_render_pipeline_desc pipeline_desc = {.color_format = WGPUTextureFormat_RGBA8Unorm,
        .samples = 1, .vertex_count = 1, .vertices = {{36, 36, WGPUVertexStepMode_Vertex}}};
    doe_resource_render_pipeline *pipeline;
    assert(doe_resource_render_pipeline_create(&config, pipeline_layout, &pipeline_desc,
        (doe_resource_native){native_pipeline, &life, release_pipeline}, &pipeline) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout_release(pipeline_layout);
    doe_resource_layout_release(layout);
    doe_resource_bundle_encoder *bundle_encoder;
    doe_resource_render_bundle_desc bundle_desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 1};
    assert(doe_resource_bundle_encoder_create(&config, &bundle_desc, &bundle_encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_pipeline(bundle_encoder, pipeline) == DOE_RESOURCE_OK);
    doe_resource_dispatch_group dispatch_group = {group, NULL, 0};
    assert(doe_resource_bundle_encoder_set_group(bundle_encoder, 0, &dispatch_group) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_set_vertex(bundle_encoder, 0, vertices, 0, 108) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_draw(bundle_encoder, false, 3, 1, 0, 0, 0) == DOE_RESOURCE_OK);
    doe_resource_render_bundle *bundle;
    assert(doe_resource_bundle_encoder_finish(bundle_encoder, &bundle) == DOE_RESOURCE_OK);
    doe_resource_bundle_encoder_release(bundle_encoder);
    doe_resource_render_pipeline_release(pipeline);
    doe_resource_bind_group_release(group);
    doe_resource_buffer_release(uniform);
    doe_resource_buffer_release(vertices);
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass;
    doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear,
        .color_store = WGPUStoreOp_Store, .clear_color = {0, 0, 1, 1}, .occlusion_queries = queries};
    assert(doe_resource_encoder_create(&config, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_begin_occlusion(pass, 0) == DOE_RESOURCE_OK);
    doe_resource_render_state state = *doe_resource_render_pass_state(pass);
    state.scissor[2] = 4;
    assert(doe_resource_render_pass_set_state(pass, &state) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_OK);
    state.scissor[0] = 4;
    state.scissor[3] = 4;
    assert(doe_resource_render_pass_set_state(pass, &state) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_end_occlusion(pass) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_begin_occlusion(pass, 1) == DOE_RESOURCE_OK);
    state.scissor[2] = 0;
    assert(doe_resource_render_pass_set_state(pass, &state) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_end_occlusion(pass) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_begin_occlusion(pass, 2) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_end_occlusion(pass) == DOE_RESOURCE_OK);
    doe_resource_render_bundle_release(bundle);
    assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_render_pass_release(pass);
    assert(doe_resource_encoder_resolve_queries(encoder, queries, 0, 4, query_result, 256) == DOE_RESOURCE_OK);
    targets.color_load = WGPULoadOp_Load;
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    /* A later zero-draw pass may reuse index0; native reset must replace its hit. */
    assert(doe_resource_render_pass_begin_occlusion(pass, 0) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_end_occlusion(pass) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_render_pass_release(pass);
    assert(doe_resource_encoder_resolve_queries(encoder, queries, 0, 1, query_result, 0) == DOE_RESOURCE_OK);
    doe_resource_texture_copy read = {.bytes_per_row = 32, .rows_per_image = 8,
        .width = 8, .height = 8, .depth_or_layers = 1};
    assert(doe_resource_encoder_copy_texture(encoder, readback, image, &read,
        DOE_RESOURCE_TEXTURE_TO_BUFFER, DOE_RESOURCE_COPY_NATIVE) == DOE_RESOURCE_OK);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_buffer_release(readback);
    doe_resource_buffer_release(query_result);
    doe_resource_query_set_release(queries);
    doe_resource_view_release(view);
    doe_resource_texture_release(image);
    assert(!life.native_releases);
    doe_vk_commands *native_commands;
    require(doe_vk_commands_create(context, &native_commands, &error), "commands");
    doe_vk_context_release(context); /* All remaining device leases belong to owners. */
    size_t count;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    assert(count == 18);
    doe_resource_render_packet unsupported = *doe_resource_render_record_borrow(records[0].value.render);
    unsupported.targets.color_store = WGPUStoreOp_Discard;
    assert(validate_render_packet(&unsupported) == DOE_VK_UNSUPPORTED);
    doe_resource_command unsupported_events[] = {records[0], {.kind = DOE_COMMAND_WRITE_TIMESTAMP}};
    size_t consumed = 99;
    assert(render_pass(native_commands, unsupported_events, 2, &consumed) == DOE_VK_UNSUPPORTED && consumed == 99);
    assert(render_pass(native_commands, records, 3, &consumed) == DOE_VK_UNSUPPORTED && consumed == 99);
    size_t native_passes = 0;
    for (size_t i = 0; i < count;) {
        if (records[i].kind == DOE_COMMAND_RENDER) {
            require(render_pass(native_commands, records + i, count - i, &consumed), "render owned pass span");
            i += consumed;
            native_passes++;
        } else if (records[i].kind == DOE_COMMAND_RESOLVE_QUERY) {
            require(doe_vk_commands_resolve_queries(native_commands,
                doe_resource_query_set_native(records[i].value.resolve.set), records[i].value.resolve.first,
                records[i].value.resolve.count, doe_resource_buffer_native(records[i].value.resolve.destination),
                records[i].value.resolve.offset), "resolve owned occlusion span");
            i++;
        } else {
            assert(records[i].kind == DOE_COMMAND_BUFFER_TEXTURE &&
                records[i].value.texture.direction == DOE_RESOURCE_TEXTURE_TO_BUFFER);
            const doe_resource_texture_copy *copy = &records[i].value.texture.copy;
            doe_vk_texture_region region = {copy->mip, {copy->origin[0], copy->origin[1], copy->origin[2]},
                {copy->width, copy->height, copy->depth_or_layers}};
            doe_vk_texture_buffer_layout packed = {copy->offset,
                records[i].value.texture.region.pitch, records[i].value.texture.region.image_rows};
            require(doe_vk_commands_read_texture(native_commands,
                doe_resource_texture_native(records[i].value.texture.texture), region,
                doe_resource_buffer_native(records[i].value.texture.buffer), packed), "read owned attachment");
            i++;
        }
    }
    assert(native_passes == 2); /* Three draws then a zero-draw query-only load/store pass. */
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    require(doe_vk_commands_submit(native_commands, &error), "submit");
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(doe_vk_commands_release(native_commands) == DOE_VK_BUSY);
    uint8_t pixels[8][8][4];
    assert(doe_vk_buffer_read(native_readback, 0, pixels, sizeof(pixels)) == DOE_VK_BUSY);
    uint64_t results[4];
    assert(doe_vk_buffer_read(native_query_result, 256, results, sizeof(results)) == DOE_VK_BUSY);
    assert(!life.native_releases);
    require(doe_vk_commands_wait(native_commands, UINT64_C(5000000000), &error), "wait");
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(life.native_releases == 8 && !life.allocations);
    /* Native command retirement, not resource wrapper lifetime, owns this borrow. */
    require(doe_vk_buffer_read(native_readback, 0, pixels, sizeof(pixels)), "readback after resource finalization");
    for (unsigned y = 0; y < 8; y++) for (unsigned x = 0; x < 8; x++) {
        uint8_t expected[4] = {(x < 4 || y < 4) ? 255 : 0, 0, 255, 255};
        assert(!memcmp(pixels[y][x], expected, sizeof(expected)));
    }
    require(doe_vk_buffer_read(native_query_result, 256, results, sizeof(results)), "read actual occlusion results");
    assert(results[0] != 0 && results[1] == 0 && results[2] == 0 && results[3] == 0);
    uint64_t reused;
    require(doe_vk_buffer_read(native_query_result, 0, &reused, sizeof(reused)), "read later-pass reused query");
    assert(reused == 0);
    require(doe_vk_buffer_read(native_query_result, 0, sentinel, sizeof(sentinel)), "query payload boundaries");
    for (size_t i = 0; i < sizeof(sentinel); i++) {
        if (i < 8 || (i >= 256 && i < 288)) continue;
        assert(sentinel[i] == 0xa5);
    }
    require(doe_vk_commands_release(native_commands), "retire native owner");
    puts("PASS resource bundle/query event order, real hit/clipped/empty/unwritten/later-pass reuse, exact 64 pixels and query payload, pending retention and eight terminal native finalizers");
    return 0;
}
