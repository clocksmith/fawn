/* SPDX-License-Identifier: MIT */
#include "fixture.h"

typedef struct draw_case {
    doe_resource_vertex_layout layout;
    uint64_t size;
    bool bind, indexed, indirect;
    WGPUPrimitiveTopology topology;
    WGPUIndexFormat strip_format, index_format;
    uint32_t count, instances, first, first_instance;
    doe_resource_status expected;
} draw_case;

static doe_resource_view *attachment(fixture *f, native_token tokens[2])
{
    doe_resource_config c = config(f);
    doe_resource_texture_desc desc = texture_desc();
    desc.usage = WGPUTextureUsage_RenderAttachment;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    doe_resource_view *view;
    doe_resource_view_desc view_desc = {0};
    assert(doe_resource_texture_create(&c, &desc, &limits, native(f, &tokens[0]), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &view_desc, native(f, &tokens[1]), &view) == DOE_RESOURCE_OK);
    doe_resource_texture_release(texture);
    return view;
}

static void run_draw_case(const draw_case *test, bool bundled)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token pipeline_token = {0}, vertex_token = {0}, index_token = {0}, indirect_token = {0};
    doe_resource_pipeline_layout *layout;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
    doe_resource_render_pipeline_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm,
        .samples = 1, .vertex_count = 1, .topology = test->topology, .strip_index_format = test->strip_format};
    desc.vertices[0] = test->layout;
    doe_resource_render_pipeline *pipeline;
    assert(doe_resource_render_pipeline_create(&c, layout, &desc, native(&f, &pipeline_token), &pipeline) == DOE_RESOURCE_OK);
    doe_resource_pipeline_layout_release(layout);
    const doe_resource_render_pipeline_desc *normalized = doe_resource_render_pipeline_description(pipeline);
    bool required = test->layout.step_mode || test->layout.last_attribute_end;
    assert(doe_resource_render_pipeline_vertex_required(pipeline, 0) == required);
    assert(!doe_resource_render_pipeline_vertex_required(pipeline, 1));
    assert(!doe_resource_render_pipeline_vertex_required(NULL, 0));
    assert(normalized->topology == (test->topology ? test->topology : WGPUPrimitiveTopology_TriangleList));
    if (!required) assert(!normalized->vertices[0].stride && !normalized->vertices[0].last_attribute_end);
    if (required && !test->layout.step_mode) assert(normalized->vertices[0].step_mode == WGPUVertexStepMode_Vertex);
    doe_resource_buffer *vertices = test->bind ? buffer(&f, &vertex_token, test->size, WGPUBufferUsage_Vertex) : NULL;
    doe_resource_buffer *indices = test->indexed ? buffer(&f, &index_token, 64, WGPUBufferUsage_Index) : NULL;
    doe_resource_buffer *arguments = test->indirect ? buffer(&f, &indirect_token, 20, WGPUBufferUsage_Indirect) : NULL;
    native_token attachment_tokens[2] = {{0}};
    doe_resource_view *view = attachment(&f, attachment_tokens);
    doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear,
        .color_store = WGPUStoreOp_Store};
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    doe_resource_status status;
    if (bundled) {
        doe_resource_render_bundle_desc signature = {.color_format = desc.color_format, .samples = 1};
        doe_resource_bundle_encoder *builder;
        assert(doe_resource_bundle_encoder_create(&c, &signature, &builder) == DOE_RESOURCE_OK);
        assert(doe_resource_bundle_encoder_set_pipeline(builder, pipeline) == DOE_RESOURCE_OK);
        if (vertices) assert(doe_resource_bundle_encoder_set_vertex(builder, 0, vertices, 0, test->size) == DOE_RESOURCE_OK);
        if (indices) assert(doe_resource_bundle_encoder_set_index(builder, indices, test->index_format, 0, 64) == DOE_RESOURCE_OK);
        status = test->indirect ? doe_resource_bundle_encoder_draw_indirect(builder, arguments, 0, test->indexed) :
            doe_resource_bundle_encoder_draw(builder, test->indexed, test->count, test->instances,
                test->first, 0, test->first_instance);
        assert(status == test->expected);
        doe_resource_render_bundle *bundle = NULL;
        assert(doe_resource_bundle_encoder_finish(builder, &bundle) == status);
        doe_resource_bundle_encoder_release(builder);
        if (status == DOE_RESOURCE_OK) {
            assert(doe_resource_render_pass_execute_bundles(pass, &bundle, 1) == DOE_RESOURCE_OK);
            doe_resource_render_bundle_release(bundle);
        } else assert(!bundle);
    } else {
        assert(doe_resource_render_pass_set_pipeline(pass, pipeline) == DOE_RESOURCE_OK);
        if (vertices) assert(doe_resource_render_pass_set_vertex(pass, 0, vertices, 0, test->size) == DOE_RESOURCE_OK);
        if (indices) assert(doe_resource_render_pass_set_index(pass, indices, test->index_format, 0, 64) == DOE_RESOURCE_OK);
        if (test->indirect) status = doe_resource_render_pass_draw_indirect(pass, arguments, 0, test->indexed);
        else if (test->indexed) status = doe_resource_render_pass_draw_indexed(pass,
            test->count, test->instances, test->first, 0, test->first_instance);
        else status = doe_resource_render_pass_draw(pass, test->count, test->instances, test->first, test->first_instance);
        assert(status == test->expected);
    }
    doe_resource_commands *commands = NULL;
    if (status == DOE_RESOURCE_OK) {
        assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    }
    doe_resource_render_pass_release(pass);
    doe_resource_encoder_release(encoder);
    doe_resource_render_pipeline_release(pipeline);
    doe_resource_buffer_release(vertices);
    doe_resource_buffer_release(indices);
    doe_resource_buffer_release(arguments);
    doe_resource_view_release(view);
    if (commands) {
        assert(!pipeline_token.releases);
        assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
        doe_resource_commands_release(commands);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    }
    assert(pipeline_token.releases == 1);
    assert(vertex_token.releases == (unsigned)test->bind);
    assert(index_token.releases == (unsigned)test->indexed);
    assert(indirect_token.releases == (unsigned)test->indirect);
    empty(&f);
}

static void test_draw_boundaries(void)
{
    const draw_case cases[] = {
        {.layout = {3, 0, WGPUVertexStepMode_Undefined}, .count = 3, .instances = 1},
        {.layout = {16, 0, WGPUVertexStepMode_Vertex}, .count = 3, .instances = 1, .expected = DOE_RESOURCE_UNAVAILABLE},
        {.layout = {16, 0, WGPUVertexStepMode_Vertex}, .bind = true, .size = 16, .count = 2, .instances = 1},
        {.layout = {16, 0, WGPUVertexStepMode_Vertex}, .bind = true, .count = 2, .instances = 1, .expected = DOE_RESOURCE_RANGE},
        {.layout = {16, 12, WGPUVertexStepMode_Undefined}, .bind = true, .size = 16, .count = 1, .instances = 1},
        {.layout = {16, 12, WGPUVertexStepMode_Vertex}, .count = 1, .expected = DOE_RESOURCE_UNAVAILABLE},
        {.layout = {16, 12, WGPUVertexStepMode_Vertex}, .bind = true, .size = 16, .first = 2, .expected = DOE_RESOURCE_RANGE},
        {.layout = {16, 12, WGPUVertexStepMode_Vertex}, .bind = true},
        {.layout = {16, 12, WGPUVertexStepMode_Vertex}, .bind = true, .size = 12, .first = 1},
        {.layout = {16, 12, WGPUVertexStepMode_Instance}, .bind = true, .first_instance = 1, .expected = DOE_RESOURCE_RANGE},
        {.layout = {16, 12, WGPUVertexStepMode_Instance}, .bind = true},
        {.layout = {16, 12, WGPUVertexStepMode_Vertex}, .bind = true, .indexed = true,
            .index_format = WGPUIndexFormat_Uint16, .count = 1, .instances = 1},
        {.layout = {16, 12, WGPUVertexStepMode_Instance}, .bind = true, .indexed = true,
            .index_format = WGPUIndexFormat_Uint16, .count = 1, .instances = 1, .expected = DOE_RESOURCE_RANGE},
        {.indexed = true, .topology = WGPUPrimitiveTopology_TriangleStrip, .strip_format = WGPUIndexFormat_Uint16,
            .index_format = WGPUIndexFormat_Uint16, .count = 3, .instances = 1},
        {.indexed = true, .topology = WGPUPrimitiveTopology_LineStrip, .strip_format = WGPUIndexFormat_Uint32,
            .index_format = WGPUIndexFormat_Uint16, .count = 3, .instances = 1, .expected = DOE_RESOURCE_LAYOUT},
        {.indexed = true, .topology = WGPUPrimitiveTopology_TriangleStrip,
            .index_format = WGPUIndexFormat_Uint16, .count = 3, .instances = 1, .expected = DOE_RESOURCE_LAYOUT},
        {.indexed = true, .index_format = WGPUIndexFormat_Uint16, .count = 33, .expected = DOE_RESOURCE_RANGE},
        {.indexed = true, .index_format = WGPUIndexFormat_Uint16, .first = 33, .expected = DOE_RESOURCE_RANGE},
        {.layout = {UINT64_MAX - 3, 12, WGPUVertexStepMode_Vertex}, .bind = true, .size = 16,
            .count = UINT32_MAX, .first = UINT32_MAX, .expected = DOE_RESOURCE_OVERFLOW},
        {.layout = {0, 12, WGPUVertexStepMode_Vertex}, .bind = true, .size = 12, .first = UINT32_MAX},
        {.layout = {16, 12, WGPUVertexStepMode_Instance}, .bind = true, .indexed = true, .indirect = true,
            .index_format = WGPUIndexFormat_Uint16}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        run_draw_case(&cases[i], false);
        run_draw_case(&cases[i], true);
    }
}

static void test_pipeline_topology(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_pipeline_layout *layout;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
    const WGPUPrimitiveTopology topologies[] = {(WGPUPrimitiveTopology)99, WGPUPrimitiveTopology_TriangleList,
        WGPUPrimitiveTopology_LineStrip, WGPUPrimitiveTopology_TriangleStrip};
    const WGPUIndexFormat indices[] = {WGPUIndexFormat_Undefined, WGPUIndexFormat_Uint16,
        (WGPUIndexFormat)99, WGPUIndexFormat_Uint32};
    for (size_t i = 0; i < 4; i++) {
        native_token token = {0};
        doe_resource_render_pipeline_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm,
            .samples = 1, .topology = topologies[i], .strip_index_format = indices[i]};
        doe_resource_render_pipeline *pipeline = (void *)(uintptr_t)1;
        size_t allocations = f.allocations;
        doe_resource_status status = doe_resource_render_pipeline_create(&c, layout, &desc, native(&f, &token), &pipeline);
        if (i == 3) {
            assert(status == DOE_RESOURCE_OK);
            doe_resource_render_pipeline_release(pipeline);
            assert(token.releases == 1);
        } else assert(status == DOE_RESOURCE_LAYOUT && pipeline == (void *)(uintptr_t)1 &&
            !token.releases && f.allocations == allocations);
    }
    doe_resource_pipeline_layout_release(layout);
    empty(&f);
}

static void test_frontend_rejection(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_render_bundle_desc desc = {.color_format = WGPUTextureFormat_RGBA8Unorm, .samples = 1};
    doe_resource_bundle_encoder *builder;
    assert(doe_resource_bundle_encoder_create(&c, &desc, &builder) == DOE_RESOURCE_OK);
    assert(doe_resource_bundle_encoder_invalidate(builder, DOE_RESOURCE_PENDING) == DOE_RESOURCE_INVALID_ARGUMENT);
    assert(doe_resource_bundle_encoder_invalidate(builder, DOE_RESOURCE_OUT_OF_MEMORY) == DOE_RESOURCE_OUT_OF_MEMORY);
    assert(doe_resource_bundle_encoder_invalidate(builder, DOE_RESOURCE_UNSUPPORTED) == DOE_RESOURCE_OUT_OF_MEMORY);
    doe_resource_render_bundle *bundle = (void *)(uintptr_t)1;
    assert(doe_resource_bundle_encoder_finish(builder, &bundle) == DOE_RESOURCE_OUT_OF_MEMORY);
    assert(bundle == (void *)(uintptr_t)1);
    doe_resource_bundle_encoder_release(builder);
    native_token attachment_tokens[2] = {{0}};
    doe_resource_view *view = attachment(&f, attachment_tokens);
    doe_resource_render_targets targets = {.color = view, .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store};
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_invalidate(pass, DOE_RESOURCE_OK) == DOE_RESOURCE_INVALID_ARGUMENT);
    assert(doe_resource_render_pass_invalidate(pass, DOE_RESOURCE_OUT_OF_MEMORY) == DOE_RESOURCE_OUT_OF_MEMORY);
    assert(doe_resource_render_pass_invalidate(pass, DOE_RESOURCE_UNSUPPORTED) == DOE_RESOURCE_OUT_OF_MEMORY);
    assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OUT_OF_MEMORY);
    doe_resource_render_pass_release(pass);
    doe_resource_commands *commands = (void *)(uintptr_t)1;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OUT_OF_MEMORY);
    assert(commands == (void *)(uintptr_t)1);
    doe_resource_encoder_release(encoder);
    doe_resource_view_release(view);
    assert(doe_resource_bundle_encoder_invalidate(NULL, DOE_RESOURCE_UNSUPPORTED) == DOE_RESOURCE_INVALID_ARGUMENT);
    assert(doe_resource_render_pass_invalidate(NULL, DOE_RESOURCE_UNSUPPORTED) == DOE_RESOURCE_INVALID_ARGUMENT);
    empty(&f);
}

int main(void)
{
    test_draw_boundaries();
    test_pipeline_topology();
    test_frontend_rejection();
    puts("draw admission owner: 3 host suites passed; no GPU execution claimed");
    return 0;
}
